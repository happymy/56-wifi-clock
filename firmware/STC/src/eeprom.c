#include "eeprom.h"

/* 配置固定存 EEPROM 扇区0(0x0000)，地址 <256，用 8 位计数避免 16 位运算。
   读/写合一：wr=0 读，wr=1 写。IAP 使能一次循环操作，结束禁用防误写。 */
static void iap_wait(void) { volatile unsigned char k; for (k = 0; k < 40; k++); }
static void eep_blk(unsigned char n, __xdata unsigned char *buf, unsigned char wr) {
    unsigned char i, a = 0;
    IAP_CONTR = 0x84; IAP_ADDRH = 0;
    for (i = 0; i < n; i++) {
        IAP_ADDRL = a;
        if (wr) IAP_DATA = buf[a];
        IAP_CMD = wr ? 2 : 1;
        IAP_TRIG = 0x5A; IAP_TRIG = 0xA5; iap_wait();
        if (!wr) buf[a] = IAP_DATA;
        a++;
    }
    IAP_CONTR = 0; IAP_CMD = 0;
}

void eeprom_read(unsigned int addr, __xdata unsigned char *buf, unsigned char n) {
    (void)addr; eep_blk(n, buf, 0);
}
void eeprom_write(unsigned int addr, __xdata unsigned char *buf, unsigned char n) {
    (void)addr; eep_blk(n, buf, 1);
}
void eeprom_erase(unsigned int addr) {
    IAP_CONTR = 0x84; IAP_ADDRH = 0; IAP_ADDRL = (unsigned char)addr;
    IAP_CMD = 3;
    IAP_TRIG = 0x5A; IAP_TRIG = 0xA5; iap_wait();
    IAP_CONTR = 0; IAP_CMD = 0;
}
