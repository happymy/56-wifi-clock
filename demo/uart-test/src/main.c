#include "tm1639.h"
#include "stc15.h"

/* 硬件串口回环测试：UART1 经 P3.1(TXD)/P3.0(RXD)，9600 8N1，内部 IRC 11.063MHz。
   STC15W408AS 无 Timer1，波特源必须用 Timer2。重装值 = 65536 - FOSC/4/BAUD。
   短接 P3.0<->P3.1 后，MCU 发出字节经短线回到 RX，比对即可验证 UART 收发。
   uart_send/uart_recv 亦即后续接 ESP-01 等模块所需的原语（ponytail: 回环为第一关）。 */

static void uart_init(void) {
    P3M1 &= ~0x02; P3M0 |= 0x02;   /* P3.1 推挽输出(否则驱动不出 TX 电平) */
    SCON  = 0x50;                  /* 模式1、8N1、REN=1 */
    T2L = 0xE0;
    T2H = 0xFE;
    AUXR  = 0x14;                  /* Timer2 1T 模式 + 启动 Timer2(bit4 BRTR) */
    AUXR |= 0x01;                  /* bit0 S1BRS=1：选 Timer2 作 UART1 波特源 */
}

static void uart_putc(unsigned char c) {
    unsigned int w = 0;
    SBUF = c;
    while (!(SCON & 0x02)) { if (++w >= 30000) break; }
    SCON &= ~0x02;                    /* 清 TI */
}
static void uart_str(const char *s) { while (*s) uart_putc((unsigned char)*s++); }
static void uart_hex(unsigned char v) {
    static const char HEX[] = "0123456789ABCDEF";
    uart_putc((unsigned char)HEX[v >> 4]);
    uart_putc((unsigned char)HEX[v & 0x0F]);
}

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

/* BEEP(P2.1) 与 LED_T(P1.2)：本测试仅用 LED_T 作通过/失败指示 */
#define LED_ON()   do { P1 &= ~0x04; } while (0)
#define LED_OFF()  do { P1 |= 0x04; } while (0)

/* 大屏(GRID1-4)=左发/右收(各 2 位十六进制)；SMG1/SMG2 小屏=状态 0 或 E */
static void render_lb(unsigned char d[8], unsigned char tx, unsigned char rx, unsigned char ok) {
    unsigned char st = ok ? 0x00 : 0x0E;   /* 0x0E -> seg_font['E'] */
    d[0] = seg_font[tx >> 4];                       /* 左十位 = 发高半字节 */
    d[1] = seg_font[tx & 0x0F];                     /* 左个位 = 发低半字节 */
    d[2] = seg_rotate180(seg_font[rx >> 4]);        /* 右十位 = 收高半字节(GRID3 倒装) */
    d[3] = seg_font[rx & 0x0F];                     /* 右个位 = 收低半字节 */
    d[5] = seg_font[st]; d[4] = seg_font[st];       /* SMG1 */
    d[7] = seg_font[st]; d[6] = seg_font[st];       /* SMG2 */
}

static void uart_send(unsigned char c) {
    unsigned int w = 0;
    SCON &= ~0x02;                /* 清可能残留的 TI */
    SBUF = c;
    while (!(SCON & 0x02)) { if (++w >= 30000) break; }  /* 带超时，波特异常时防止死等 */
    SCON &= ~0x02;
}
/* 带超时接收：成功返回 1 并写入 *out；超时返回 0 且 *out=0xFF（未短接/无数据源时触发） */
static unsigned char uart_recv_to(unsigned char *out) {
    unsigned int w = 0;
    SCON &= ~0x01;                /* 清可能残留的 RI，避免读到旧字节 */
    while (!(SCON & 0x01)) { if (++w >= 50000) { *out = 0xFF; return 0; } }
    *out = SBUF;
    SCON &= ~0x01;
    return 1;
}

void main(void) {
    unsigned char disp[8], got;
    unsigned char tx = 0, rx, ok;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽(本测试不用，仅保默认) */
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.0/3.1 准双向(UART) */
    P1 &= ~0x04;                 /* LED_T 红：运行指示 */

    uart_init();
    uart_str("uart-loopback start\r\n");
    tm1639_init();

    while (1) {
        uart_send(tx);
        got = uart_recv_to(&rx);
        ok = got && (rx == tx);      /* 仅当真正收到且与发送一致才算通过 */

        render_lb(disp, tx, rx, ok);
        tm1639_write_display(disp);

        if (ok) LED_ON(); else LED_OFF();

        uart_str("TX="); uart_hex(tx); uart_str(" RX="); uart_hex(rx);
        uart_str(ok ? " OK\r\n" : " FAIL\r\n");

        tx++;                        /* unsigned char 自然回卷 0xFF->0x00 */
        delay_ms(300);
    }
}
