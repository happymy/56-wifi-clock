#include "tm1639.h"

/* 亮度：用 TM1639 自带 8 档硬件占空比(0x88|level, 0=1/16 最暗..7 最亮)。
   自动模式由光敏 ADC 映射到 8 档；手动模式用 cfg.bright_lvl。
   ponytail: 不另起 T0 中断做软件 PWM(省 ~700B)，足够 8 级亮度需求。 */

const unsigned char seg_font[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

static __xdata unsigned char g_bright = 7;   /* 当前亮度 0..7，写屏/调亮度共用 */

/* 倒装管七段旋转 180°：a↔d, b↔e, c↔f, g↔g, dp↔dp，补偿物理倒装 */
unsigned char seg_rotate180(unsigned char d) {
    return ((d >> 3) & 1u) << 0   /* a <- d */
         | ((d >> 4) & 1u) << 1   /* b <- e */
         | ((d >> 5) & 1u) << 2   /* c <- f */
         | ((d >> 0) & 1u) << 3   /* d <- a */
         | ((d >> 1) & 1u) << 4   /* e <- b */
         | ((d >> 2) & 1u) << 5   /* f <- c */
         | ((d >> 6) & 1u) << 6   /* g <- g */
         | (d & 0x80);            /* dp <- dp */
}

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

void tm1639_write_display(__xdata unsigned char *data) {
    unsigned char i;
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x40);              /* 数据命令：写显示，地址自增 */
    P2 |= TM_STB_MASK; tm_delay();
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0xC0);              /* 起始地址 00H(GRID1 低字节) */
    for (i = 0; i < 8; i++) {
        tm_write_byte(data[i] & 0x0F);        /* 低字节 SEG1-4 = a-d */
        tm_write_byte((data[i] >> 4) & 0x0F); /* 高字节 SEG9-12 = e-g,dp */
    }
    P2 |= TM_STB_MASK; tm_delay();
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x88 | g_bright);  /* 显示控制：开 + 当前占空比(0..7) */
    P2 |= TM_STB_MASK;
}

void tm1639_init(void) {
    unsigned char i;
    P2 |= TM_STB_MASK | TM_CLK_MASK | TM_DIO_MASK; /* 三线 idle 高 */
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x40);              /* 写显示, 地址自增 */
    P2 |= TM_STB_MASK; tm_delay();
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0xC0);              /* 起始地址 */
    for (i = 0; i < 8; i++) { tm_write_byte(0); tm_write_byte(0); }
    P2 |= TM_STB_MASK; tm_delay();
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x88);              /* 显示开, 占空比0(最暗) */
    P2 |= TM_STB_MASK;
}

/* 每帧 tm1639_write_display 已写 0x88|g_bright，故仅更新 g_bright 即可(省冗余 STB) */
void tm1639_set_brightness(unsigned char level) {
    if (level > 7) level = 7;
    g_bright = level;
}

/* 自动亮度: 光敏 light(0..1023) → 硬件 8 档(0..7)。暗室→更亮(沿用旧方向) */
void tm1639_set_light(unsigned int light) {
    g_bright = (unsigned char)(7 - (light >> 7));
}
