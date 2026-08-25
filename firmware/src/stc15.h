#ifndef STC15_H
#define STC15_H

/* STC15W408AS 寄存器（与 Keil/SDCC 通用 sfr 语法） */
sfr P0 = 0x80;
sfr P1 = 0x90;
sfr P2 = 0xA0;
sfr P3 = 0xB0;
sfr P4 = 0xC0;
sfr PSW = 0xD0;
sfr ACC = 0xE0;
sfr SP = 0x81;
sfr PCON = 0x87;
sfr TCON = 0x88;
sfr TMOD = 0x89;
sfr TL0 = 0x8C;
sfr TH0 = 0x8D;
sfr TL1 = 0x8A;
sfr TH1 = 0x8B;
sfr IE = 0xA8;
sfr IP = 0xB8;

#endif
