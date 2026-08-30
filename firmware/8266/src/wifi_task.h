#pragma once
/* 伪待机网络任务：RF 控制（forceSleep）、STA 连接、NTP 对时并推 SET_TIME(0x81)。
   风格口径见 firmware/8266/编程计划.md E3；configTime 签名核实自 core time.cpp：
   configTime(int timezone_sec, int daylightOffset_sec, srv1, srv2, srv3)。 */

#include <Arduino.h>

void wifi_setup();             /* setup() 调：读凭据，决定 伪待机 or 开 AP */
void wifi_loop();              /* loop() 每拍调：非阻塞调度 */
void wifi_force_sync();        /* 51 REQ_TIME → 立即触发对时 */
void wifi_ep_ap_mode();        /* 51 ENTER_AP → 清凭据后重启进 AP */
bool wifi_synced();            /* NTP 已同步 */
bool wifi_ap_active();         /* 当前开 AP 配网中 */
void wifi_touch();             /* Web 服务时刷新 STA 闲置计时（防配置中断网） */
int  wifi_tz_h();              /* 有效时区小时(有符号)，用于 SET_TIME tz 字节 */