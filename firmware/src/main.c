#include "tm1639.h"

void main(void) {
    unsigned char disp[8];
    unsigned char i;
    tm1639_init();
    /* 点亮测试：8 位依次显示 1..8，便于核对段码与位序
       GRID1=LED2(1) GRID2=LED3(2) GRID3=LED4(3) GRID4=LED5(4)
       GRID5=SMG1右(5) GRID6=SMG1左(6) GRID7=SMG2右(7) GRID8=SMG2左(8) */
    for (i = 0; i < 8; i++) disp[i] = seg_font[(i + 1) % 16];
    tm1639_write_display(disp);
    while (1) {
        /* 初始固件：静态显示，等待后续接入 RTC/按键/WiFi */
    }
}
