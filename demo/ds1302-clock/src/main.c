#include "tm1639.h"
#include "ds1302.h"
#include "stc15.h"

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

/* 基准时间：2026-08-26 00:00:00（纽扣电池已装，DS1302 持续走时）。
   字段顺序与 ds_time 一致：sec, min, hr, date, month, weekday, year (均为 BCD) */
static const ds_time BASE = { 0x00, 0x00, 0x00, 0x26, 0x08, 0x03, 0x26 };

/* 时间 +1 分钟（24h 进位到日；日仅 1..31 回卷，不处理月/年进位） */
static void add_one_minute(ds_time *t) {
    int mm = ((t->min  >> 4) & 0x0F) * 10 + (t->min  & 0x0F);
    int hh = ((t->hr   >> 4) & 0x0F) * 10 + (t->hr   & 0x0F);
    int dd = ((t->date >> 4) & 0x0F) * 10 + (t->date & 0x0F);
    if (++mm >= 60) { mm = 0; if (++hh >= 24) { hh = 0; if (++dd > 31) dd = 1; } }
    t->min  = (unsigned char)(((mm / 10) << 4) | (mm % 10));
    t->hr   = (unsigned char)(((hh / 10) << 4) | (hh % 10));
    t->date = (unsigned char)(((dd / 10) << 4) | (dd % 10));
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

void main(void) {
    unsigned char disp[8];
    ds_time t;
    unsigned char prev_up = 0;
    unsigned int set_hold = 0;
    unsigned char reset_done = 0;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽 */
    BEEP_OFF();                  /* active-low: 高=静音 */
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.2/3.3 准双向(按键输入) */
    P1 &= ~0x04;                 /* LED_T 红：运行指示 */

    tm1639_init();
    ds1302_init();               /* 若停振则写入默认时间并启动走时 */

    while (1) {
        unsigned char up, set;
        ds1302_read_time(&t);
        render(disp, &t);
        tm1639_write_display(disp);

        up  = (unsigned char)(!(P3 & 0x04));   /* P3.2=UP，按下=低 */
        set = (unsigned char)(!(P3 & 0x08));   /* P3.3=SET，按下=低 */

        if (set) {
            set_hold += 20;
            if (set_hold >= 2000 && !reset_done) {
                ds1302_write_time(&BASE);       /* 重置为基准时间 */
                beep_once();                    /* 蜂鸣器响一下 */
                reset_done = 1;
            }
        } else {
            set_hold = 0;
            reset_done = 0;
        }

        if (up && !prev_up) {                   /* 单击 UP：时间 +1 分钟 */
            ds1302_read_time(&t);
            add_one_minute(&t);
            ds1302_write_time(&t);
        }
        prev_up = up;

        delay_ms(20);
    }
}
