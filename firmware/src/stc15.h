#ifndef STC15_H
#define STC15_H

/* STC15W408AS 寄存器定义。
   SDCC 用 __sfr __at(addr)，Keil C51 用 sfr addr；用宏统一。
   仅放地址确定的标准 8051 寄存器；STC15 扩展 SFR 按模块追加并校订地址。 */
#if defined(__SDCC)
  #define __SFR(name, addr)  __sfr  __at(addr) name
#else /* Keil C51 */
  #define __SFR(name, addr)  sfr  name = addr
#endif

__SFR(P0, 0x80);
__SFR(P1, 0x90);
__SFR(P2, 0xA0);
__SFR(P3, 0xB0);

#endif
