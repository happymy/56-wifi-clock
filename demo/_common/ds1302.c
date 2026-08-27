#include "ds1302.h"

/* DS1302 三线接口：CE=P1.3, IO=P1.4(DSDA), SCLK=P1.5(DSCL)，bit-bang */
#define DS_CE    (1u << 3)
#define DS_IO    (1u << 4)
#define DS_SCLK  (1u << 5)

/* ponytail: 空循环会被 SDCC 优化掉；volatile 访问不会被优化，保证 SCLK 脉宽 ≥ DS1302 最小值(~1us) */
static void ds_delay(void) {
    volatile unsigned char i;
    for (i = 0; i < 40; i++);
}

static void ds_write_bit(unsigned char b) {
    P1 &= ~DS_SCLK;
    if (b) P1 |= DS_IO; else P1 &= ~DS_IO;
    ds_delay();
    P1 |= DS_SCLK;               /* 上升沿锁存 */
    ds_delay();
}

static unsigned char ds_read_bit(void) {
    unsigned char b;
    P1 &= ~DS_SCLK;              /* 下降沿后 DS1302 输出数据 */
    ds_delay();
    b = (P1 & DS_IO) ? 1 : 0;
    P1 |= DS_SCLK;
    ds_delay();
    return b;
}

static void ds_write_byte(unsigned char addr, unsigned char dat) {
    unsigned char i;
    P1 &= ~DS_SCLK;             /* DS1302：CE 上升沿前 SCLK 须为低，否则移位错位(读出乱码) */
    P1 |= DS_CE;
    ds_delay();
    for (i = 0; i < 8; i++) { ds_write_bit(addr & 1u); addr >>= 1; }
    for (i = 0; i < 8; i++) { ds_write_bit(dat & 1u); dat >>= 1; }
    P1 &= ~DS_CE;
}

static unsigned char ds_bcd_ok(unsigned char v, unsigned char max) {
    return (((v >> 4) & 0xF) <= 9 && (v & 0xF) <= 9 && v <= max);
}

static void ds_write_base(void) {
    ds_write_byte(0x8E, 0x00);   /* 解除写保护 */
    ds_write_byte(0x80, 0x00);   /* 秒=00, CH=0 启动 */
    ds_write_byte(0x82, 0x00);   /* 分=00 */
    ds_write_byte(0x84, 0x00);   /* 时=00 */
    ds_write_byte(0x86, 0x26);   /* 日=26 */
    ds_write_byte(0x88, 0x08);   /* 月=08 */
    ds_write_byte(0x8A, 0x03);   /* 周=03 */
    ds_write_byte(0x8C, 0x26);   /* 年=26 */
    ds_write_byte(0x8E, 0x80);   /* 写保护 */
}

unsigned char g_ds_init_action = 0;   /* 0=保住已走时 1=CH=1掉电(电池未保住) 2=BCD非法；供调试 */

void ds1302_init(void) {
    ds_time t;
    ds1302_read_time(&t);                    /* 上电首读可能过早读到乱码(CH=1)，重试确认 */
    if (t.sec & 0x80) {
        ds_delay(); ds_delay();
        ds1302_read_time(&t);
    }
    if (t.sec & 0x80) {                      /* 确属停振(掉电/电池未保住) */
        g_ds_init_action = 1;
        ds_write_base();
        return;
    }
    if (!ds_bcd_ok(t.sec, 0x59) || !ds_bcd_ok(t.min, 0x59) ||
        !ds_bcd_ok(t.hr, 0x23) || t.date == 0 || !ds_bcd_ok(t.date, 0x31) ||
        t.month == 0 || !ds_bcd_ok(t.month, 0x12) || !ds_bcd_ok(t.year, 0x99)) {
        g_ds_init_action = 2;
        ds_write_base();                     /* 纠正为基准时间 2026-08-26 00:00:00 */
        return;
    }
    g_ds_init_action = 0;                    /* 正常运行，保留已走时 */
}

/* ponytail: 读用突发(单 CE 周期，实测稳定)；单字节 ds_read_byte 在连续多次 CE 间会被芯片忽略
   读回全 FF。写用单字节 ds_write_byte(与 ds_write_base 同路径，实测可靠) */
void ds1302_read_time(ds_time *t) {
    unsigned char i, k, b;
    unsigned char *p = (unsigned char *)t;
    P1 &= ~DS_SCLK;             /* DS1302：CE 上升沿前 SCLK 须为低，否则移位错位(读出乱码) */
    P1 |= DS_CE;
    ds_delay();
    unsigned char addr = 0xBF;               /* 时钟突发读 */
    for (i = 0; i < 8; i++) { ds_write_bit(addr & 1u); addr >>= 1; }
    P1 |= DS_IO;                             /* 释放 IO */
    for (i = 0; i < 8; i++) {                /* 突发读须读满 8 字节(WP 在后)，否则芯片停在突发中，
                                                下次读命令被忽略 -> 全 FF；多读的第 8 字节丢弃 */
        b = 0;
        for (k = 0; k < 8; k++) if (ds_read_bit()) b |= (1u << k);
        if (i < 7) p[i] = b;
    }
    P1 &= ~DS_CE;
}

void ds1302_write_time(const ds_time *t) {
    ds_write_byte(0x8E, 0x00);   /* 解除写保护 */
    ds_write_byte(0x80, t->sec);
    ds_write_byte(0x82, t->min);
    ds_write_byte(0x84, t->hr);
    ds_write_byte(0x86, t->date);
    ds_write_byte(0x88, t->month);
    ds_write_byte(0x8A, t->weekday);
    ds_write_byte(0x8C, t->year);
    ds_write_byte(0x8E, 0x80);   /* 写保护，防误写(不影响芯片自身走时) */
}
