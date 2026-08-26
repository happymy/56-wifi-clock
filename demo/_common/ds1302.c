#include "ds1302.h"

/* DS1302 三线接口：CE=P1.3, IO=P1.4(DSDA), SCLK=P1.5(DSCL)，bit-bang */
#define DS_CE    (1u << 3)
#define DS_IO    (1u << 4)
#define DS_SCLK  (1u << 5)

static void ds_delay(void) {
    unsigned char i;
    for (i = 0; i < 6; i++);
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
    P1 |= DS_CE;
    ds_delay();
    for (i = 0; i < 8; i++) { ds_write_bit(addr & 1u); addr >>= 1; }
    for (i = 0; i < 8; i++) { ds_write_bit(dat & 1u); dat >>= 1; }
    P1 &= ~DS_CE;
}

static unsigned char ds_read_byte(unsigned char addr) {
    unsigned char i, dat = 0;
    P1 |= DS_CE;
    ds_delay();
    for (i = 0; i < 8; i++) { ds_write_bit(addr & 1u); addr >>= 1; }
    P1 |= DS_IO;                 /* 释放 IO 为输入(准双向) */
    for (i = 0; i < 8; i++) if (ds_read_bit()) dat |= (1u << i);
    P1 &= ~DS_CE;
    return dat;
}

void ds1302_init(void) {
    unsigned char s = ds_read_byte(0x81);   /* 读秒 */
    if (s & 0x80) {                          /* CH=1：停振未走时 */
        ds_write_byte(0x8E, 0x00);           /* 解除写保护 */
        ds_write_byte(0x80, 0x00);           /* 秒=00, CH=0 启动 */
        ds_write_byte(0x82, 0x00);           /* 分=00 */
        ds_write_byte(0x84, 0x12);           /* 时=12 */
        ds_write_byte(0x86, 0x01);           /* 日=01 */
        ds_write_byte(0x88, 0x01);           /* 月=01 */
        ds_write_byte(0x8A, 0x01);           /* 周=01 */
        ds_write_byte(0x8C, 0x25);           /* 年=25 */
        ds_write_byte(0x8E, 0x80);           /* 写保护 */
    }
}

void ds1302_read_time(ds_time *t) {
    unsigned char i;
    unsigned char *p = (unsigned char *)t;
    P1 |= DS_CE;
    ds_delay();
    unsigned char addr = 0xBF;               /* 时钟突发读 */
    for (i = 0; i < 8; i++) { ds_write_bit(addr & 1u); addr >>= 1; }
    P1 |= DS_IO;                             /* 释放 IO */
    for (i = 0; i < 7; i++) {
        unsigned char b = 0, k;
        for (k = 0; k < 8; k++) if (ds_read_bit()) b |= (1u << k);
        p[i] = b;
    }
    P1 &= ~DS_CE;
}

void ds1302_write_time(const ds_time *t) {
    unsigned char i, k, byte;
    const unsigned char *p = (const unsigned char *)t;
    ds_write_byte(0x8E, 0x00);   /* 解除写保护 */
    P1 |= DS_CE;
    ds_delay();
    byte = 0xBE;                 /* 时钟突发写命令 */
    for (i = 0; i < 8; i++) { ds_write_bit(byte & 1u); byte >>= 1; }
    for (i = 0; i < 7; i++) {    /* 7 字节：sec,min,hr,date,month,weekday,year */
        byte = p[i];
        for (k = 0; k < 8; k++) { ds_write_bit(byte & 1u); byte >>= 1; }
    }
    byte = 0x00;                 /* WP 字节：保持可写，便于后续再次写入 */
    for (k = 0; k < 8; k++) { ds_write_bit(byte & 1u); byte >>= 1; }
    P1 &= ~DS_CE;
}
