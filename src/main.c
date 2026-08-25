#include <8051.h>

/* STC15W408AS 内核是 8051，P1 端口地址 0x90，
   上电默认为准双向口，可直接灌电流驱动 LED（低电平点亮）。
   具体 LED 引脚请按你的板子原理图修改。 */
__sbit __at (0x90) LED;   /* P1.0 */

/* 软件延时，粗略按内部 IRC（约 24MHz）估算。
   实际频率由芯片 Option 字节决定，闪烁快慢请调 j 的上限。 */
void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 600; j++)
            ;
}

void main(void)
{
    while (1) {
        LED = 0; delay_ms(400);   /* 亮 */
        LED = 1; delay_ms(400);   /* 灭 */
    }
}
