#include "tm1639.h"
#include "ds1302.h"
#include "display.h"
#include "config.h"
#include "keys.h"
#include "stc15.h"

/* BEEP(P2.1) 经 S9012 PNP：拉低=响，拉高=静音(active-low) */
#define BEEP_ON()   do { P2 &= ~0x02; } while (0)
#define BEEP_OFF()  do { P2 |= 0x02; } while (0)
/* LED(P1.2) 状态指示：亮=已联网同步（由 NET_STATUS 帧驱动，M10 赋值） */
#define LED_ON()    do { P1 &= ~0x04; } while (0)   /* active-low: P1.2 拉低亮 */
#define LED_OFF()   do { P1 |= 0x04; } while (0)

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

static void beep_once(void) { BEEP_ON(); delay_ms(150); BEEP_OFF(); delay_ms(120); }

/* 10 位 ADC 查询读：ch0=P1.0 光敏, ch1=P1.1 热敏 */
static unsigned int adc_read(unsigned char ch) {
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL | (ch & 0x07) | ADC_START;
    while (!(ADC_CONTR & ADC_FLAG));
    ADC_CONTR &= ~ADC_FLAG;
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}

/* ---- 手动时间设置辅助（沿用 demo/ds1302-clock 验证逻辑）---- */
/* 免除法库: 减10循环拆 BCD 十位/个位 */
static void u2bcd(__data unsigned char *out, unsigned char v) {
    unsigned char q = 0;
    while (v >= 10) { v -= 10; q++; }
    out[0] = q; out[1] = v;
}
/* 秒→MM:SS 内联于 cd 运行显示(仅一处调用, 省函数开销) */
static unsigned char bcd2bin(unsigned char b) {
    unsigned char hi = (b >> 4) & 0x0F, lo = b & 0x0F;
    return (unsigned char)(hi * 8 + hi * 2 + lo);   /* 免 __mulint(29B): hi*10+lo */
}
static void inc_bcd(__xdata unsigned char *f, unsigned char max_dec) {
    int v = bcd2bin(*f);
    if (++v > max_dec) v = 0;
    { unsigned char b[2]; u2bcd(b, (unsigned char)v); *f = (b[0] << 4) | b[1]; }
}
static void inc_date(__xdata ds_time *t) {
    unsigned char m = bcd2bin(t->month);
    unsigned char max;
    if (m == 2) {                                  /* 闰年2月29(2000-2099: 年%4) */
        unsigned char s = (unsigned char)((t->year >> 4) + (t->year >> 4) + (t->year & 0x0F));
        max = ((s & 3) == 0) ? 29 : 28;
    } else {
        max = (m <= 7) ? ((m & 1) ? 31 : 30) : ((m & 1) ? 30 : 31);
    }
    unsigned char d = bcd2bin(t->date);
    if (++d > max) d = 1;
    { unsigned char b[2]; u2bcd(b, d); t->date = (b[0] << 4) | b[1]; }
}

static void apply_bright(unsigned int light) {
    if (cfg.bright_mode == 0) tm1639_set_light(light);
    else tm1639_set_brightness((unsigned char)(cfg.bright_lvl - 1));
}

/* 亮度调节：大屏显 "Lnn"（L=亮度档位指示，nn=0-8） */
static void render_bright_adj(__xdata unsigned char *disp, unsigned char val, unsigned char blank) {
    unsigned char i, b[2];
    for (i = 0; i < 8; i++) disp[i] = 0;
    if (!blank) {
        u2bcd(b, val);
        disp[0] = SEG_L;
        disp[1] = seg_font[b[0]];
        disp[2] = seg_rotate180(seg_font[b[1]]);   /* GRID3 倒装 */
    }
}

/* 时间设置：字段 0时1分2秒3年4月5日，blink 闪烁当前字段 */
static void render_setting(__xdata unsigned char *disp, const __xdata ds_time *t, unsigned char idx, unsigned char blank) {
    unsigned char H  = bcd2bin(t->hr);
    unsigned char M  = bcd2bin(t->min);
    unsigned char S  = bcd2bin(t->sec);
    unsigned char D  = bcd2bin(t->date);
    unsigned char MO = bcd2bin(t->month);
    unsigned char Y  = bcd2bin(t->year);
    unsigned char b[2], s1t, s1o, s2t, s2o;
    u2bcd(b, D); s1t = b[0]; s1o = b[1];            /* SMG1 默认:日 */
    u2bcd(b, S); s2t = b[0]; s2o = b[1];            /* SMG2 默认:秒 */
    if (idx == 4) { u2bcd(b, MO); s1t = b[0]; s1o = b[1]; }  /* 月 */
    if (idx == 3) { u2bcd(b, Y);  s2t = b[0]; s2o = b[1]; }  /* 年 */

    u2bcd(b, H);
    disp[0] = (idx == 0 && blank) ? 0x00 : seg_font[b[0]];
    disp[1] = (idx == 0 && blank) ? 0x00 : seg_font[b[1]];
    u2bcd(b, M);
    disp[2] = seg_rotate180((idx == 1 && blank) ? 0x00 : seg_font[b[0]]);
    disp[3] = (idx == 1 && blank) ? 0x00 : seg_font[b[1]];
    disp[5] = ((idx == 3 || idx == 4) && blank) ? 0x00 : seg_font[s1t];  /* SMG1 左=十位 */
    disp[4] = ((idx == 3 || idx == 4) && blank) ? 0x00 : seg_font[s1o];  /* SMG1 右=个位 */
    disp[7] = ((idx == 2 || idx == 3) && blank) ? 0x00 : seg_font[s2t];  /* SMG2 左=十位 */
    disp[6] = ((idx == 2 || idx == 3) && blank) ? 0x00 : seg_font[s2o];  /* SMG2 右=个位 */
}

/* 大屏显示 MM:SS（倒计时/计时器共用） */
static void put_mmss(__xdata unsigned char *disp, unsigned char mm, unsigned char ss) {
    unsigned char b[2]; u2bcd(b, mm);
    disp[0] = seg_font[b[0]]; disp[1] = seg_font[b[1]] | 0x80;   /* GRID2 dp = 冒号上点 */
    u2bcd(b, ss);
    disp[2] = seg_rotate180(seg_font[b[0]] | 0x80); disp[3] = seg_font[b[1]];  /* GRID3 倒装, dp = 冒号下点 */
    disp[4] = disp[5] = disp[6] = disp[7] = 0;
}

/* NTC 原始值 → 温度×10（有符号）。ponytail: 标称 10K@25°C/B=4050(MF11-103) + 10K 上拉（原理图确认），
    线性近似仅占位；真实曲线未烧录验证，单点标定靠 cfg.temp_offset（Web 下发，用户给参考温度）。
    25°C 时 NTC≈10K=上拉 → raw≈512 为模型零点；偏离室温越远线性误差越大(0/50°C 约±3°C)。 */
static int ntc_temp_x10(unsigned int raw) {
    if (raw == 0 || raw >= 1023) return -999;       /* 开路/短接 */
    int d = (int)raw - 512;                         /* 25°C≈raw512(10K NTC+10K 上拉) */
    int p = (d << 5) - ((d << 1) + d);              /* d*29, 免 __mulint */
    int off = (int)cfg.temp_offset;
    return (250 - (p >> 5)) + ((off << 3) + (off << 1));  /* temp×10: 250=25.0°C */
}

/* ============ M10: 51 ↔ ESP8266 串口协议（9600 8N1, Timer2 波特源） ============ */
#define CMD_REQ_TIME  0x01
#define CMD_HEARTBEAT 0x02
#define CMD_ENTER_AP  0x04
#define CMD_SET_TIME  0x81
#define CMD_SET_CFG   0x82
#define CMD_NET_STAT  0x83
#define CMD_REQ_CFG   0x87
#define CMD_STA_IP    0x88
#define CMD_BOOT      0x8F
#define CMD_CD_CTRL        0x05   /* MCU→ESP: 倒计时控制(0=暂停/恢复,1=取消) */
#define CMD_DISP_OVERRIDE  0x89   /* ESP→MCU: 覆盖显示(倒计时/响铃) */

static void uart_init(void) {
    SCON = 0x50;                 /* 8N1, REN=1（复用 demo/ds1302-clock 验证配置） */
    T2L = 0xE0; T2H = 0xFE;
    AUXR = 0x14;                 /* Timer2 1T + 启动 */
    AUXR |= 0x01;                /* S1BRS=1：Timer2 作 UART1 波特源 */
    ES = 1; EA = 1;              /* 开串口中断（仅收） */
}
static void uart_send(unsigned char c) {
    ES = 0;                       /* 关收中断：RI/TI 共用向量，ISR 会清 TI，否则下方忙等与 ISR 死锁 */
    SBUF = c;
    while (!(SCON & 0x02));       /* 等 TI */
    SCON &= ~0x02;
    ES = 1;                       /* 恢复收中断 */
}
static void uart_send_frame(unsigned char cmd, __xdata unsigned char *p, unsigned char len) {
    unsigned char i, chk = cmd ^ len;
    uart_send(0xAA); uart_send(0x55); uart_send(cmd); uart_send(len);
    for (i = 0; i < len; i++) { uart_send(p[i]); chk ^= p[i]; }
    uart_send(chk);
}
/* 空载荷帧: 传 0 指针（循环 0 次），避免通用指针开销 */
#define uart_send_null(cmd) uart_send_frame(cmd, (__xdata unsigned char *)0, 0)

/* 单字节载荷帧: 复用专用缓冲, 避免栈指针(通用指针库) */
static __xdata unsigned char tx1[1];
static void uart_send1(unsigned char c, unsigned char v) { tx1[0] = v; uart_send_frame(c, tx1, 1); }

#define URX_LEN 32   /* XRAM仅256B, 环不能大; 改由高频轮询(uart_poll放内层10ms循环)防59B SET_CFG帧溢出丢头 */
static __xdata unsigned char urx[URX_LEN];
static volatile __xdata unsigned char urx_w = 0, urx_r = 0;
void uart_isr(void) __interrupt(4) {
    if (SCON & 0x01) {            /* RI */
        urx[urx_w] = SBUF; urx_w = (urx_w + 1) & (URX_LEN - 1);
        SCON &= ~0x01;
    } else SCON &= ~0x02;        /* 清 TI（发送用阻塞，一般不会进） */
}

static __xdata unsigned char sta_ip_last = 0;   /* 配网 IP 末段(同网段前三段固定，仅显末段) */
static __xdata ds_time tscr;            /* apply_set_time 暂存, 避开栈 overlay */
static unsigned char esp_online = 0;
volatile unsigned char net_status = 0;   /* 网络状态(0未连..3已同步), UART 赋值 */
unsigned char clock_ok = 1;               /* 1=DS1302 走时有效(CH=0) */

/* 8266 倒计时显示接管：DISP_OVERRIDE 帧写入, main 渲染/按键读取 */
static __xdata unsigned char cd_disp = 0;          /* 1=大屏显示 8266 倒计时 */
static __xdata unsigned char cd_mm = 0, cd_ss = 0; /* 8266 推来的剩余 MM:SS */
static __xdata unsigned char ring_alarm;            /* 0=静音, 1..3 闹钟响铃, 4=倒计时归零响铃(启动清XISEG) */
static __xdata unsigned int  ring_ticks;             /* 当前响铃剩余 250ms 拍 */

static void apply_set_time(__xdata unsigned char *p) {
    tscr.year = p[0]; tscr.month = p[1]; tscr.date = p[2]; tscr.weekday = p[3];
    tscr.hr = p[4]; tscr.min = p[5]; tscr.sec = p[6] & 0x7F;   /* 清 CH 位 */
    ds1302_write_time(&tscr);
    clock_ok = 1;
    cfg.tz = p[7];
}
static void apply_set_cfg(__xdata unsigned char *p) {
    __xdata unsigned char *d = (__xdata unsigned char *)&cfg;
    unsigned char i;
    for (i = 0; i < 54; i++) d[i] = p[i];   /* __xdata↔__xdata, 避开通用指针库 */
    cfg_save();                              /* M11: 落盘(扇区0) */
}
static void uart_dispatch(unsigned char cmd, __xdata unsigned char *p, unsigned char len) {
    switch (cmd) {
        case CMD_BOOT:     esp_online = 1; break;   /* 握手: 此后才接受 8266 下行帧 */
        case CMD_SET_TIME: if (esp_online && len >= 8) apply_set_time(p); break;
        case CMD_SET_CFG:  if (esp_online && len >= 54) apply_set_cfg(p); break;
        case CMD_NET_STAT: if (esp_online && len >= 1) net_status = p[0]; break;
        case CMD_STA_IP:   if (esp_online && len >= 4) sta_ip_last = p[3]; break;
        case CMD_REQ_CFG:  if (esp_online) uart_send_frame(CMD_SET_CFG, (__xdata unsigned char *)&cfg, 54); break;
        case CMD_DISP_OVERRIDE:  /* 8266 倒计时显示接管 */
            if (esp_online && len >= 1) {
                if (p[0] == 0) cd_disp = 0;
                else if (p[0] == 1 && len >= 3) { cd_disp = 1; cd_mm = p[1]; cd_ss = p[2]; }
                else if (p[0] == 2) { ring_alarm = 4; ring_ticks = 240; }  /* 倒计时归零响铃: 复用闹钟机制(可SET停/UP贪睡), 非阻塞 */
            }
            break;
        default: break;
    }
}
static unsigned char p_st = 0, p_cmd = 0, p_len = 0, p_idx = 0, p_chk = 0;
static __xdata unsigned char p_buf[64];

static void uart_poll(void) {
    while (urx_r != urx_w) {
        unsigned char b = urx[urx_r]; urx_r = (urx_r + 1) & (URX_LEN - 1);        switch (p_st) {
            case 0: if (b == 0xAA) p_st = 1; break;
            case 1: p_st = (b == 0x55) ? 2 : 0; break;
            case 2: p_cmd = b; p_chk = b; p_st = 3; break;
            case 3: p_len = b; p_chk ^= b; p_idx = 0;
                    if (p_len == 0) p_st = 5;
                    else if (p_len > 60) p_st = 0;   /* 超缓冲/非法帧:丢弃,防 p_buf 越界踩内存挂死 */
                    else p_st = 4;
                    break;
            case 4: p_buf[p_idx++] = b; p_chk ^= b; if (p_idx >= p_len) p_st = 5; break;
            case 5: if (b == p_chk) uart_dispatch(p_cmd, p_buf, p_len); p_st = 0; break;
        }
    }
}

void main(void) {
    __xdata unsigned char disp[8];
    __xdata ds_time t, t_set;
    __xdata key_ev_t ke;
    __xdata unsigned char mode = DISP_TIME, blink = 0, smg1_rot = 0;
    /* mode: 整屏主模式(手动UP切); smg1_rot: 走时SMG1选显 0=温度 1=日期(由 cfg.smg1_mode, 不轮换) */
    __xdata unsigned char bright_adj = 0, adj_val = 0;
    __xdata unsigned char tset_mode = 0, tset_idx = 0;
    __xdata unsigned char last_min = 0xFF;
    __xdata unsigned char wake_ticks = 0;       /* 关屏时段内点按唤醒剩余秒(0=不唤醒) */
    __xdata unsigned int  snooze_ticks = 0;     /* 贪睡倒计时(拍) */
    __xdata unsigned char snooze_idx = 0;       /* 贪睡对应的闹钟索引(1..3, 0=无) */
    __xdata unsigned char tm = 0;               /* 计时器状态: 0关 1暂停 2运行 */
        __xdata unsigned char tm_sec = 0, tm_min = 0, tm_last = 0; /* 计时 MM:SS + DS1302秒基准 */
    __xdata unsigned char sec_last = 0xFF;     /* 每秒基准: 用于唤醒倒计时 */

    __xdata unsigned char ip_disp = 0;           /* 显示配网 IP 末段 ~3s */
    __xdata unsigned int ip_ticks = 0;
    __xdata unsigned char hb_tick = 0, boot_t = 20, ap_sent = 0;   /* 握手窗口≈5s(20拍×250ms): 8266 启动常>2s, 2026实测放宽; 无8266时进调试模式延至~5s(可接受) */
    __xdata unsigned char both_cnt = 0;          /* 双键同按计时(外循环拍, 满21≈5s) */
    __xdata unsigned int light;
    __xdata int temp_x10;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽(安全) */
    BEEP_OFF();
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.2/3.3 准双向 */
    /* 安全红线: P3.0/P3.1 保持准双向, 绝不置强推挽 */
    P1 |= 0x04;                  /* LED_T 红：初始灭(active-low) */

    P1ASF |= 0x03;               /* P1.0 光敏 / P1.1 热敏 模拟输入 */
    P1M1 |= 0x03; P1M0 &= ~0x03; /* 高阻 */
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL;
    delay_ms(1);

    delay_ms(200);
    tm1639_init();
    cfg_defaults();
    cfg_load();                             /* M11: EEPROM 覆盖默认(若有) */

    delay_ms(500);
    ds1302_init();

    keys_init();
    uart_init();

    /* 上电判定 DS1302 走时：CH 位(秒寄存器 bit7)=1 表示停振/未校时 */
    ds1302_read_time(&t);
    if (t.sec & 0x80) clock_ok = 0;

    while (1) {
        unsigned char k;
        for (k = 0; k < 24; k++) {
            keys_scan();
            uart_poll();   /* 高频轮询: 10ms级, 两轮间<12字节, 32B环不溢出, SET_CFG(59B)分段收全 */
            while (key_get(&ke)) {
                wake_ticks = 10;   /* 关屏时段内任意键唤醒10s(非关屏期无副作用) */
                /* 仅"正在响铃"拦截按键: SET=停响+关所有贪睡倒计时(滴两声确认), UP=贪睡; 贪睡倒计时期间不拦截, 按键走正常功能 */
                if (ring_alarm) {
                    if (ke.btn == KEY_SET) {
                        ring_alarm = 0; ring_ticks = 0; snooze_idx = 0; snooze_ticks = 0; BEEP_OFF();
                        beep_once(); beep_once();          /* ponytail: 取消确认音 */
                    } else { /* KEY_UP = 贪睡 */
                        snooze_idx = ring_alarm; snooze_ticks = ((unsigned int)cfg.snooze << 8) - ((unsigned int)cfg.snooze << 4); /* *240 免__mulint */
                        ring_alarm = 0; ring_ticks = 0; BEEP_OFF();
                    }
                    continue;
                }
                /* 8266 倒计时接管大屏: 设备键路由到倒计时控制(P2) */
                if (cd_disp) {
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) uart_send1(CMD_CD_CTRL, 0);   /* 单击=暂停/恢复 */
                    else if (ke.btn == KEY_SET && ke.ev == EV_LONG) uart_send1(CMD_CD_CTRL, 1); /* 长按=取消 */
                    continue;
                }
                /* 计时器 长按UP 进/出(非设置态) */
                if (!tset_mode && !bright_adj && ke.btn == KEY_UP && ke.ev == EV_LONG) {
                    tm = (tm) ? 0 : 1; tm_sec = 0; tm_min = 0; tm_last = 0;
                    continue;
                }
                if (tm) {
                    if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) { tm = (tm == 1) ? 2 : 1; if (tm == 2) tm_last = t.sec; }  /* 单击=起/停; 起动捕获当前秒→首秒不快 */
                    else if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) { tm = 1; tm_sec = 0; tm_min = 0; tm_last = 0; } /* 单击SET=复位 */
                    continue;
                }
                if (bright_adj) {
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        bright_adj = 0;
                        cfg.bright_mode = (adj_val == 0) ? 0 : 1;
                        if (adj_val) cfg.bright_lvl = adj_val;
                        cfg_save();   /* 按键调亮度落盘: 断电/8266重拉配置不丢 */
                    } else if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        adj_val = (adj_val >= 8) ? 0 : adj_val + 1;   /* 亮度0-8循环, 免%9 */
                        cfg.bright_mode = (adj_val == 0) ? 0 : 1;     /* 实时改cfg, 主循环apply_bright即时生效 */
                        if (adj_val) cfg.bright_lvl = adj_val;
                    }
                } else if (tset_mode) {
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        if (++tset_idx > 5) {            /* 末字段后再按：保存退出 */
                            tset_mode = 0;
                            ds1302_write_time(&t_set);
                            beep_once();
                        }
                    } else if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        switch (tset_idx) {
                            case 0: inc_bcd(&t_set.hr, 23); break;
                            case 1: inc_bcd(&t_set.min, 59); break;
                            case 2: inc_bcd(&t_set.sec, 59); break;
                            case 3: inc_bcd(&t_set.year, 99); if (t_set.year < 0x26) t_set.year = 0x26; break;  /* 年 2026-2099 */
                            case 4: inc_bcd(&t_set.month, 12); break;
                            case 5: inc_date(&t_set); break;
                        }
                    }
                } else {  /* 常态: SET 控亮度/时间设置/IP; UP 手动切整屏模式; 走时SMG1选显(温度/日期)由 8266 配置 */
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        bright_adj = 1; adj_val = (cfg.bright_mode == 0) ? 0 : cfg.bright_lvl;
                    } else if (ke.btn == KEY_SET && ke.ev == EV_LONG) {
                        tset_mode = 1; tset_idx = 0;
                        ds1302_read_time(&t_set);
                    } else if (ke.btn == KEY_SET && ke.ev == EV_DOUBLE) {
                        ip_disp = 1; ip_ticks = 13;             /* 双击SET=显示P+IP末段3s + 对时 */
                        uart_send_null(CMD_REQ_TIME);
                    } else if (ke.btn == KEY_UP && ke.ev == EV_DOUBLE) {
                        mode = DISP_TIME;          /* 双击UP=恢复走时(自动) */
                    } else if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        mode = (mode < DISP_TEMP) ? (mode + 1) : DISP_TIME;  /* 手动切整屏模式 */
                    }
                }
            }
            delay_ms(10);
        }

        /* 双键同按 ≥5s：清 STA 凭据 + 重进 AP 配网（一次性, 21外循环×~240ms≈5s） */
        if (key_both_hold()) {
            if (++both_cnt >= 21) { if (!ap_sent) { ap_sent = 1; uart_send_null(CMD_ENTER_AP); beep_once(); } }
        } else { both_cnt = 0; ap_sent = 0; }

        /* 串口帧解析 + 心跳 + 上电 8266 握手窗口 */
        uart_poll();
        if (++hb_tick >= 4) { hb_tick = 0; uart_send_null(CMD_HEARTBEAT); }
        if (boot_t) {
            if (--boot_t == 0 && !esp_online) { beep_once(); beep_once(); beep_once(); } /* 无8266:响3声(beep_once 自带间隔) */
        }

        light = adc_read(0);
            apply_bright(light);   /* 含亮度调节态: cfg 实时改即即时预览 */
        temp_x10 = ntc_temp_x10(adc_read(1));

        ds1302_read_time(&t);
        if (t.sec != sec_last) { sec_last = t.sec; if (wake_ticks) wake_ticks--; }  /* 每秒递减唤醒计时 */

        /* 关屏窗：off_start/off_end 命中→整屏灭（§七, 须放显示填充后）；跨夜纯字节比较 */
        unsigned char sh = cfg.off_start[0], sm = cfg.off_start[1], eh = cfg.off_end[0], em = cfg.off_end[1];
        unsigned char off_on = (sh != 0xFF && eh != 0xFF) &&
            ((sh <= eh)
                ? ((t.hr > sh || (t.hr == sh && t.min >= sm)) && (t.hr < eh || (t.hr == eh && t.min < em)))
                : ((t.hr > sh || (t.hr == sh && t.min >= sm)) || (t.hr < eh || (t.hr == eh && t.min < em))));


        /* 分钟变化：整点报时 + 闹钟匹配(到点即接管蜂鸣, 保证0秒触发) */
        if (t.min != last_min) {
            last_min = t.min;
            if (cfg.display_mode == 1) mode = (mode < DISP_TEMP) ? (mode + 1) : DISP_TIME;  /* 8266 控制: 每分钟轮换整屏主模式 */
            {
                unsigned char a;
                for (a = 0; a < 3; a++) {
                    if (cfg.alarm[a][0] && cfg.alarm[a][1] == t.hr && cfg.alarm[a][2] == t.min) {
                        ring_alarm = (unsigned char)(a + 1); ring_ticks = 240; break;
                    }
                }
            }
        }
        /* 响铃相位：500ms 响 / 500ms 静；贪睡倒计时到点重响 */
        if (ring_alarm) {
            if (ring_ticks) {
                ring_ticks--;
                if ((ring_ticks & 3) < 2) BEEP_ON(); else BEEP_OFF();
                if (ring_ticks == 0) { ring_alarm = 0; BEEP_OFF(); }
            }
        } else if (snooze_ticks) {
            snooze_ticks--;
            if (snooze_ticks == 0) { ring_alarm = snooze_idx; ring_ticks = 240; snooze_idx = 0; }
        }

        /* 计时器：以 DS1302 秒为基准, 每秒 +1(封顶 99:59 自动停) */
        if (tm == 2) {
            if (t.sec != tm_last) {
                tm_last = t.sec;
                if (++tm_sec >= 60) { tm_sec = 0; tm_min++; }
                if (tm_min >= 100) { tm_min = 99; tm_sec = 59; tm = 1; }
            }
        }

        /* IP 显示 3s 倒计时 */
        if (ip_disp) { if (ip_ticks) ip_ticks--; else ip_disp = 0; }

        /* 状态灯：已联网同步则点亮(红灯可由 8266 经 cfg.led_en 关闭) */
        if (cfg.led_en && net_status >= 2) LED_ON(); else LED_OFF();

        blink ^= 1;
        if (bright_adj) {
            render_bright_adj(disp, adj_val, blink);
        } else if (tset_mode) {
            render_setting(disp, &t_set, tset_idx, blink);
        } else if (cd_disp) {
            put_mmss(disp, cd_mm, cd_ss);             /* 8266 倒计时 */
        } else if (tm) {
            put_mmss(disp, tm_min, tm_sec);           /* 计时器 */
        } else if (ip_disp) {
            /* P + 状态: 无8266=P000, 8266未联网=P404, 否则=P+IP末段 */
            unsigned char v = (!esp_online) ? 0 : (net_status ? sta_ip_last : 404);
            unsigned char h = 0, t = 0, o = v;
            while (o >= 100) { o -= 100; h++; }
            while (o >= 10)  { o -= 10;  t++; }
            disp[0] = SEG_P;
            disp[1] = seg_font[h];
            disp[2] = seg_rotate180(seg_font[t]);
            disp[3] = seg_font[o];
            disp[4] = disp[5] = disp[6] = disp[7] = 0;
        } else {
            if (clock_ok) disp_render(mode, &t, temp_x10, smg1_rot, disp);
            else { unsigned char i; for (i = 0; i < 8; i++) disp[i] = blink ? 0x7F : 0x00; } /* 未校时:全段闪 */
        }
        /* 关屏窗：off_on 命中则整屏灭(必须放显示填充之后, 见§七); 点按唤醒/响铃时强制亮屏 */
        if (off_on && !wake_ticks && !ring_alarm) { unsigned char i; for (i = 0; i < 8; i++) disp[i] = 0; }
        tm1639_write_display(disp);

        /* 走时: 大屏恒 HH:MM; SMG1 由 cfg.smg1_mode 固定选 温度/日期(8266 配置, 不轮换) */
        if (mode == DISP_TIME && !bright_adj && !tset_mode
            && !tm && !cd_disp && !ip_disp) {
            smg1_rot = (cfg.smg1_mode != 0);       /* 0=温度 1=日期 */
        }
    }
}
