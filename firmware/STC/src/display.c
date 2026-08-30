#include "display.h"
#include "tm1639.h"
#include "stc15.h"
#include "config.h"   /* 直接读 cfg.temp_unit 做°F换算，省参数传递 */

/* 大屏(GRID1-4) HH:MM；SMG1(GRID5/6)：左=十位(disp[5]) 右=个位(disp[4])；
   SMG2(GRID7/8)：左=十位(disp[7]) 右=个位(disp[6])。GRID3(disp[2]) 倒装→seg_rotate180。
   DS1302 字段为 BCD：十位=高4位, 个位=低4位, 直取避免运算。 */

static void put_smg2(__xdata unsigned char *d, unsigned char hi, unsigned char lo) {
    d[7] = seg_font[hi]; d[6] = seg_font[lo];   /* 左=十位 右=个位 */
}

/* |v|/10（温度×10→整°C），×26>>8 近似免循环；夹断2位(护°F≥100°F时deg/10越界)。SMG1与整屏温度共用 */
static unsigned char abs_div10(int v) {
    unsigned int a = (v < 0) ? (unsigned int)(-v) : (unsigned int)v;
    unsigned char d = 0;
    while (a >= 10) { a -= 10; d++; }   /* |v|/10: 免 16 位乘/除库(deg/10、deg%10 为 8 位, SDCC 内联) */
    if (d > 99) d = 99;
    return d;
}

void disp_render(unsigned char mode, const __xdata ds_time *t, int temp_x10,
                    unsigned char smg1_sel, __xdata unsigned char *disp) {
    unsigned char i;
    for (i = 0; i < 8; i++) disp[i] = 0;   /* 整屏清, 防残留 */
    if (mode == DISP_DATE) {
        /* 大屏 MMDD: 月左两位, 日右两位(镜像) */
        disp[0] = seg_font[(t->month >> 4) & 0x0F];
        disp[1] = seg_font[t->month & 0x0F];
        disp[2] = seg_rotate180(seg_font[(t->date >> 4) & 0x0F]);
        disp[3] = seg_font[t->date & 0x0F];
        disp[7] = seg_font[(t->year >> 4) & 0x0F];   /* SMG2 = 年份 YY(2位) */
        disp[6] = seg_font[t->year & 0x0F];
        disp[5] = seg_font[t->weekday & 0x0F];        /* SMG1 = 星期 1-7, 不补零 */
        disp[4] = 0;
    } else if (mode == DISP_TEMP) {
        int v = temp_x10;
        if (v <= -990) v = 250;                 /* NTC 开路/短接: 兜底 25.0°C */
        if (cfg.temp_unit != 0) v = v + (v >> 1) + (v >> 2) + (v >> 4) + 320;  /* °C×10→°F×10, 仅移位近似 */
        unsigned char neg = (v < 0);
        unsigned char deg = abs_div10(v);       /* |v|/10 夹断 2 位 */
        disp[0] = neg ? 0x40 : 0;               /* 负号或空 */
        disp[1] = seg_font[deg / 10];           /* 十位 */
        disp[2] = seg_rotate180(seg_font[deg % 10]);   /* 个位 (GRID3 倒装, 须旋转) */
        disp[3] = (cfg.temp_unit != 0) ? 0x71 : 0x39;  /* 单位 F / C */
        /* SMG1/SMG2 灭: 整屏清屏已置 0 */
    } else { /* DISP_TIME: 大屏 HH:MM, 小时/分十位带小数点作冒号; SMG1 由 smg1_sel 选 温度/日期 */
        disp[0] = seg_font[(t->hr >> 4) & 0x0F];
        disp[1] = seg_font[t->hr & 0x0F] | 0x80;
        disp[2] = seg_rotate180(seg_font[(t->min >> 4) & 0x0F] | 0x80);  /* GRID3 倒装, dp 作冒号下点 */
        disp[3] = seg_font[t->min & 0x0F];
        put_smg2(disp, (t->sec >> 4) & 0x0F, (t->sec & 0x0F));
        if (smg1_sel == 0) {          /* SMG1=温度(整°C), 个位(GRID5)小数点亮 */
            unsigned char td = abs_div10((temp_x10 <= -990) ? 250 : temp_x10);  /* -999 哨兵兜底 25°C, 同 DISP_TEMP */
            disp[5] = seg_font[td / 10]; disp[4] = seg_font[td % 10] | 0x80;
        } else {                      /* SMG1=日期(日 DD, BCD 2位) */
            disp[5] = seg_font[(t->date >> 4) & 0x0F]; disp[4] = seg_font[t->date & 0x0F];
        }
    }
}
