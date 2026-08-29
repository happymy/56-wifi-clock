#include "display.h"
#include "tm1639.h"
#include "stc15.h"

/* 大屏(GRID1-4) HH:MM；SMG1(GRID5/6)：左=十位(disp[5]) 右=个位(disp[4])；
   SMG2(GRID7/8)：左=十位(disp[7]) 右=个位(disp[6])。GRID3(disp[2]) 倒装→seg_rotate180。
   DS1302 字段为 BCD：十位=高4位, 个位=低4位, 直取避免运算。 */

static void put_smg2(__xdata unsigned char *d, unsigned char hi, unsigned char lo) {
    d[7] = seg_font[hi]; d[6] = seg_font[lo];   /* 左=十位 右=个位 */
}

/* v = 温度×10(有符号)。ponytail: 免除法库——先循环除以10得整数度, 再 *26>>8 拆十位/个位(0-60 内精确) */
static void bin2(__xdata unsigned char *d, int v, unsigned char neg) {
    if (v <= -99) { d[0] = 0x40; d[1] = 0x40; return; }  /* NTC 开路哨兵(-999)等: 显 "--" 而非乱码 */
    int vv = (v < 0) ? -v : v;
    unsigned char deg = 0;
    while (vv >= 10) { vv -= 10; deg++; }
    unsigned char tens = (unsigned char)((((int)deg << 4) + ((int)deg << 3) + ((int)deg << 1)) >> 8); /* ÷10 */
    unsigned char ones = (unsigned char)(deg - ((int)tens << 3) - ((int)tens << 1));
    d[1] = seg_font[tens];          /* 左(disp[5])=十位 */
    d[0] = seg_font[ones];          /* 右(disp[4])=个位 */
    if (neg) {                       /* 负号: 2 位放不下"符号+十位", |v|>=10 显"--", 否则"-x" */
        if (tens > 0) { d[1] = 0x40; d[0] = 0x40; }
        else { d[1] = 0x40; }
    }
}

void disp_render(unsigned char mode, const __xdata ds_time *t, int temp_x10,
                   unsigned char unit_c, __xdata unsigned char *disp) {
    unsigned char i;
    for (i = 0; i < 8; i++) disp[i] = 0;   /* 模式切换前整体清屏, 避免残留段(如日期→温度) */
    if (mode == DISP_DATE) {
        /* 大屏 MMDD: 月左两位, 日右两位(镜像) */
        disp[0] = seg_font[(t->month >> 4) & 0x0F];
        disp[1] = seg_font[t->month & 0x0F];
        disp[2] = seg_rotate180(seg_font[(t->date >> 4) & 0x0F]);
        disp[3] = seg_font[t->date & 0x0F];
        /* SMG2 = 年份 YY(2位) */
        disp[7] = seg_font[(t->year >> 4) & 0x0F];
        disp[6] = seg_font[t->year & 0x0F];
        /* SMG1 = 星期 1-7, 不补零 */
        disp[5] = seg_font[t->weekday & 0x0F];
        disp[4] = 0;
    } else if (mode == DISP_TEMP) {
        int v = temp_x10;
        if (v <= -990) v = 250;                 /* NTC 开路/短接: 兜底 25.0°C */
        if (unit_c != 0) v = v + (v >> 1) + (v >> 2) + (v >> 4) + 320;  /* °C×10→°F×10, 仅移位近似 */
        unsigned char neg = (v < 0);
        unsigned int av = (unsigned int)(neg ? -v : v);          /* |v| */
        unsigned char deg = (unsigned char)((av * 26) >> 8);  /* |v|/10 ≈ ×26>>8, 免循环 */
        if (deg > 99) deg = 99;                  /* 2 位幅度夹断 */
        disp[0] = neg ? 0x40 : 0;               /* 负号或空 */
        disp[1] = seg_font[deg / 10];           /* 十位 */
        disp[2] = seg_rotate180(seg_font[deg % 10]);   /* 个位 (GRID3 倒装, 须旋转) */
        disp[3] = (unit_c != 0) ? 0x31 : 0x39;  /* 单位 F / C */
        /* SMG1/SMG2 灭: disp_render 开头整体清屏已置 0 */
    } else { /* DISP_TIME: 大屏 HH:MM, 小时个位带小数点作冒号 */
        disp[2] = 0;                 /* DISP_TEMP 分支不清 GRID3，这里复位避免残留段 */
        disp[0] = seg_font[(t->hr >> 4) & 0x0F];
        disp[1] = seg_font[t->hr & 0x0F] | 0x80;
        disp[2] = seg_rotate180(seg_font[(t->min >> 4) & 0x0F]);
        disp[3] = seg_font[t->min & 0x0F];
        put_smg2(disp, (t->sec >> 4) & 0x0F, (t->sec & 0x0F));
        bin2(disp + 4, temp_x10, (temp_x10 < 0) ? 1 : 0);  /* SMG1=温度 */
    }
}
