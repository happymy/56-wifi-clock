#include "tm1639.h"

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

/* 显示顺序自测：初始 12345678，每位独立 +1 循环 1-8，每 2 秒进一位；
   8 位小数点一起 2Hz 闪烁。GRID3(LED4) 倒装管做 180° 段补偿。 */
void main(void) {
    unsigned char disp[8];
    unsigned char i, v, frame;
    unsigned int tick = 0;
    delay_ms(200);
    P1 &= ~0x04;                 /* 红灯亮：运行指示 */
    tm1639_init();
    while (1) {
        tick++;
        frame = (unsigned char)(tick >> 3);   /* 每 8 tick(2s) 进一位 */
        for (i = 0; i < 8; i++) {
            if (i == 4) v = ((5 + frame) % 8) + 1;       /* SMG1 右=GRID5 */
            else if (i == 5) v = ((4 + frame) % 8) + 1;  /* SMG1 左=GRID6 */
            else if (i == 6) v = ((7 + frame) % 8) + 1;  /* SMG2 右=GRID7 */
            else if (i == 7) v = ((6 + frame) % 8) + 1;  /* SMG2 左=GRID8 */
            else v = ((i + frame) % 8) + 1;              /* 大屏 GRID1-4 */
            disp[i] = seg_font[v];
            if (i == 2) disp[i] = seg_rotate180(disp[i]);
            if (tick & 1) disp[i] |= 0x80;     /* 小数点闪烁(2Hz) */
        }
        tm1639_write_display(disp);
        delay_ms(250);
    }
}
