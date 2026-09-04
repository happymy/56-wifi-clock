#pragma once
/* 8266 本地持久化（Arduino EEPROM 模拟，flash 内 512B 足够）：
   WiFi STA 凭据 / AP 密码 / 13B 配置镜像备份 / 闹钟 3 组 / 倒计时预设。
   51 cfg(13B 基础字段)为权威，本镜像备份掉电后快速回显，上线仍以 REQ_CFG 拉取为准；
   闹钟为 8266 本地权威（决策⑨），本 store 直接持有，不随 13B 下发。 */
#include <Arduino.h>

void store_init();              /* EEPROM.begin + 载入 */
void store_load();              /* 填充全局（含 g_cfg 备份） */
bool store_has_cfg();           /* EEPROM MAGIC 有效 = 有可回显的备份（配过网/保存过配置） */
void store_save_wifi(const char *ssid, const char *pwd);
bool store_get_wifi(char *ssid, size_t ss, char *pwd, size_t ps);
void store_set_ap_pwd(const char *p);
const char *store_get_ap_pwd();
void store_save_cfg_blob();     /* 备份 g_cfg */
void store_save_cd(uint8_t preset_min, uint8_t preset_sec);
uint8_t store_get_cd_min();
uint8_t store_get_cd_sec();
void store_save_alarm(int n, uint8_t on, uint8_t hh_bcd, uint8_t mm_bcd); /* 闹钟 3 组, BCD 存 */
bool store_get_alarm(int n, uint8_t *hh_bcd, uint8_t *mm_bcd);           /* 返回 on, 读取 BCD */
