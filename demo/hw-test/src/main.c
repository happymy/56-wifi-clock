#include "tm1639.h"
#include "ds1302.h"
#include "stc15.h"

/* 硬件生产测试固件：逐项验证整板功能，用于排查虚焊/漏焊。
   被排除项：ESP-01S 为成品模块，不测；其串口(UART1)改用 P3.0<->P3.1 回环自测。
   测试项与对应引脚：
       T1 显示自检  -> TM1639(P2.3/2.4/2.5) 主+SMG1 全段点亮查段；SMG2 交替 01./88.(带小数点)
      T2 实时时钟  -> DS1302(P1.3/1.4/1.5) 主屏 HH:MM + SMG1 SS 走时，判秒前进
       T3 串口自测  -> UART1(P3.0/3.1) 打印即证 TX；短接 P3.0<->P3.1 才回环 00,11..FF 验 RX，末显 HP
      T4 按键      -> SET(P3.3) 加高位、UP(P3.2) 加低位计数器
     T5 蜂鸣器    -> BEEP(P2.1) 发声(人耳确认)
     T6 状态灯    -> LED_T(P1.2) 闪烁(肉眼确认)
     T7 光敏      -> GM(P1.0) ADC 未悬空/未短接
      T8 热敏      -> RM(P1.1) ADC 未悬空/未短接
     T9 EEPROM    -> IAP 擦/写读回末扇区(连通性/虚焊，不遍历不测寿命)
     T10 断电保持 -> DS1302+CR1220(看 g_boot_action；手动切电核对，置最后)
     结果：自动判定项(T2/T4/T7/T8/T9，及 T3 仅在短接回环不匹配时)失败置位 g_fail；T1/T5/T6 由产线员肉眼确认，T3 未短接则跳过 RX 不判失败(能读到串口即证 TX 正常)。
    显示：SMG2(上排2位)常驻步骤号01~10；主屏(GRID1-4)/SMG1 显示该项内容或结果(0=过 E=败)，确认步骤闪烁提示。串口仅打印 ASCII 明细(防乱码)。 */

#define BIT(n) (1u << (n))
#define T_DISP 0
#define T_RTC  1
#define T_UART 2
#define T_KEY  3
#define T_BEEP 4
#define T_LED  5
#define T_LDR  6
#define T_NTC  7
#define T_EEP  8

static unsigned int g_fail;
static unsigned char g_boot_action;   /* 上电首读 ds1302_init 动作: 0=保住走时 1=掉电停振 2=BCD非法 */

/* ---------- 串口（复用 uart-test 已验证配置：Timer2 波特源 / 9600 8N1）---------- */
static void uart_init(void) {
    P3M1 &= ~0x02; P3M0 |= 0x02;   /* P3.1 推挽输出 */
    SCON  = 0x50;
    T2L = 0xE0; T2H = 0xFE;
    AUXR  = 0x14;                  /* Timer2 1T + 启动 */
    AUXR |= 0x01;                  /* S1BRS=1：Timer2 作 UART1 波特源 */
}
static void uart_putc(unsigned char c) {
    unsigned int w = 0;
    SBUF = c;
    while (!(SCON & 0x02)) { if (++w >= 30000) break; }
    SCON &= ~0x02;
}
static void uart_str(const char *s) { while (*s) uart_putc((unsigned char)*s++); }
static void uart_hex(unsigned char v) {
    static const char HEX[] = "0123456789ABCDEF";
    uart_putc((unsigned char)HEX[v >> 4]);
    uart_putc((unsigned char)HEX[v & 0x0F]);
}
static void uart_u8(unsigned char v) {
    unsigned char buf[3]; unsigned char i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[i++] = (unsigned char)('0' + v % 10); v /= 10; }
    while (i) uart_putc(buf[--i]);
}
/* ---------- 基础外设 ---------- */
#define BEEP_ON()  do { P2 &= ~0x02; } while (0)
#define BEEP_OFF() do { P2 |= 0x02; } while (0)
#define LED_ON()   do { P1 &= ~0x04; } while (0)
#define LED_OFF()  do { P1 |= 0x04; } while (0)

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}
static void beep_ms(unsigned int ms) { BEEP_ON(); delay_ms(ms); BEEP_OFF(); }

/* 10 位 ADC 查询读：ch 0=P1.0 光敏, 1=P1.1 热敏 */
static unsigned int adc_read(unsigned char ch) {
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL | (ch & 0x07) | ADC_START;
    while (!(ADC_CONTR & ADC_FLAG));
    ADC_CONTR &= ~ADC_FLAG;
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}

/* key_wait 已由 confirm_wait / blink_wait 取代 */

static unsigned char both_keys(void) { return (!(P3 & 0x04) && !(P3 & 0x08)); }

/* ---------- 显示辅助 ---------- */
/* 十六进制段码（0-F），用于 T3 左发右显 */
static const unsigned char SEG_HEX[16] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F, /*0-9*/
    0x77,0x7C,0x39,0x5E,0x79,0x71                          /*A,b,C,d,E,F*/
};
/* 步骤号常驻 SMG2(上排2位)，让产线员不看串口也知道测到哪一项 */
static void disp_step(unsigned char disp[8], unsigned char n) {
    /* 物理上小屏右管为 disp[6]、左管为 disp[7]，故个位放 disp[6]、十位放 disp[7] */
    disp[6] = seg_font[n % 10];
    disp[7] = seg_font[n / 10];
}
static void clear_main(unsigned char disp[8]) {
    unsigned char i;
    for (i = 0; i < 6; i++) disp[i] = 0;   /* 清主4位+SMG1，保留 SMG2 步骤号 */
}
/* 主4位显示无符号整数(0-1023) */
static void disp_u16(unsigned char disp[8], unsigned int val) {
    disp[0] = seg_font[val / 1000];
    disp[1] = seg_font[(val / 100) % 10];
    disp[2] = seg_rotate180(seg_font[(val / 10) % 10]);  /* GRID3 倒装 */
    disp[3] = seg_font[val % 10];
}
/* 4 位计数器：高2位 hi(0-99) + 低2位 lo(0-99)，用于 T4 */
static void disp_hilo(unsigned char disp[8], unsigned char hi, unsigned char lo) {
    disp[0] = seg_font[hi / 10]; disp[1] = seg_font[hi % 10];
    disp[2] = seg_rotate180(seg_font[lo / 10]); disp[3] = seg_font[lo % 10];  /* GRID3 倒装 */
}
/* 单步结果闪现：主4位+SMG1 显 0(过)/E(败)，SMG2 保留步骤号 */
static void flash_step(unsigned char disp[8], unsigned char n, unsigned char ok) {
    unsigned char i, c = ok ? 0x3F : 0x79;
    for (i = 0; i < 6; i++) disp[i] = (i == 2) ? seg_rotate180(c) : c;  /* GRID3 倒装 */
    disp_step(disp, n);
    tm1639_write_display(disp);
    delay_ms(800);
}
/* 确认步骤：SMG2 步骤号闪烁提示“需人工”，期间 p3bit 按下即通过 */
static unsigned char confirm_wait(unsigned char disp[8], unsigned char n, unsigned char p3bit, unsigned int ms) {
    unsigned int t = 0; unsigned char on = 1;
    while (t < ms) {
        if (on) disp_step(disp, n); else { disp[6] = disp[7] = 0; }
        tm1639_write_display(disp);
        if (!(P3 & p3bit)) return 1;
        delay_ms(250); t += 250; on = !on;
    }
    return 0;
}
static void show_allseg(unsigned char disp[8]) {
    unsigned char i;
    for (i = 0; i < 8; i++) disp[i] = 0xFF;
}
static void show_result(unsigned char disp[8], unsigned char ok) {
    unsigned char i;
    unsigned char c = ok ? 0x3F /* '0' */ : 0x79 /* 'E' */;
    for (i = 0; i < 8; i++) {
        disp[i] = (i == 2) ? seg_rotate180(c) : c;
    }
}

/* ---------- 各测试项 ---------- */
static unsigned char test_rtc(void) {
    ds_time t;
    unsigned char s0, s;
    unsigned int tries;
    /* delay_ms 在 11MHz/1T 下仅约 0.33ms/次，故 RTC 走时判定改用轮询等秒变化，不依赖延时精度 */
    ds1302_read_time(&t);
    if (t.sec & 0x80) {                       /* 停振：写基准时间启动后复读 */
        ds_time d;
        d.sec = 0x00; d.min = 0x00; d.hr = 0x00;
        d.date = 0x01; d.month = 0x01; d.weekday = 0x01; d.year = 0x26;
        ds1302_write_time(&d);
        ds1302_read_time(&t);
        if (t.sec & 0x80) return 0;
    }
    s0 = (unsigned char)((t.sec & 0x7F) >> 4) * 10 + (t.sec & 0x0F);
    for (tries = 0; tries < 10000; tries++) {
        ds1302_read_time(&t);
        s = (unsigned char)((t.sec & 0x7F) >> 4) * 10 + (t.sec & 0x0F);
        if (s != s0) return 1;                 /* 确已走时 */
        delay_ms(1);
    }
    return 0;                                  /* 超时未走时 */
}

static void uart_send(unsigned char c) {
    unsigned int w = 0;
    SCON &= ~0x02; SBUF = c;
    while (!(SCON & 0x02)) { if (++w >= 30000) break; }
    SCON &= ~0x02;
}
static unsigned char uart_recv_to(unsigned char *out) {
    unsigned int w = 0;
    /* 不在开头清 RI：调用方须先 uart_send 前清 RI，否则会误清掉已到达的回显 */
    while (!(SCON & 0x01)) { if (++w >= 50000) { *out = 0xFF; return 0; } }
    *out = SBUF; SCON &= ~0x01;
    return 1;
}
/* test_uart 已由 T3 内联 00,11..FF 回环取代 */

/* ---------- EEPROM 连通性（虚焊排查）：擦末扇区 0x1200(512B)+写读回几字节 ---------- */
/* 官方：IAP_CONTR=0x84 (IAPEN + WT=100@11.051MHz)；IAP_CMD 0x01读/0x02写/0x03擦；
   触发序 0x5A→0xA5；地址每次重设(不自动加1)；结束 IapIdle 防误写。EEPROM 5K@0x0000-0x13FF */
static void iap_wait(void) { volatile unsigned char k; for (k = 0; k < 40; k++); }
static void iap_trig(void) { IAP_TRIG = 0x5A; IAP_TRIG = 0xA5; iap_wait(); }
static void iap_idle(void) {
    IAP_CONTR = 0; IAP_CMD = 0; IAP_TRIG = 0;
    IAP_ADDRH = 0x80; IAP_ADDRL = 0;   /* 指到 EEPROM 外，防误写 */
}
static unsigned char test_eeprom(void) {
    static const unsigned char AD[3] = {0x00, 0x01, 0xFF};   /* 0x1200/0x1201/0x12FF */
    static const unsigned char PA[3] = {0x5A, 0xA5, 0x3C};
    unsigned int i;
    unsigned char ok = 1;
    IAP_CONTR = 0x84;                                  /* 使能 + 等待时间(≤12MHz) */
    /* 擦除 0x1200 扇区(512B)，回读应全 0xFF */
    IAP_ADDRH = 0x12; IAP_ADDRL = 0x00;
    IAP_CMD = 0x03; iap_trig();
    for (i = 0; i < 512; i++) {
        IAP_ADDRH = (unsigned char)(0x12 + (i >> 8)); IAP_ADDRL = (unsigned char)i;
        IAP_CMD = 0x01; iap_trig();
        if (IAP_DATA != 0xFF) { ok = 0; break; }
    }
    if (ok) {                                          /* 写 3 个不同位置，再读回比对 */
        for (i = 0; i < 3; i++) {
            IAP_ADDRH = 0x12; IAP_ADDRL = AD[i];
            IAP_DATA = PA[i]; IAP_CMD = 0x02; iap_trig();
        }
        for (i = 0; i < 3; i++) {
            IAP_ADDRH = 0x12; IAP_ADDRL = AD[i];
            IAP_CMD = 0x01; iap_trig();
            if (IAP_DATA != PA[i]) { ok = 0; break; }
        }
    }
    if (IAP_CONTR & 0x10) { IAP_CONTR &= ~0x10; ok = 0; }   /* CMD_FAIL：地址越界等 */
    iap_idle();
    return ok;
}

/* ---------- 主测试流程 ---------- */
static void run_tests(void) {
    unsigned char disp[8];
    unsigned int v;
    unsigned char i, ok = 0;

    g_fail = 0;
    uart_str("==== HW TEST ====\r\n");

    /* T1 显示自检：主+SMG1 全段点亮查段；SMG2 交替 01./88.(两管各带小数点) 待 SET 或超时 */
    clear_main(disp);
    for (i = 0; i < 6; i++) disp[i] = 0xFF;   /* 主4位+SMG1 全段点亮 */
    tm1639_write_display(disp);
    uart_str("T1 DISP: all segments lit; SMG2 01./88. [confirm SET]\r\n");
    {
        unsigned char phase = 0; unsigned int t = 0;
        while (t < 4000) {
            if (phase) { disp[6] = 0x7F | 0x80; disp[7] = 0x7F | 0x80; }              /* 88 带两 dp */
            else      { disp[6] = seg_font[1] | 0x80; disp[7] = seg_font[0] | 0x80; } /* 01 带两 dp */
            tm1639_write_display(disp);
            if (!(P3 & 0x08)) break;          /* SET 按下即过 */
            delay_ms(500); t += 500; phase = !phase;
        }
    }

    /* T2 实时时钟：主显 HH:MM，SMG1 显 SS，SMG2=02，5s */
    {
        ds_time tt; unsigned int tries; unsigned char s0, s, hh, mm, ss;
        clear_main(disp); disp_step(disp, 2); tm1639_write_display(disp);
        uart_str("T2 RTC: ticking\r\n");
        ds1302_read_time(&tt);
        if (tt.sec & 0x80) {
            ds_time d; d.sec = 0; d.min = 0; d.hr = 0; d.date = 1; d.month = 1; d.weekday = 1; d.year = 0x26;
            ds1302_write_time(&d); ds1302_read_time(&tt);
        }
        s0 = (unsigned char)((tt.sec & 0x7F) >> 4) * 10 + (tt.sec & 0x0F);
        for (tries = 0; tries < 10000; tries++) {
            ds1302_read_time(&tt);
            s = (unsigned char)((tt.sec & 0x7F) >> 4) * 10 + (tt.sec & 0x0F);
            if (s != s0) { ok = 1; break; }
            delay_ms(1);
        }
        for (i = 0; i < 10; i++) {
            ds1302_read_time(&tt);
            hh = (unsigned char)((tt.hr >> 4) * 10 + (tt.hr & 0x0F));
            mm = (unsigned char)((tt.min >> 4) * 10 + (tt.min & 0x0F));
            ss = (unsigned char)((tt.sec & 0x7F) >> 4) * 10 + (tt.sec & 0x0F);
            clear_main(disp);
            disp[0] = seg_font[hh / 10]; disp[1] = seg_font[hh % 10] | 0x80;
            disp[2] = seg_rotate180(seg_font[mm / 10]); disp[3] = seg_font[mm % 10];  /* GRID3 倒装，分钟十位需旋转 */
            disp[4] = seg_font[ss % 10]; disp[5] = seg_font[ss / 10];   /* SMG1 物理左右颠倒，个位放左(disp[4])、十位放右(disp[5]) */
            disp_step(disp, 2); tm1639_write_display(disp);
            delay_ms(500);
        }
        if (ok) { uart_str("T2 RTC: PASS\r\n"); } else { g_fail |= BIT(T_RTC); uart_str("T2 RTC: FAIL\r\n"); }
        flash_step(disp, 2, ok);
    }

    /* T3 串口自测：固件打印本身即在 P3.1 发出、操作员能读到即证明 TX+焊接 OK；
       若短接 P3.0<->P3.1 则额外跑 00,11..FF 16 值回环确认 RX。未短接不算失败(跳过 RX)。 */
    {
        unsigned char i, tx, rx;
        unsigned char looped = 0, ok = 1;
        clear_main(disp); disp_step(disp, 3); tm1639_write_display(disp);
        uart_str("T3 UART: TX banner (readable = TX+焊接 OK)\r\n");
        SCON &= ~0x01; uart_send(0x55);                 /* 探测：短接则回显 */
        if (uart_recv_to(&rx) && rx == 0x55) looped = 1;
        if (looped) {
            for (i = 0; i < 16; i++) {
                tx = (unsigned char)(i * 0x11);
                SCON &= ~0x01; uart_send(tx);           /* 先清 RI 专等本次回显 */
                if (!(uart_recv_to(&rx) && rx == tx)) ok = 0;
                clear_main(disp);
                disp[0] = SEG_HEX[tx >> 4]; disp[1] = SEG_HEX[tx & 0x0F];
                disp[2] = seg_rotate180(SEG_HEX[rx >> 4]); disp[3] = SEG_HEX[rx & 0x0F];
                disp_step(disp, 3); tm1639_write_display(disp);
                delay_ms(1000);
            }
            if (ok) uart_str("T3 UART: PASS (loopback ok)\r\n");
            else { g_fail |= BIT(T_UART); uart_str("T3 UART: FAIL (loopback mismatch)\r\n"); }
        } else {
            uart_str("T3 UART: TX ok, loopback not shorted -> skip RX (no fail)\r\n");
        }
        clear_main(disp); disp[0] = 0x76; disp[1] = 0x73; disp_step(disp, 3); tm1639_write_display(disp);  /* HP */
        delay_ms(3000);
    }

    /* T4 按键：SET 加高位、UP 加低位，5s 内两键都按过即 PASS，SMG2=04 */
    {
        unsigned char hi = 0, lo = 0, done = 0; unsigned int t = 0;
        clear_main(disp); disp_step(disp, 4); disp_hilo(disp, hi, lo); tm1639_write_display(disp);
        uart_str("T4 KEY: SET->hi+1, UP->lo+1 (within 5s)\r\n");
        while (t < 5000) {
            if (!(P3 & 0x04)) { delay_ms(30); if (!(P3 & 0x04)) { hi = (hi + 1) % 100; disp_hilo(disp, hi, lo); tm1639_write_display(disp); while (!(P3 & 0x04)); } }
            if (!(P3 & 0x08)) { delay_ms(30); if (!(P3 & 0x08)) { lo = (lo + 1) % 100; disp_hilo(disp, hi, lo); tm1639_write_display(disp); while (!(P3 & 0x08)); } }
            if (hi > 0 && lo > 0) { done = 1; break; }
            delay_ms(10); t += 10;
        }
        if (done) { uart_str("T4 KEY: PASS\r\n"); flash_step(disp, 4, 1); }
        else { g_fail |= BIT(T_KEY); uart_str("T4 KEY: FAIL\r\n"); flash_step(disp, 4, 0); }
    }

    /* T5 蜂鸣器：响 3 次，SMG2=05 */
    clear_main(disp); disp[0] = 0x7C; disp[1] = 0x79; disp[2] = seg_rotate180(0x79); disp[3] = 0x73; disp_step(disp, 5); tm1639_write_display(disp);  /* bEEP (GRID3 倒装) */
    uart_str("T5 BEEP: 3 beeps\r\n");
    { unsigned char k; for (k = 0; k < 3; k++) { BEEP_ON(); delay_ms(200); BEEP_OFF(); delay_ms(200); } }

    /* T6 状态灯：闪 5s，SMG2=06 */
    clear_main(disp); disp[0] = 0x06; disp[1] = 0x79; disp[2] = seg_rotate180(0x5E); disp_step(disp, 6); tm1639_write_display(disp);  /* LED: 7段无L/C之分，首字用1代L */
    uart_str("T6 LED: blink 5s\r\n");
    { unsigned char k; for (k = 0; k < 10; k++) { LED_ON(); delay_ms(250); LED_OFF(); delay_ms(250); } }
    LED_ON();

    /* T7 光敏 ADC：显 raw 5s，SMG2=07 */
    clear_main(disp); disp_step(disp, 7); tm1639_write_display(disp);
    v = adc_read(0);
    uart_str("T7 LDR: raw="); uart_u8((unsigned char)v); uart_str("\r\n");
    clear_main(disp); disp_u16(disp, v); disp_step(disp, 7); tm1639_write_display(disp);
    delay_ms(10000);
    if (v > 0 && v < 1023) { uart_str("T7 LDR: PASS\r\n"); flash_step(disp, 7, 1); }
    else { g_fail |= BIT(T_LDR); uart_str("T7 LDR: FAIL\r\n"); flash_step(disp, 7, 0); }

    /* T8 热敏 ADC：显 raw 5s，SMG2=08 */
    clear_main(disp); disp_step(disp, 8); tm1639_write_display(disp);
    v = adc_read(1);
    uart_str("T8 NTC: raw="); uart_u8((unsigned char)v); uart_str("\r\n");
    clear_main(disp); disp_u16(disp, v); disp_step(disp, 8); tm1639_write_display(disp);
    delay_ms(10000);
    if (v > 0 && v < 1023) { uart_str("T8 NTC: PASS\r\n"); flash_step(disp, 8, 1); }
    else { g_fail |= BIT(T_NTC); uart_str("T8 NTC: FAIL\r\n"); flash_step(disp, 8, 0); }

    /* T9 EEPROM 连通性：通过显 01，错误显 EE，结果 5s，SMG2=09 */
    clear_main(disp); disp_step(disp, 9); tm1639_write_display(disp);
    uart_str("T9 EEPROM: erase+rd/wr 0x1200\r\n");
    ok = test_eeprom();
    clear_main(disp);
    if (ok) { disp[1] = 0x73; disp[2] = seg_rotate180(seg_font[1]); }   /* P1 (GRID3 倒装) */
    else { disp[1] = 0x79; disp[2] = seg_rotate180(0x79); }                    /* EE (GRID3 倒装) */
    disp_step(disp, 9); tm1639_write_display(disp);
    if (ok) uart_str("T9 EEPROM: PASS\r\n");
    else { g_fail |= BIT(T_EEP); uart_str("T9 EEPROM: FAIL\r\n"); }
    delay_ms(5000);

    /* T10 断电保持（最后）：SMG2=10，结果 5s */
    clear_main(disp); disp_step(disp, 10); tm1639_write_display(disp);
    uart_str("T10 RETENTION: code=P"); uart_putc((unsigned char)('0' + g_boot_action + 1));
    if (g_boot_action == 0) uart_str(" PASS (battery ok)\r\n");
    else if (g_boot_action == 1) uart_str(" WARN (stopped: new cell)\r\n");
    else uart_str(" WARN (stopped: dead battery)\r\n");
    clear_main(disp); disp[0] = 0x73; disp[1] = seg_font[g_boot_action + 1]; disp_step(disp, 10); tm1639_write_display(disp);  /* P1=正常 P2/P3=异常 */
    delay_ms(5000);

    /* 汇总（其他不变） */
    show_result(disp, g_fail == 0); tm1639_write_display(disp);
    if (g_fail == 0) {
        uart_str("ALL PASS\r\n");
        beep_ms(120); delay_ms(120); beep_ms(120);
    } else {
        static const char * const tname[9] = {"T1","T2","T3","T4","T5","T6","T7","T8","T9"};
        unsigned char b;
        uart_str("FAIL =");
        for (b = 0; b < 9; b++) if (g_fail & (1u << b)) uart_str(tname[b]);
        uart_str("\r\n");
        beep_ms(600);
    }
}

void main(void) {
    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP 推挽 */
    BEEP_OFF();
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* 按键准双向(内部上拉) */
    LED_ON();

    P1ASF |= 0x03;               /* P1.0/1.1 模拟输入 */
    P1M1 |= 0x03; P1M0 &= ~0x03; /* 高阻 */
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL;
    delay_ms(1);

    tm1639_init();
    tm1639_set_brightness(7);    /* 测试用最大亮度 */
    delay_ms(200);

    uart_init();
    uart_str("HWTEST boot\r\n");

    delay_ms(500);               /* 等 Vcc 稳再读 DS1302 */
    ds1302_init();
    g_boot_action = g_ds_init_action;   /* 捕获断电保持信号，供 T9 判定 */

    while (1) {
        run_tests();
        /* 保持结果画面；两键同按重新测试 */
        while (1) {
            if (both_keys()) { delay_ms(50); if (both_keys()) break; }
            delay_ms(50);
        }
        delay_ms(300);
    }
}
