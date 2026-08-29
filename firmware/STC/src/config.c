#include "config.h"
#include "stc15.h"
#include "eeprom.h"

__xdata cfg_t cfg;
static __xdata unsigned char eb_scratch;   /* M11: 读 MAGIC 用 1 字节暂存 */

void cfg_load(void) {
    eeprom_read(CFG_EEP_BASE, &eb_scratch, 1);
    if (eb_scratch != CFG_MAGIC) return;    /* 无有效配置 → 保留默认 */
    eeprom_read(CFG_EEP_BASE + 1, (__xdata unsigned char *)&cfg, 54);
}

void cfg_save(void) {
    eeprom_erase(CFG_EEP_BASE);             /* 扇区0 整擦 */
    eb_scratch = CFG_MAGIC;
    eeprom_write(CFG_EEP_BASE, &eb_scratch, 1);
    eeprom_write(CFG_EEP_BASE + 1, (__xdata unsigned char *)&cfg, 54);
}

void cfg_defaults(void) {
    unsigned char i;
    cfg.display_mode = 1;      /* 自动轮显 */
    cfg.bright_mode = 0;       /* 自动(光敏) */
    cfg.bright_lvl  = 7;       /* 手动默认档(自动模式下不生效) */
    cfg.temp_offset = 0;
    for (i = 0; i < 3; i++) { cfg.alarm[i][0] = 0; cfg.alarm[i][1] = 0; cfg.alarm[i][2] = 0; }
    cfg.tz = 8;                /* UTC+8 */
    cfg.off_start[0] = 0xFF; cfg.off_start[1] = 0xFF;   /* 关屏禁用 */
    cfg.off_end[0]   = 0xFF; cfg.off_end[1]   = 0xFF;
    cfg.chime = 0;
    cfg.snooze = 5;
    cfg.led_en = 1;           /* 默认开, 保留联网亮灯行为 */
    cfg.smg1_mode = 0;         /* 默认 SMG1 显温度 */
    for (i = 0; i < 6; i++) {
        cfg.rem1[i] = 0; cfg.rem2[i] = 0; cfg.rem3[i] = 0; cfg.rem4[i] = 0; cfg.rem5[i] = 0;
    }
    cfg.cd_preset = 5;         /* 倒计时默认 5 分 */
    cfg.temp_unit = 0;         /* °C */
}
