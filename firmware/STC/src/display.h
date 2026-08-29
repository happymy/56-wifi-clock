#ifndef DISPLAY_H
#define DISPLAY_H

#include "ds1302.h"

/* 显示内容模式（大屏 GRID1-4 显示的“主信息”）；SMG1(GRID5/6)/SMG2(GRID7/8) 随模式变化。
      DISP_TIME : 大屏 HH:MM，SMG2=秒 SS，SMG1 由 smg1_sel 选 温度°C(个位dp亮)/星期1-7（8266 cycle_flags 控制轮换；手动 UP 可切整屏模式）
      DISP_DATE : 大屏 MMDD（月左/日右镜像），SMG2=年份 YY，SMG1=星期 1-7（不补零）
      DISP_TEMP : 大屏=[符号][2位温度][C/F]，SMG1/SMG2 灭（NTC 无效兜底 25°C）

     ★ 铁律（见 AGENTS.md §7 / plan/原理图.md §4.3）：disp[0..7]=GRID1..GRID8。
       GRID3=disp[2] 物理倒装 → 段码必经 seg_rotate180；
       SMG1 左=GRID6=disp[5]=十位、右=GRID5=disp[4]=个位；SMG2 左=GRID8=disp[7]=十位、右=GRID7=disp[6]=个位。
     ★ SMG1 凡显示温度（DISP_TIME 且 smg1_sel=0），其个位(GRID5/disp[4])小数点恒亮，与是否轮换无关。*/
#define DISP_TIME 0
#define DISP_DATE 1
#define DISP_TEMP 2

/* temp_x10: 温度×10（有符号，单位 0.1°C，哨兵 -999=开路/短接），unit_c: 1=°C 0=°F（仅影响渲染标识）
     写入 disp[0..7]（对应 GRID1..GRID8）。GRID3 倒装由 seg_rotate180 处理。°F 仅改标识，由本函数内部换算。 */
void disp_render(unsigned char mode, const __xdata ds_time *t, int temp_x10,
                  unsigned char smg1_sel, __xdata unsigned char *disp);

#endif
