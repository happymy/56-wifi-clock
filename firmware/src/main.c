#include "tm1639.h"
#include "stc15.h"

/* BEEP(P2.1) 经 S9012 PNP 三极管驱动蜂鸣器：拉低=导通响，拉高=截止静音(active-low) */
#define BEEP_ON()   do { P2 &= ~0x02; } while (0)
#define BEEP_OFF()  do { P2 |= 0x02; } while (0)

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

/* 显示顺序自测(滚动)：初始 12345678，每位独立循环 1-8，每 2 秒进一位；
   8 位小数点一起 2Hz 闪烁。GRID3(LED4) 倒装管做 180° 段补偿。
   SMG2 右(GRID7) 固定显示当前亮度档 0..7，便于观察光敏调光是否生效。 */
static void build_disp(unsigned char disp[8], unsigned char frame, unsigned int tick, unsigned char lvl) {
    unsigned char i, v;
    for (i = 0; i < 8; i++) {
        if (i == 4) v = ((5 + frame) % 8) + 1;       /* SMG1 右=GRID5 */
        else if (i == 5) v = ((4 + frame) % 8) + 1;  /* SMG1 左=GRID6 */
        else if (i == 6) v = lvl;                    /* SMG2 右=GRID7：亮度档 */
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
    unsigned int tick = 0, light;

    P2M1 &= ~0x02; P2M0 |= 0x02; /* BEEP(P2.1) 推挽 */
    BEEP_OFF();                  /* 启动即静音(active-low: 高=停) */
    delay_ms(200);
    P1 &= ~0x04;                 /* LED_T 低有效：红灯亮(运行指示) */

    P1ASF |= 0x01;               /* P1.0 光敏作模拟输入 */
    P1M1 |= 0x01; P1M0 &= ~0x01; /* P1.0 高阻 */
    ADC_CONTR = ADC_POWER | ADC_SPEEDLL;  /* ADC 上电 */
    delay_ms(1);

    tm1639_init();
    build_disp(disp, 0, 0, 7);
    tm1639_write_display(disp);  /* 先出显示 */
    delay_ms(300);
    alarm_start();               /* 显示后才响 2 秒报警音(非阻塞) */

    while (1) {
        tick++;
        frame = (unsigned char)(tick >> 3);          /* 每 8 tick(2s) 进一位 */

        light = adc_read(0);
        {                                       /* 光敏标定：遮住≈512..767→档0(最暗)，强光<~85→档7(最亮) */
            unsigned int t = light;
            if (t > 512) t = 512;
            lvl = (unsigned char)(7 - (t * 7) / 512);
        }
        if (lvl != last_lvl) { tm1639_set_brightness(lvl); last_lvl = lvl; }

        build_disp(disp, frame, tick, lvl);
        tm1639_write_display(disp);
        alarm_tick();
        delay_ms(250);
    }
}
