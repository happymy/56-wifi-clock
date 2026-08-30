#pragma once
/* 两页 Web：AP 配网页(仅 WiFi 账号) + STA 全功能配置页。全 PROGMEM/F()，无外部库。
   提交后改镜像字节 → 整帧 SET_CFG 下推 51（SET_CFG 完整性铁律）。 */

#include <Arduino.h>

void web_setup(bool ap_mode);   /* 参数保留签名兼容，实际按运行时 wifi_ap_active() 分流 */
void web_loop();                /* server.handleClient() */