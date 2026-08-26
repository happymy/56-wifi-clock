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
void ds1302_read_time(ds_time *t); /* 突发读 7 字节，BCD */
void ds1302_write_time(const ds_time *t); /* 突发写 7 字节，BCD（写前自动解写保护） */

#endif
