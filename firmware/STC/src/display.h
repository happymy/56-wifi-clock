#ifndef DISPLAY_H
#define DISPLAY_H

#include "ds1302.h"

/* 显示内容模式（大屏 GRID1-4 显示的“主信息”）；SMG1(GRID5/6)/SMG2(GRID7/8) 随模式变化。
   DISP_TIME : 大屏 HH:MM，SMG2=秒 SS，SMG1=温度 °C（带 dp）
   DISP_DATE : 大屏 MMDD（月左/日右镜像），SMG2=年份 YY，SMG1=星期 1-7（不补零）
   DISP_TEMP : 大屏=[符号][2位温度][C/F]，SMG1/SMG2 灭（NTC 无效兜底 25°C） */
#define DISP_TIME 0
#define DISP_DATE 1
#define DISP_TEMP 2

/* temp_x10: 温度×10（有符号，单位 0.1°C），unit_c: 1=°C 0=°F（仅影响渲染标识）
   写入 disp[0..7]（对应 GRID1..GRID8）。GRID3 倒装由 seg_rotate180 处理。 */
void disp_render(unsigned char mode, const __xdata ds_time *t, int temp_x10,
                 unsigned char unit_c, __xdata unsigned char *disp);

#endif
