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

/* 端口模式寄存器：PxM1.n,PxM0.n = 00 准双向 / 01 推挽 / 10 高阻 / 11 开漏 */
__SFR(P1M1, 0x91);
__SFR(P1M0, 0x92);
__SFR(P2M1, 0x95);
__SFR(P2M0, 0x96);
__SFR(P3M1, 0xB1);
__SFR(P3M0, 0xB2);

/* ADC（STC15W408AS，10 位） */
__SFR(P1ASF, 0x9D);    /* P1 模拟功能选择（只写） */
__SFR(ADC_CONTR, 0xBC);
__SFR(ADC_RES, 0xBD);  /* 结果高 8 位（ADRJ=0 默认） */
__SFR(ADC_RESL, 0xBE); /* 结果低 2 位 */

/* ADC_CONTR 控制位 */
#define ADC_POWER   0x80   /* ADC 上电 */
#define ADC_FLAG    0x10   /* 转换完成标志 */
#define ADC_START   0x08   /* 启动转换 */
#define ADC_SPEEDLL 0x00   /* 540 时钟/次 */
#define ADC_SPEEDL  0x20   /* 360 */
#define ADC_SPEEDH  0x40   /* 180 */
#define ADC_SPEEDHH 0x60   /* 90 */

#endif
