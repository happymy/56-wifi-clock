#include "tm1639.h"
#include "ds1302.h"
#include "stc15.h"

/* 硬件串口：UART1 经 P3.1(TXD) 输出，9600 8N1，内部 IRC 11.063MHz。
   STC15W408AS 无 Timer1，波特源必须用 Timer2。重装值公式 = 65536 - FOSC/4/BAUD。 */
static void uart_init(void) {
    P3M1 &= ~0x02; P3M0 |= 0x02;   /* P3.1 推挽输出(否则驱动不出 TX 电平) */
    SCON  = 0x50;                  /* 模式1、8N1、REN=1 */
    /* 11063MHz/4/9600 = 288 -> 重装值 65536-288 = 65248 = 0xFEE0 -> 实测 9603bps */
    T2L = 0xE0;
    T2H = 0xFE;
    AUXR  = 0x14;                  /* Timer2 1T 模式 + 启动 Timer2(bit4 BRTR) */
    AUXR |= 0x01;                  /* bit0 S1BRS=1：选 Timer2 作 UART1 波特源 */
}
static void uart_putc(unsigned char c) {
    unsigned int w = 0;
    SBUF = c;
    while (!(SCON & 0x02)) { if (++w >= 30000) break; }  /* 波特时钟异常时防死等 */
    SCON &= ~0x02;                    /* 清 TI */
}
static void uart_str(const char *s) { while (*s) uart_putc((unsigned char)*s++); }
static void uart_hex(unsigned char v) {
    static const char HEX[] = "0123456789ABCDEF";
    uart_putc((unsigned char)HEX[v >> 4]);
    uart_putc((unsigned char)HEX[v & 0x0F]);
}
static void uart_u8(unsigned char v) {        /* 十进制 0..255 */
    unsigned char buf[3]; unsigned char i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[i++] = (unsigned char)('0' + v % 10); v /= 10; }
    while (i) uart_putc(buf[--i]);
}
static void dbg_time(const char *tag, const ds_time *t) {
    uart_str(tag);
    uart_str(" raw=sec:"); uart_hex(t->sec); uart_str(" min:"); uart_hex(t->min);
    uart_str(" hr:"); uart_hex(t->hr); uart_str(" date:"); uart_hex(t->date);
    uart_str(" mon:"); uart_hex(t->month); uart_str(" wd:"); uart_hex(t->weekday);
    uart_str(" yr:"); uart_hex(t->year);
    uart_str("  CH="); uart_putc((t->sec & 0x80) ? '1' : '0');
    uart_str("  time=");
    uart_u8((unsigned char)((t->hr   >> 4) * 10 + (t->hr   & 0x0F))); uart_putc(':');
    uart_u8((unsigned char)((t->min  >> 4) * 10 + (t->min  & 0x0F))); uart_putc(':');
    uart_u8((unsigned char)((t->sec  >> 4) * 10 + (t->sec  & 0x0F)));
    uart_putc(' ');
    uart_u8((unsigned char)((t->date >> 4) * 10 + (t->date & 0x0F)));
    uart_putc('/'); uart_u8(t->month); uart_putc('/'); uart_u8(t->year);
    uart_str("\r\n");
}

/* BEEP(P2.1) 经 S9012 PNP 三极管驱动蜂鸣器：拉低=导通响，拉高=截止静音(active-low) */
#define BEEP_ON()   do { P2 &= ~0x02; } while (0)
#define BEEP_OFF()  do { P2 |= 0x02; } while (0)

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

static void beep_once(void) {
    BEEP_ON();
    delay_ms(150);
    BEEP_OFF();
}

/* 某 BCD 字段 +1，到 max_dec 后回卷为 0（手动设置不进位） */
static void inc_bcd(unsigned char *f, unsigned char max_dec) {
    int v = ((*f >> 4) & 0x0F) * 10 + (*f & 0x0F);
    if (++v > max_dec) v = 0;
    *f = (unsigned char)(((v / 10) << 4) | (v % 10));
}

static unsigned char days_in_month(unsigned char mon_bcd, unsigned char yr_bcd) {
    static const unsigned char d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m = ((mon_bcd >> 4) & 0x0F) * 10 + (mon_bcd & 0x0F);
    int y = 2000 + ((yr_bcd >> 4) & 0x0F) * 10 + (yr_bcd & 0x0F);
    if (m == 2) {
        int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return (unsigned char)(leap ? 29 : 28);
    }
    if (m < 1 || m > 12) return 31;
    return d[m - 1];
}

static void inc_date(ds_time *t) {
    int d = ((t->date >> 4) & 0x0F) * 10 + (t->date & 0x0F);
    int max = days_in_month(t->month, t->year);
    if (++d > max) d = 1;
    t->date = (unsigned char)(((d / 10) << 4) | (d % 10));
}

/* 大屏(GRID1-4, LED2-5)=HH:MM；SMG1(上排)=日 DD；SMG2(下排)=秒 SS */
static void render(unsigned char disp[8], const ds_time *t) {
    unsigned char H = ((t->hr   >> 4) & 0x0F) * 10 + (t->hr   & 0x0F);
    unsigned char M = ((t->min  >> 4) & 0x0F) * 10 + (t->min  & 0x0F);
    unsigned char S = ((t->sec  >> 4) & 0x0F) * 10 + (t->sec  & 0x0F);
    unsigned char D = ((t->date >> 4) & 0x0F) * 10 + (t->date & 0x0F);
    disp[0] = seg_font[H / 10];
    disp[1] = seg_font[H % 10];
    disp[2] = seg_rotate180(seg_font[M / 10]);   /* GRID3 倒装管 */
    disp[3] = seg_font[M % 10];
    disp[5] = seg_font[D / 10];                  /* SMG1 左 = 日十位 */
    disp[4] = seg_font[D % 10];                  /* SMG1 右 = 日个位 */
    disp[7] = seg_font[S / 10];                  /* SMG2 左 = 秒十位 */
    disp[6] = seg_font[S % 10];                  /* SMG2 右 = 秒个位 */
}

/* 设置模式渲染：HH:MM 大屏；SMG1/SMG2 小屏按当前字段显示，blank=1 时该字段熄灭形成闪烁。
   set_idx: 0年 1月 2日 3时 4分 5秒 6星期(0-6, 0=周日) */
static void render_setting(unsigned char disp[8], const ds_time *t, unsigned char idx, unsigned char blank) {
    unsigned char H  = ((t->hr     >> 4) & 0x0F) * 10 + (t->hr     & 0x0F);
    unsigned char M  = ((t->min    >> 4) & 0x0F) * 10 + (t->min    & 0x0F);
    unsigned char S  = ((t->sec    >> 4) & 0x0F) * 10 + (t->sec    & 0x0F);
    unsigned char D  = ((t->date   >> 4) & 0x0F) * 10 + (t->date   & 0x0F);
    unsigned char MO = ((t->month  >> 4) & 0x0F) * 10 + (t->month  & 0x0F);
    unsigned char Y  = ((t->year   >> 4) & 0x0F) * 10 + (t->year   & 0x0F);
    unsigned char W  = ((t->weekday >> 4) & 0x0F) * 10 + (t->weekday & 0x0F);
    unsigned char smg1_t = D / 10, smg1_o = D % 10;   /* SMG1 默认：日 */
    unsigned char smg2_t = S / 10, smg2_o = S % 10;   /* SMG2 默认：秒 */
    if (idx == 1) { smg1_t = MO / 10; smg1_o = MO % 10; }  /* 月 */
    if (idx == 0) { smg2_t = Y  / 10; smg2_o = Y  % 10; }  /* 年 */
    if (idx == 6) { smg1_t = 0;       smg1_o = W;        }  /* 星期 */

    disp[0] = (idx == 3 && blank) ? 0x00 : seg_font[H / 10];
    disp[1] = (idx == 3 && blank) ? 0x00 : seg_font[H % 10];
    disp[2] = seg_rotate180((idx == 4 && blank) ? 0x00 : seg_font[M / 10]);
    disp[3] = (idx == 4 && blank) ? 0x00 : seg_font[M % 10];
    disp[5] = ((idx == 1 || idx == 2 || idx == 6) && blank) ? 0x00 : seg_font[smg1_t];
    disp[4] = ((idx == 1 || idx == 2 || idx == 6) && blank) ? 0x00 : seg_font[smg1_o];
    disp[7] = ((idx == 0 || idx == 5) && blank) ? 0x00 : seg_font[smg2_t];
    disp[6] = ((idx == 0 || idx == 5) && blank) ? 0x00 : seg_font[smg2_o];
}

void main(void) {
    unsigned char disp[8];
    ds_time t, t_set;
    unsigned char prev_up = 0, prev_set = 0;
    unsigned char setting = 0, set_idx = 0, blink = 0;
    unsigned int blink_tick = 0;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽 */
    BEEP_OFF();                  /* active-low: 高=静音 */
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.2/3.3 准双向(按键输入) */
    P1 &= ~0x04;                 /* LED_T 红：运行指示 */

    uart_init();
    uart_str("boot\r\n");

    tm1639_init();
    delay_ms(500);               /* 等 Vcc 稳定后再访问 DS1302，避免上电首读读到乱码误判掉电 */
    ds1302_init();               /* 若停振则写入默认时间并启动走时 */
    { ds_time t0; ds1302_read_time(&t0); dbg_time("init", &t0); }
    uart_str("init_action="); uart_u8(g_ds_init_action); uart_str("\r\n");

    while (1) {
        unsigned char up, set;

        up  = (unsigned char)(!(P3 & 0x04));   /* P3.2=UP，按下=低 */
        set = (unsigned char)(!(P3 & 0x08));   /* P3.3=SET，按下=低 */

        if (set && !prev_set) {                /* SET 单击：进入/切换字段/保存退出 */
            if (!setting) {
                setting = 1; set_idx = 0;
                ds1302_read_time(&t_set);      /* 进入时拷贝当前时间 */
            } else if (++set_idx > 6) {        /* 末位(星期)后再按：保存并退出 */
                setting = 0;
                ds1302_write_time(&t_set);
                beep_once();
            }
        }
        if (up && !prev_up && setting) {       /* UP 单击：当前字段 +1 */
            switch (set_idx) {
                case 0: inc_bcd(&t_set.year, 99); break;
                case 1: inc_bcd(&t_set.month, 12); if (t_set.month == 0) t_set.month = 1; break;   /* 月 12→1 不设 0 */
                case 2: inc_date(&t_set); break;
                case 3: inc_bcd(&t_set.hr, 23); break;
                case 4: inc_bcd(&t_set.min, 59); break;
                case 5: inc_bcd(&t_set.sec, 59); break;
                case 6: inc_bcd(&t_set.weekday, 6); break;   /* 星期 0-6: 0=周日 */
            }
        }
        prev_up = up; prev_set = set;

        if (setting) render_setting(disp, &t_set, set_idx, blink);
        else { ds1302_read_time(&t); render(disp, &t); }
        tm1639_write_display(disp);

        blink_tick += 20;                      /* ~2Hz 闪烁 */
        if (blink_tick >= 250) { blink_tick = 0; blink ^= 1; }

        { static unsigned int dbg_tick = 0;
          dbg_tick += 20;
          if (dbg_tick >= 250) {
              dbg_tick = 0;
              if (setting) dbg_time("set", &t_set);
              else dbg_time("loop", &t);
              uart_str(" setting="); uart_putc(setting ? '1' : '0');
              uart_str(" idx="); uart_u8(set_idx);
              uart_str(" blink="); uart_putc(blink ? '1' : '0');
              uart_str(" up="); uart_putc(up ? '1' : '0');
              uart_str(" set="); uart_putc(set ? '1' : '0');
              uart_str("\r\n");
          }
        }

        delay_ms(20);
    }
}
