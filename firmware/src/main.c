#include "tm1639.h"
#include "stc15.h"

/* BEEP(P2.1) 经 S9012 PNP 三极管驱动蜂鸣器：拉低=导通响，拉高=截止静音(active-low) */
#define BEEP_ON()   do { P2 &= ~0x02; } while (0)
#define BEEP_OFF()  do { P2 |= 0x02; } while (0)

#define MODE_SCROLL 0   /* 滚动自测 */
#define MODE_LIGHT  1   /* SET：大屏显光敏值，SMG2 显光敏档(2位) */
#define MODE_TEMP   2   /* UP：大屏显热敏值，SMG1 显 66 */

static void delay_ms(unsigned int ms) {
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 1200; j++);
}

/* 10 位 ADC 查询读，ch: 0=P1.0 光敏, 1=P1.1 热敏 */
static unsigned int adc_read(unsigned char ch) {
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL | (ch & 0x07) | ADC_START;
    while (!(ADC_CONTR & ADC_FLAG));
    ADC_CONTR &= ~ADC_FLAG;
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}

/* 非阻塞报警：8 步×250ms ≈ 2s 经典 BB(响250/停250×4)。循环照常跑，不阻塞光敏。 */
static unsigned char alarm_on;
static unsigned char alarm_step;
static void alarm_start(void) { alarm_on = 1; alarm_step = 0; }
static void alarm_tick(void) {
    if (!alarm_on) return;
    if (alarm_step & 1) BEEP_OFF(); else BEEP_ON();
    if (++alarm_step >= 8) { alarm_on = 0; BEEP_OFF(); }
}

/* 大屏(GRID1-4, LED2-5) 显示 0..9999 的 4 位值，GRID3(LED4) 倒装补偿。 */
static void render_big4(unsigned char disp[8], unsigned int num) {
    disp[0] = seg_font[(num / 1000) % 10];
    disp[1] = seg_font[(num / 100)  % 10];
    disp[2] = seg_rotate180(seg_font[(num / 10) % 10]);   /* GRID3 倒装管 */
    disp[3] = seg_font[num % 10];
}

/* 滚动自测(参考 3e8094f)：初始 12345678，每位独立循环 1-8，每 2 秒进一位；
   8 位小数点 2Hz 闪烁；GRID3 倒装；SMG2 右显当前亮度档。 */
static void build_scroll(unsigned char disp[8], unsigned char frame, unsigned int tick) {
    unsigned char i, v;
    for (i = 0; i < 8; i++) {
        if (i == 4) v = ((5 + frame) % 8) + 1;       /* SMG1 右=GRID5 */
        else if (i == 5) v = ((4 + frame) % 8) + 1;  /* SMG1 左=GRID6 */
        else if (i == 6) v = ((7 + frame) % 8) + 1;  /* SMG2 右=GRID7：纯滚动 */
        else if (i == 7) v = ((6 + frame) % 8) + 1;  /* SMG2 左=GRID8 */
        else v = ((i + frame) % 8) + 1;              /* 大屏 GRID1-4 */
        disp[i] = seg_font[v];
        if (i == 2) disp[i] = seg_rotate180(disp[i]);
        if (tick & 1) disp[i] |= 0x80;               /* 小数点闪烁(2Hz) */
    }
}

void main(void) {
    unsigned char disp[8];
    unsigned char frame, lvl, last_lvl = 0xFF;
    unsigned char mode = MODE_SCROLL;
    unsigned char prev_up = 0, prev_set = 0, both_cnt = 0;
    unsigned int tick = 0, light, temp;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽 */
    BEEP_OFF();                  /* 启动即静音(active-low: 高=停) */
    P3M1 &= ~0x0C; P3M0 &= ~0x0C; /* P3.2/3.3 准双向(内部上拉，按键输入) */
    delay_ms(200);
    P1 &= ~0x04;                 /* LED_T 低有效：红灯亮(运行指示) */

    P1ASF |= 0x03;               /* P1.0 光敏 / P1.1 热敏 作模拟输入 */
    P1M1 |= 0x03; P1M0 &= ~0x03; /* P1.0/1.1 高阻 */
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL;  /* ADC 上电 */
    delay_ms(1);

    tm1639_init();
    build_scroll(disp, 0, 0);
    tm1639_write_display(disp);  /* 先出显示 */
    delay_ms(300);
    alarm_start();               /* 显示后才响 2 秒报警音(非阻塞) */

    while (1) {
        unsigned char up   = (unsigned char)(!(P3 & 0x04));   /* P3.2=UP，按下=低 */
        unsigned char set  = (unsigned char)(!(P3 & 0x08));   /* P3.3=SET，按下=低 */

        if (up && set) {                                  /* 两键同按：计 2s 回滚动 */
            if (++both_cnt >= 8) mode = MODE_SCROLL;
        } else {
            both_cnt = 0;
            if (set && !prev_set) mode = MODE_LIGHT;       /* 单击 SET */
            else if (up && !prev_up) mode = MODE_TEMP;     /* 单击 UP */
        }
        prev_up = up; prev_set = set;

        light = adc_read(0);
        {                                               /* 光敏标定：遮住≈512..767→0(最暗)，强光<~85→7(最亮) */
            unsigned int t = light;
            if (t > 512) t = 512;
            lvl = (unsigned char)(7 - (t * 7) / 512);
        }
        if (lvl != last_lvl) { tm1639_set_brightness(lvl); last_lvl = lvl; }

        tick++;
        frame = (unsigned char)(tick >> 3);              /* 每 8 tick(2s) 进一位 */

        if (mode == MODE_SCROLL) {
            build_scroll(disp, frame, tick);
        } else if (mode == MODE_LIGHT) {
            render_big4(disp, light);                    /* 大屏=光敏值 */
            disp[7] = seg_font[lvl / 10];                /* SMG2 左=档十位 */
            disp[6] = seg_font[lvl % 10];                /* SMG2 右=档个位(00..07) */
            disp[4] = 0; disp[5] = 0;                    /* SMG1 灭 */
        } else { /* MODE_TEMP */
            temp = adc_read(1);
            render_big4(disp, temp);                     /* 大屏=热敏值 */
            disp[5] = seg_font[6];                       /* SMG1 左=6 */
            disp[4] = seg_font[6];                       /* SMG1 右=6 */
            disp[6] = 0; disp[7] = 0;                    /* SMG2 灭 */
        }

        tm1639_write_display(disp);
        alarm_tick();
        delay_ms(250);
    }
}
