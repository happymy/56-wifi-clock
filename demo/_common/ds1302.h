#ifndef DS1302_H
#define DS1302_H

#include "stc15.h"

typedef struct {
    unsigned char sec;
    unsigned char min;
    unsigned char hr;
    unsigned char date;
    unsigned char month;
    unsigned char weekday;
    unsigned char year;
} ds_time;

void ds1302_init(void);          /* 未走时则写入默认时间并启动 */
extern unsigned char g_ds_init_action;  /* 上电自检结果：0保住 1掉电 2非法 */
void ds1302_read_time(ds_time *t); /* 突发读 8 字节(第8字节 WP 丢弃)，BCD */
void ds1302_write_time(const ds_time *t); /* 单字节写 7 寄存器，BCD（写前自动解写保护） */

#endif
