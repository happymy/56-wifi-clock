#pragma once
/* 倒计时权威在 8266（plan/串口通信协议.md §6.3）：
   tick 每秒推 DISP_OVERRIDE mode1[1,mm,ss]（十进制值字节），归零推 mode2 响铃（显示停留末帧，释放靠取消）；
   CD_CTRL: 0=暂停/恢复，1=取消（取消回 mode0 释放；IDLE 收到任意控制亦回 mode0 防 51 残留卡死）。 */

#include <Arduino.h>

void cd_set_preset(uint8_t min, uint8_t sec);
void cd_start();                 /* 按预设分钟启动 */
void cd_pause_resume();
void cd_cancel();                /* 回发 mode0 释放显示 */
bool cd_active();                /* 是否正在展示倒计时（含暂停） */
void cd_tick();                  /* 每 1s 由主循环调用 */