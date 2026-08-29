#ifndef EEPROM_H
#define EEPROM_H

#include "stc15.h"

/* STC15 片内 Data Flash(EEPROM) 5KB@0x0000-0x13FF，扇区 512B。
   官方序列：IAP_CONTR=0x84(IAPEN+WT) 使能；IAP_CMD 1读/2写/3擦；
   触发 0x5A→0xA5；地址每次重设；结束置 IAP_CONTR=0 防误写。
   配置 M11 用扇区0(0x0000)，dbg_log 等后续用其它扇区避免冲突。 */

void eeprom_read(unsigned int addr, __xdata unsigned char *buf, unsigned char n);
void eeprom_write(unsigned int addr, __xdata unsigned char *buf, unsigned char n);
void eeprom_erase(unsigned int addr);   /* 擦 addr 所在 512B 扇区 */

#endif
