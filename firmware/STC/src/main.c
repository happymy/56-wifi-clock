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
#define LED_ON()    do { P1 |= 0x04; } while (0)
#define LED_OFF()   do { P1 &= ~0x04; } while (0)

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

static void beep_once(void) { BEEP_ON(); delay_ms(150); BEEP_OFF(); }

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
/* 秒→MM:SS */
static void sec2mmss(unsigned int s, __data unsigned char *mm, __data unsigned char *ss) {
    unsigned char m = 0;
    while (s >= 60) { s -= 60; m++; }
    *mm = m; *ss = (unsigned char)s;
}
static unsigned char bcd2bin(unsigned char b) {
    unsigned char hi = (b >> 4) & 0x0F, lo = b & 0x0F;
    return (unsigned char)(hi * 8 + hi * 2 + lo);   /* 免 __mulint(29B): hi*10+lo */
}
static void inc_bcd(__xdata unsigned char *f, unsigned char max_dec) {
    int v = bcd2bin(*f);
    if (++v > max_dec) v = 0;
    { unsigned char b[2]; u2bcd(b, (unsigned char)v); *f = (b[0] << 4) | b[1]; }
}
static unsigned char days_in_month(unsigned char mon_bcd, unsigned char yr_bcd) {
    static const unsigned char d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m = bcd2bin(mon_bcd);
    int y = 2000 + bcd2bin(yr_bcd);
    if (m == 2) {
        int leap = ((y & 3) == 0);   /* 2000-2099 范围: 仅 %4, 等价闰年; 免除法库 */
        return (unsigned char)(leap ? 29 : 28);
    }
    if (m < 1 || m > 12) return 31;
    return d[m - 1];
}
static void inc_date(__xdata ds_time *t) {
    unsigned char d = bcd2bin(t->date);
    unsigned char max = days_in_month(t->month, t->year);
    if (++d > max) d = 1;
    { unsigned char b[2]; u2bcd(b, d); t->date = (b[0] << 4) | b[1]; }
}

static void apply_bright(unsigned int light) {
    if (cfg.bright_mode == 0) tm1639_set_light(light);
    else tm1639_set_brightness((unsigned char)(cfg.bright_lvl - 1));
}

/* 亮度调节：大屏闪显档位(0=自动,1..8=手动) */
static void render_bright_adj(__xdata unsigned char *disp, unsigned char val, unsigned char blank) {
    unsigned char i;
    for (i = 0; i < 8; i++) disp[i] = 0;
    if (!blank) {
        unsigned char b[2]; u2bcd(b, val);
        disp[1] = seg_font[b[0]];
        disp[3] = seg_font[b[1]];
        disp[2] = seg_rotate180(seg_font[b[1]]);
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
    disp[0] = seg_font[b[0]]; disp[1] = seg_font[b[1]];
    u2bcd(b, ss);
    disp[2] = seg_rotate180(seg_font[b[0]]); disp[3] = seg_font[b[1]];
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

#define URX_LEN 32
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
        default: break;
    }
}
static unsigned char p_st = 0, p_cmd = 0, p_len = 0, p_idx = 0, p_chk = 0;
static __xdata unsigned char p_buf[64];
static void uart_poll(void) {
    while (urx_r != urx_w) {
        unsigned char b = urx[urx_r]; urx_r = (urx_r + 1) & (URX_LEN - 1);
        switch (p_st) {
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
    __xdata unsigned char mode = DISP_TIME, cyc = 0, blink = 0;
    __xdata unsigned char bright_adj = 0, adj_val = 0;
    __xdata unsigned char tset_mode = 0, tset_idx = 0;
    __xdata unsigned char last_min = 0xFF;
    __xdata unsigned char ring_alarm = 0;       /* 0=静音, 1..3 正在响 */
    __xdata unsigned int  ring_ticks = 0;        /* 当前响铃剩余 250ms 拍 */
    __xdata unsigned int  snooze_ticks = 0;     /* 贪睡倒计时(拍) */
    __xdata unsigned char snooze_idx = 0;       /* 贪睡对应的闹钟索引 */
    __xdata unsigned char cd_mode = 0;          /* 0关 1设定 2运行 */
    __xdata unsigned char cd_min = 5;           /* 倒计时设定分钟 */
    __xdata unsigned int  cd_ticks = 0;         /* 运行剩余拍 */
    __xdata unsigned char cd_ring = 0;          /* 归零后响铃拍 */
    __xdata unsigned char mode_manual = 0;      /* 1=手动锁定显示模式 */
    __xdata unsigned char ip_disp = 0;           /* 显示配网 IP 末段 10s */
    __xdata unsigned int ip_ticks = 0;
    __xdata unsigned char hb_tick = 0, boot_t = 8, ap_sent = 0;
    __xdata unsigned int light;
    __xdata int temp_x10;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽(安全) */
    BEEP_OFF();
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.2/3.3 准双向 */
    /* 安全红线: P3.0/P3.1 保持准双向, 绝不置强推挽 */
    P1 &= ~0x04;                 /* LED_T 红：运行指示 */

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
            while (key_get(&ke)) {
                /* 响铃/贪睡中：任意键处理停止与贪睡 */
                if (ring_alarm || snooze_ticks) {
                    if (ke.ev == EV_SINGLE) {
                        if (ke.btn == KEY_SET) {
                            ring_alarm = 0; ring_ticks = 0; snooze_ticks = 0; BEEP_OFF();
                        } else { /* KEY_UP = 贪睡 */
                            unsigned char idx = ring_alarm ? ring_alarm : snooze_idx;
                            ring_alarm = 0; ring_ticks = 0; BEEP_OFF();
                            snooze_idx = idx; snooze_ticks = (unsigned int)cfg.snooze * 240u;
                        }
                    }
                    continue;
                }
                if (cd_mode) {
                    if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        if (cd_mode == 1) { if (cd_min >= 60) cd_min = 0; cd_min += 1; }   /* 免%60 */
                    } else if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        if (cd_mode == 1) { cd_mode = 2; cd_ticks = (unsigned int)cd_min * 240u; cfg.cd_preset = cd_min; }
                        else { cd_mode = 0; cd_ring = 0; BEEP_OFF(); }
                    }
                    continue;
                }
                if (bright_adj) {
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        bright_adj = 0;
                        cfg.bright_mode = (adj_val == 0) ? 0 : 1;
                        if (adj_val) cfg.bright_lvl = adj_val;
                    } else if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        adj_val = (adj_val >= 8) ? 0 : adj_val + 1;   /* 亮度0-8循环, 免%9 */
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
                            case 3: inc_bcd(&t_set.year, 99); break;
                            case 4: inc_bcd(&t_set.month, 12); break;
                            case 5: inc_date(&t_set); break;
                        }
                    }
                } else {  /* 常态 */
                    if (ke.btn == KEY_SET && ke.ev == EV_SINGLE) {
                        bright_adj = 1; adj_val = (cfg.bright_mode == 0) ? 0 : cfg.bright_lvl;
                    } else if (ke.btn == KEY_SET && ke.ev == EV_LONG) {
                        tset_mode = 1; tset_idx = 0;
                        ds1302_read_time(&t_set);
                    } else if (ke.btn == KEY_UP && ke.ev == EV_DOUBLE) {
                        cd_mode = 1; cd_min = cfg.cd_preset;
                    } else if (ke.btn == KEY_SET && ke.ev == EV_DOUBLE) {
                        ip_disp = 1; ip_ticks = 40;             /* 双击SET=显示IP末段+对时 */
                        uart_send_null(CMD_REQ_TIME);
                    } else if (ke.btn == KEY_UP && ke.ev == EV_SINGLE) {
                        mode_manual = 1;
                        mode = (mode < DISP_TEMP) ? (mode + 1) : DISP_TIME;  /* 手动切模式 */
                    }
                }
            }
            delay_ms(10);
        }

        /* 双键同按 ≥2s：清 STA 凭据 + 重进 AP 配网（一次性） */
        if (key_both_hold()) {
            if (!ap_sent) { ap_sent = 1; uart_send_null(CMD_ENTER_AP); beep_once(); }
        } else ap_sent = 0;

        /* 串口帧解析 + 心跳 + 上电 8266 握手窗口 */
        uart_poll();
        if (++hb_tick >= 4) { hb_tick = 0; uart_send_null(CMD_HEARTBEAT); }
        if (boot_t) {
            if (--boot_t == 0 && !esp_online) { beep_once(); beep_once(); beep_once(); } /* 无8266:响3声 */
        }

        light = adc_read(0);
        if (!bright_adj) apply_bright(light);
        temp_x10 = ntc_temp_x10(adc_read(1));

        ds1302_read_time(&t);

        /* 分钟变化：整点报时 + 闹钟匹配（响铃中不重复触发） */
        if (t.min != last_min) {
            last_min = t.min;
            if (t.min == 0 && cfg.chime >= 1) beep_once();
            else if (t.min == 30 && cfg.chime == 2) beep_once();
            if (!ring_alarm && !snooze_ticks) {
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
            if (snooze_ticks == 0) { ring_alarm = snooze_idx; ring_ticks = 240; }
        }

        /* 倒计时：运行递减，归零响铃；计时器：运行递增 */
        if (cd_mode == 2 && cd_ticks) {
            cd_ticks--;
            if (cd_ticks == 0) cd_ring = 60;   /* 归零响 ~15s */
        }
        if (cd_ring) {
            cd_ring--;
            if ((cd_ring & 3) < 2) BEEP_ON(); else BEEP_OFF();
            if (cd_ring == 0) { cd_mode = 0; BEEP_OFF(); }
        }

        /* IP 显示 10s 倒计时 */
        if (ip_disp) { if (ip_ticks) ip_ticks--; else ip_disp = 0; }

        /* 关屏时段：off_start/off_end = [时,分]，0xFF=禁用；支持跨夜（纯字节比较省 overlay） */
        {
            unsigned char off_on = 0;
            if (cfg.off_start[0] != 0xFF && cfg.off_end[0] != 0xFF) {
                unsigned char sh = cfg.off_start[0], sm = cfg.off_start[1];
                unsigned char eh = cfg.off_end[0],   em = cfg.off_end[1];
                unsigned char th = t.hr, tm = t.min;
                if (sh <= eh)
                    off_on = ((th > sh) || (th == sh && tm >= sm)) && ((th < eh) || (th == eh && tm < em));
                else
                    off_on = !(((th < sh) || (th == sh && tm < sm)) && ((th < eh) || (th == eh && tm < em)));
            }
            if (off_on) { unsigned char i; for (i = 0; i < 8; i++) disp[i] = 0; }
        }

        /* 状态灯：已联网同步则点亮 */
        if (net_status >= 2) LED_ON(); else LED_OFF();

        blink ^= 1;
        if (bright_adj) {
            render_bright_adj(disp, adj_val, blink);
        } else if (tset_mode) {
            render_setting(disp, &t_set, tset_idx, blink);
        } else if (cd_mode == 1) {
            put_mmss(disp, 0, cd_min);                 /* 设定分钟 */
            if (blink) { disp[0] = disp[1] = disp[2] = disp[3] = 0; }
        } else if (cd_mode == 2) {
            unsigned int sec = cd_ticks / 4u;
            unsigned char mm, ss; sec2mmss(sec, &mm, &ss);
            put_mmss(disp, mm, ss);
        } else if (ip_disp) {
            unsigned char v = sta_ip_last, h = 0, t = 0, o = v;  /* P + 末段(0-255) */
            while (o >= 100) { o -= 100; h++; }
            while (o >= 10)  { o -= 10;  t++; }
            disp[0] = SEG_P;
            disp[1] = seg_font[h];
            disp[2] = seg_rotate180(seg_font[t]);
            disp[3] = seg_font[o];
            disp[4] = disp[5] = disp[6] = disp[7] = 0;
        } else {
            if (clock_ok) disp_render(mode, &t, temp_x10, cfg.temp_unit, disp);
            else { unsigned char i; for (i = 0; i < 8; i++) disp[i] = blink ? 0x7F : 0x00; } /* 未校时:全段闪 */
        }
        tm1639_write_display(disp);

        if (!bright_adj && !tset_mode && !cd_mode && !mode_manual) {
            if (++cyc >= 12) { cyc = 0; if (++mode > DISP_TEMP) mode = DISP_TIME; }
        }
    }
}
