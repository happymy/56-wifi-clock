#ifndef CONFIG_H
#define CONFIG_H

/* 54B 配置结构，字段布局严格对齐 串口通信协议.md §5（SET_CFG PAYLOAD）。
   51 本地仅改 bright_mode/bright_lvl、cd_preset 与时间(DS1302 非本结构)；
   其余字段由 8266 Web 经 SET_CFG 下推。EEPROM 持久化见 M7。 */
typedef struct {
    unsigned char display_mode;      /* 0=固定(仅时间) 1=自动轮显 */
    unsigned char bright_mode;       /* 0=自动(光敏) 1=手动 */
    unsigned char bright_lvl;        /* 手动亮度档 1–8 */
    signed   char temp_offset;       /* 有符号温度补偿 */
    unsigned char alarm[3][3];       /* [0/1/2][on(0/1)+时+分]，布局同 SET_CFG(连续9B) */
    unsigned char tz;                 /* 时区(有符号, UTC+8=+8) */
    unsigned char off_start[2];      /* 关屏起 时+分(0xFF=禁用) */
    unsigned char off_end[2];        /* 关屏止 时+分 */
    unsigned char chime;             /* 整点报时 0=关 1=开 2=半点也报 */
    unsigned char snooze;            /* 贪睡 0=关 5/10=分钟 */
    unsigned char dst;               /* 夏令时 0=无 1=欧美自动 */
    unsigned char smg1_mode;         /* SMG1 选显: 0=温度 1=日期(8266 配置, 不轮换) */
    unsigned char rem1[6];           /* 事项提醒①（§6 结构） */
    unsigned char rem2[6];
    unsigned char rem3[6];
    unsigned char rem4[6];
    unsigned char rem5[6];
    unsigned char cd_preset;         /* 倒计时预设(分 1–60, 默认5) */
    unsigned char temp_unit;         /* 0=°C 1=°F */
} cfg_t;   /* sizeof == 54 */

extern __xdata cfg_t cfg;
void cfg_defaults(void);   /* 填出厂默认（未从EEPROM载入时） */
void cfg_load(void);       /* M11: 若 EEPROM 有有效配置则覆盖默认 */
void cfg_save(void);       /* M11: 配置变化时落盘(扇区0) */

#define CFG_EEP_BASE 0x0000   /* 配置存 EEPROM 扇区0；首字节为 MAGIC */
#define CFG_MAGIC    0x56

#endif
