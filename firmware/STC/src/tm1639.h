#ifndef TM1639_H
#define TM1639_H

#include "stc15.h"

/* TM1639 三线接口：STB=P2.3, CLK=P2.4, DIO=P2.5
   显示模式：8 段 × 8 位（共阴）
   段序：SEG1=a, SEG2=b, SEG3=c, SEG4=d, SEG5=e, SEG6=f, SEG7=g, SEG8=dp
   数据字节位序：bit0=SEG1(a) ... bit7=SEG8(dp) */
#define TM_STB_MASK (1u << 3)
#define TM_CLK_MASK (1u << 4)
#define TM_DIO_MASK (1u << 5)

void tm1639_init(void);
void tm1639_write_display(__xdata unsigned char *data);
void tm1639_set_brightness(unsigned char level); /* 0..7 硬件占空比 */
void tm1639_set_light(unsigned int light); /* 自动亮度: 光敏 0..1023 → 硬件档 0..7 */

extern const unsigned char seg_font[16]; /* 共阴 0-9, A-F */
#define SEG_P 0x73   /* 'P' 字形(a,b,e,f,g)，配网/IP 提示用 */
#define SEG_L 0x18   /* 'L' 字形(d,e)，亮度档位指示 */
unsigned char seg_rotate180(unsigned char d); /* 倒装管段码旋转 180° */

#endif
