#include "tm1639.h"
#include "ds1302.h"
#include "stc15.h"

/* 硬件生产测试固件：逐项验证整板功能，用于排查虚焊/漏焊。
   被排除项：ESP-01S 为成品模块，不测；其串口(UART1)改用 P3.0<->P3.1 回环自测。
   测试项与对应引脚：
     T1 显示自检  -> TM1639(P2.3/2.4/2.5) 全段点亮，肉眼查每管每段
     T2 实时时钟  -> DS1302(P1.3/1.4/1.5) 起振+走时
     T3 串口回环  -> UART1(P3.0/3.1) 短接后自发自收
     T4 按键      -> UP(P3.2)/SET(P3.3) 实测按下
     T5 蜂鸣器    -> BEEP(P2.1) 发声(人耳确认)
     T6 状态灯    -> LED_T(P1.2) 闪烁(肉眼确认)
     T7 光敏      -> GM(P1.0) ADC 未悬空/未短接
      T8 热敏      -> RM(P1.1) ADC 未悬空/未短接
     T9 EEPROM    -> IAP 擦/写读回末扇区(连通性/虚焊，不遍历不测寿命)
     T10 断电保持 -> DS1302+CR1220(看 g_boot_action；手动切电核对，置最后)
    结果：自动判定项(T2/T3/T4/T7/T8/T9)失败置位 g_fail；T1/T5/T6 由产线员肉眼确认。
   显示：大屏(GRID1-4)显示当前测试号；全过显「0」、有失败显「E」。串口打印明细。 */

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
/* 打印 BCD 时间 HH:MM:SS（供手动断电保持核对） */
static void dbg_time(const ds_time *t) {
    unsigned char h = (unsigned char)((t->hr >> 4) * 10 + (t->hr & 0x0F));
    unsigned char m = (unsigned char)((t->min >> 4) * 10 + (t->min & 0x0F));
    unsigned char s = (unsigned char)((t->sec & 0x7F) >> 4) * 10 + (t->sec & 0x0F);
    uart_u8(h); uart_putc(':'); uart_u8(m); uart_putc(':'); uart_u8(s);
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

/* 等待某键(P3 位为低=按下)最多 ms 毫秒，期间驱动显示刷屏；按下返回 1，超时返回 0 */
static unsigned char key_wait(unsigned char p3bit, unsigned int ms, unsigned char disp[8]) {
    unsigned int t = 0;
    while (t < ms) {
        tm1639_write_display(disp);
        if (!(P3 & p3bit)) return 1;
        delay_ms(10); t += 10;
    }
    return 0;
}

static unsigned char both_keys(void) { return (!(P3 & 0x04) && !(P3 & 0x08)); }

/* ---------- 显示辅助 ---------- */
static void show_stage(unsigned char disp[8], unsigned char n) {
    disp[0] = seg_font[n / 10];
    disp[1] = seg_font[n % 10];
    disp[2] = seg_rotate180(seg_font[n / 10]);
    disp[3] = seg_font[n % 10];
    disp[4] = disp[5] = disp[6] = disp[7] = 0;
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
    SCON &= ~0x01;
    while (!(SCON & 0x01)) { if (++w >= 50000) { *out = 0xFF; return 0; } }
    *out = SBUF; SCON &= ~0x01;
    return 1;
}
static unsigned char test_uart(void) {
    static const unsigned char pat[] = {0x55, 0xAA, 0x00, 0xFF, 0x12, 0x34};
    unsigned char i, rx;
    for (i = 0; i < sizeof(pat); i++) {
        uart_send(pat[i]);
        if (!uart_recv_to(&rx) || rx != pat[i]) return 0;
    }
    return 1;
}

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

    g_fail = 0;
    uart_str("==== HW TEST ====\r\n");

    /* T1 显示自检（肉眼确认）：先全段查断段，再 12345678 查 GRID 顺序/倒装管方向 */
    show_allseg(disp); tm1639_write_display(disp);
    uart_str("T1 DISP: 查 8 管全段点亮(operator confirm)\r\n");
    key_wait(0x08, 3000, disp);                /* SET 或超时进入下一项 */
    {
        unsigned char k;
        for (k = 0; k < 8; k++)
            disp[k] = (k == 2) ? seg_rotate180(seg_font[k + 1]) : seg_font[k + 1];
        tm1639_write_display(disp);
    }
    uart_str("T1 DISP: 查左→右 12345678 顺序正确(operator confirm)\r\n");
    key_wait(0x08, 3000, disp);

    /* T2 实时时钟：起振+走时(自动)；断电保持见计划 T9（手动切电核对） */
    show_stage(disp, 2); tm1639_write_display(disp);
    {
        ds_time tt;
        if (test_rtc()) uart_str("T2 RTC: PASS (tick ok)\r\n");
        else { g_fail |= BIT(T_RTC); uart_str("T2 RTC: FAIL (no tick / not run)\r\n"); }
        ds1302_read_time(&tt);
        uart_str("T2 RTC: CH="); uart_putc((tt.sec & 0x80) ? '1' : '0');
        uart_str(" now="); dbg_time(&tt); uart_str("\r\n");
    }

    /* T3 串口回环 */
    show_stage(disp, 3); tm1639_write_display(disp);
    if (test_uart()) uart_str("T3 UART: PASS (loopback ok)\r\n");
    else { g_fail |= BIT(T_UART); uart_str("T3 UART: FAIL (need P3.0-P3.1 short)\r\n"); }

    /* T4 按键（实测按下） */
    show_stage(disp, 4); tm1639_write_display(disp);
    uart_str("T4 KEY: press UP then SET\r\n");
    if (key_wait(0x04, 15000, disp) && key_wait(0x08, 15000, disp)) {
        uart_str("T4 KEY: PASS\r\n");
    } else { g_fail |= BIT(T_KEY); uart_str("T4 KEY: FAIL (no press)\r\n"); }

    /* T5 蜂鸣器（人耳确认） */
    show_stage(disp, 5); tm1639_write_display(disp);
    uart_str("T5 BEEP: listen (operator confirm)\r\n");
    beep_ms(300); delay_ms(200);

    /* T6 状态灯（肉眼确认） */
    show_stage(disp, 6); tm1639_write_display(disp);
    uart_str("T6 LED: observe red blink (operator confirm)\r\n");
    { unsigned char i; for (i = 0; i < 4; i++) { LED_ON(); delay_ms(150); LED_OFF(); delay_ms(150); } }
    LED_ON();

    /* T7 光敏 ADC */
    show_stage(disp, 7); tm1639_write_display(disp);
    v = adc_read(0);
    uart_str("T7 LDR: raw="); uart_u8((unsigned char)v); uart_str("\r\n");
    if (v > 0 && v < 1023) uart_str("T7 LDR: PASS\r\n");
    else { g_fail |= BIT(T_LDR); uart_str("T7 LDR: FAIL (open/short)\r\n"); }

    /* T8 热敏 ADC */
    show_stage(disp, 8); tm1639_write_display(disp);
    v = adc_read(1);
    uart_str("T8 NTC: raw="); uart_u8((unsigned char)v); uart_str("\r\n");
    if (v > 0 && v < 1023) uart_str("T8 NTC: PASS\r\n");
    else { g_fail |= BIT(T_NTC); uart_str("T8 NTC: FAIL (open/short)\r\n"); }

    /* T9 EEPROM 连通性（虚焊排查：擦+写读回末扇区；自动，无需手动断电，故在 T10 前） */
    show_stage(disp, 9); tm1639_write_display(disp);
    if (test_eeprom()) uart_str("T9 EEPROM: PASS (erase+rd/wr 0x1200 ok)\r\n");
    else { g_fail |= BIT(T_EEP); uart_str("T9 EEPROM: FAIL (IAP bus / solder)\r\n"); }

    /* T10 断电保持（最后一项；上电首读 g_ds_init_action 即电池保持信号，逻辑同 ds1302-clock 已验证） */
    show_stage(disp, 10); tm1639_write_display(disp);
    uart_str("T10 RETENTION: boot_action="); uart_putc('0' + g_boot_action);
    if (g_boot_action == 0)
        uart_str(" PASS (上电已在走时=电池保住)\r\n");
    else
        uart_str(" WARN 上电停振(新板或电池失效)，已启动；请切主电>=3s再上电确认\r\n");
    key_wait(0x08, 3000, disp);

    /* 汇总 */
    show_result(disp, g_fail == 0); tm1639_write_display(disp);
    if (g_fail == 0) {
        uart_str("ALL PASS\r\n");
        beep_ms(120); delay_ms(120); beep_ms(120);   /* 双短音 */
    } else {
        uart_str("FAIL mask="); uart_hex((unsigned char)(g_fail >> 8)); uart_hex((unsigned char)(g_fail & 0xFF)); uart_str("\r\n");
        beep_ms(600);                                  /* 长音 */
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
