#include "tm1639.h"

/* 共阴七段字模：bit 序 dp,g,f,e,d,c,b,a
   全亮 '8' = 0x7F，'-' = 0x40，blank = 0x00 */
const unsigned char seg_font[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

static void tm_delay(void) {
    volatile unsigned char i;
    for (i = 0; i < 10; i++) { /* ~us 级，满足 TM1639 时序 */
    }
}

/* 低位先发；CLK 上升沿锁存 DIO */
static void tm_write_byte(unsigned char dat) {
    unsigned char i;
    for (i = 0; i < 8; i++) {
        P2 &= ~TM_CLK_MASK;
        if (dat & 0x01) P2 |= TM_DIO_MASK; else P2 &= ~TM_DIO_MASK;
        tm_delay();
        P2 |= TM_CLK_MASK;
        tm_delay();
        dat >>= 1;
    }
}

void tm1639_init(void) {
    unsigned char blank[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    P2 |= TM_STB_MASK | TM_CLK_MASK | TM_DIO_MASK; /* 三线 idle 高 */
    tm1639_write_display(blank);
}

/* data[0..7] 对应 GRID1..GRID8（TM1639 16 段模式：每 GRID 占 2 字节，
   低字节=SEG1-8(a-h)，高字节=SEG9-16(空)）。发 16 字节兼容 8/16 段。 */
void tm1639_write_display(const unsigned char data[8]) {
    unsigned char i;
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x40);              /* 数据命令：写显示，地址自增 */
    P2 |= TM_STB_MASK; tm_delay();

    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0xC0);              /* 起始地址 00H(GRID1 低字节) */
    for (i = 0; i < 8; i++) {
        tm_write_byte(data[i]);       /* 低字节 = SEG1-8(a-h) */
        tm_write_byte(0x00);          /* 高字节 = SEG9-16(空) */
    }
    P2 |= TM_STB_MASK; tm_delay();

    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x8F);              /* 显示控制：开 + 最大亮度 */
    P2 |= TM_STB_MASK;
}

void tm1639_set_brightness(unsigned char level) {
    unsigned char cmd;
    if (level > 7) level = 7;
    cmd = 0x80 | (level << 1) | 0x01; /* 亮度 + 显示开 */
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(cmd);
    P2 |= TM_STB_MASK;
}
