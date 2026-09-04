#ifndef CONFIG_H
#define CONFIG_H

/* 13B 配置结构，字段布局严格对齐 串口通信协议.md §5（SET_CFG PAYLOAD）。
   响铃上移 8266 重构后：闹钟/提醒/报时/倒计时预设迁 8266 store 本地存储，51 不再承载。
   51 本地仅改 bright_mode/bright_lvl 与时间(DS1302 非本结构)；其余由 8266 Web 经 SET_CFG 下推。
   CFG_MAGIC 独立存 EEPROM 首字节判别新旧，不进本结构、不进 UART 帧（SET_CFG 帧=&cfg）。
   EEPROM 持久化见 M7；cfg_load 读 CFG_MAGIC 有效则载 13B，否则保持默认；旧 54B 升级由 8266 上线 REQ_CFG+SET_CFG 整帧覆盖兜底，不判版本。 */
typedef struct {
    unsigned char display_mode;      /* [0] 0=不自动轮显 1=自动轮显(8266 控制, 每分钟整分轮换整屏主模式) */
    unsigned char bright_mode;       /* [1] 0=自动(光敏) 1=手动 */
    unsigned char bright_lvl;        /* [2] 手动亮度档 1–8 */
    signed   char temp_offset;       /* [3] 有符号温度补偿 */
    unsigned char tz;                /* [4] 时区(有符号, UTC+8=+8) */
    unsigned char off_start[2];      /* [5,6] 关屏起 时+分(0xFF=禁用) */
    unsigned char off_end[2];        /* [7,8] 关屏止 时+分 */
    unsigned char snooze;            /* [9] 贪睡 0=关 5/10=分钟 */
    unsigned char led_en;            /* [10] 红灯指示使能: 1=开(默认) 0=关 */
    unsigned char smg1_mode;         /* [11] SMG1 选显: 0=温度 1=日期(8266 配置, 不轮换) */
    unsigned char temp_unit;         /* [12] 0=°C 1=°F */
} cfg_t;   /* sizeof == 13（EEPROM 首字节另存 CFG_MAGIC，SET_CFG/REQ_CFG 帧载荷 = 13B） */

extern __xdata cfg_t cfg;
void cfg_defaults(void);   /* 填出厂默认（未从EEPROM载入时） */
void cfg_load(void);       /* M11: 若 EEPROM 有有效配置则覆盖默认 */
void cfg_save(void);       /* M11: 配置变化时落盘(扇区0) */

#define CFG_EEP_BASE 0x0000   /* 配置存 EEPROM 扇区0；首字节为 MAGIC */
#define CFG_MAGIC    0x56

#endif
