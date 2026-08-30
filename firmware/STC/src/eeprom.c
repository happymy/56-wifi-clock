#include "eeprom.h"

/* 配置固定存 EEPROM 扇区0(0x0000)，首字节 MAGIC(0x56)，cfg 紧随其后(0x0001+)。
   读/写合一：wr=0 读，wr=1 写。IAP 使能一次循环操作，结束禁用防误写。
   ponytail: 地址 = addr + i（16 位），IAP_ADDRH 取高 5 位、ADDRL 取低 8 位；偏移 0x0001 的 cfg 区由此落位，
   不再忽略 addr 导致 MAGIC 被覆盖、cfg_load 永远早退(配置无法过电保存)。 */
static void iap_wait(void) { volatile unsigned char k; for (k = 0; k < 40; k++); }
static void eep_blk(unsigned int addr, unsigned char n, __xdata unsigned char *buf, unsigned char wr) {
    unsigned char i;
    unsigned int a;
    IAP_CONTR = 0x84;
    for (i = 0; i < n; i++) {
        a = addr + i;
        IAP_ADDRH = (a >> 8) & 0x3F;
        IAP_ADDRL = a & 0xFF;
        if (wr) IAP_DATA = buf[i];
        IAP_CMD = wr ? 2 : 1;
        IAP_TRIG = 0x5A; IAP_TRIG = 0xA5; iap_wait();
        if (!wr) buf[i] = IAP_DATA;
    }
    IAP_CONTR = 0; IAP_CMD = 0;
}

void eeprom_read(unsigned int addr, __xdata unsigned char *buf, unsigned char n) {
    eep_blk(addr, n, buf, 0);
}
void eeprom_write(unsigned int addr, __xdata unsigned char *buf, unsigned char n) {
    eep_blk(addr, n, buf, 1);
}
void eeprom_erase(unsigned int addr) {
    IAP_CONTR = 0x84; IAP_ADDRH = (addr >> 8) & 0x3F; IAP_ADDRL = addr & 0xFF;
    IAP_CMD = 3;
    IAP_TRIG = 0x5A; IAP_TRIG = 0xA5; iap_wait();
    IAP_CONTR = 0; IAP_CMD = 0;
}
