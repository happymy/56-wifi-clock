#include <8051.h>

/* 编译验证用 demo：Timer0 中断翻转 P1.0 LED。
   仅验证 SDCC 编译/中断/定时器 SFR 可用，时序未按 1T 内核校准。 */
__sbit __at (0x90) LED;   /* P1.0 */

void timer0_isr(void) __interrupt (1)
{
    static unsigned char tick = 0;
    TH0 = 0x4C;            /* 16 位重装（按 12T 估算，仅示意） */
    TL0 = 0x00;
    if (++tick >= 250) {   /* 约 250ms 翻转一次 */
        tick = 0;
        LED = !LED;
    }
}

void main(void)
{
    TMOD = 0x01;           /* T0 模式 1，16 位 */
    TH0 = 0x4C; TL0 = 0x00;
    TR0 = 1;               /* 启动 T0 */
    ET0 = 1;               /* 允许 T0 中断 */
    EA  = 1;               /* 开总中断 */
    while (1) { }          /* 空转，由中断翻转 LED */
}
