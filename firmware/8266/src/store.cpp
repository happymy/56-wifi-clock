#include "store.h"
#include "proto.h"
#include <EEPROM.h>

#define EEP_SIZE  512
#define MAGIC     0x5A

/* 布局：magic(1) wifi_ssid(32) wifi_pwd(64) ap_pwd(16) cfg_blob(54) cd_min(1) cd_sec(1) */
#define OFF_MAGIC    0
#define OFF_SSID     1
#define OFF_PWD      33
#define OFF_AP_PWD   97
#define OFF_CFG      113
#define OFF_CD_MIN   167
#define OFF_CD_SEC   168

static void putc(size_t o, uint8_t v) { EEPROM.write(o, v); }

void store_init() { EEPROM.begin(EEP_SIZE); }

void store_load() {
    if (EEPROM.read(OFF_MAGIC) != MAGIC) return;          /* 未初始化，用默认 */
    for (int i = 0; i < CFG_LEN; i++) g_cfg[i] = EEPROM.read(OFF_CFG + i);
    /* g_cfg_valid 仍为 false：上线 REQ_CFG 以 51 为准 */
}

bool store_has_cfg() { return EEPROM.read(OFF_MAGIC) == MAGIC; }

bool store_get_wifi(char *ssid, size_t ss, char *pwd, size_t ps) {
    size_t i;
    if (EEPROM.read(OFF_MAGIC) != MAGIC) return false;
    for (i = 0; i < ss - 1 && EEPROM.read(OFF_SSID + i); i++) ssid[i] = EEPROM.read(OFF_SSID + i);
    ssid[i] = 0;
    for (i = 0; i < ps - 1 && EEPROM.read(OFF_PWD + i); i++) pwd[i] = EEPROM.read(OFF_PWD + i);
    pwd[i] = 0;
    return ssid[0] != 0;
}

static void write_str(size_t off, size_t cap, const char *s) {
    size_t i = 0;
    for (; i < cap - 1 && s && s[i]; i++) putc(off + i, (uint8_t)s[i]);
    for (; i < cap; i++) putc(off + i, 0);
}

void store_save_wifi(const char *ssid, const char *pwd) {
    putc(OFF_MAGIC, MAGIC);
    write_str(OFF_SSID, 32, ssid);
    write_str(OFF_PWD, 64, pwd);
    EEPROM.commit();
}

void store_set_ap_pwd(const char *p) { write_str(OFF_AP_PWD, 16, p); EEPROM.commit(); }

const char *store_get_ap_pwd() {
    static char buf[17];
    size_t i;
    for (i = 0; i < 16 && EEPROM.read(OFF_AP_PWD + i); i++) buf[i] = EEPROM.read(OFF_AP_PWD + i);
    buf[i] = 0;
    return (buf[0] == 0xFF || buf[0] == 0) ? "12345678" : buf;   /* 0xFF=擦除态,0=空串 均回默认 */
}

void store_save_cfg_blob() { for (int i = 0; i < CFG_LEN; i++) putc(OFF_CFG + i, g_cfg[i]); EEPROM.commit(); }

void store_save_cd(uint8_t preset_min, uint8_t preset_sec) { putc(OFF_CD_MIN, preset_min ? preset_min : 5); putc(OFF_CD_SEC, preset_sec); EEPROM.commit(); }
uint8_t store_get_cd_min() { uint8_t v = EEPROM.read(OFF_CD_MIN); return (v >= 1 && v <= 99) ? v : 5; }
uint8_t store_get_cd_sec() { uint8_t v = EEPROM.read(OFF_CD_SEC); return (v <= 59) ? v : 0; }
