#include "tm1639.h"

/* 软件 PWM 极暗：硬件 8 级占空比下限为 1/16(档0)，全黑(光敏>512)时仍偏亮。
   用定时器 T0 间歇开关显示，把有效占空比压到 1/16 之下。
   节拍 1ms(12T/11.051MHz 复位默认)，PWM_STEPS 步一周期 → 开关边沿≈1kHz，肉眼不闪。
   调光比例 = pwm_on_steps/PWM_STEPS，与节拍快慢无关，故 12T/1T 偏差只改频率不改亮度。 */
#define PWM_TH 0xFC
#define PWM_TL 0x67
#define PWM_STEPS 4
static unsigned char pwm_active = 0;   /* 1 = 软件 PWM 极暗启用 */
static unsigned char pwm_on_steps = PWM_STEPS;
static unsigned char pwm_phase = 0;
static unsigned char cur_lvl = 0xFF;   /* 已生效硬件亮度档(缓存去重) */
static unsigned char cur_on = 0xFF;    /* 已生效 PWM on 步数(缓存去重) */
static unsigned char cur_pwm = 0xFF;   /* 已生效 PWM 开关(缓存去重) */
static void tm1639_pwm_init(void); /* 前向声明(SDCC 单趟编译) */

/* 共阴七段字模：bit 序 dp,g,f,e,d,c,b,a
   全亮 '8' = 0x7F，'-' = 0x40，blank = 0x00 */
const unsigned char seg_font[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

static unsigned char g_bright = 7;   /* 当前亮度 0..7，写屏/调亮度共用 */

/* 7 段数码管旋转 180°：补偿物理倒装管（a↔d, b↔e, c↔f, g↔g）。
   倒装管顶部 view_a 实为 tube_d 段，故 view_seg = rotate(tube_seg)。 */
unsigned char seg_rotate180(unsigned char d) {
    return ((d >> 3) & 1u) << 0   /* a <- d */
         | ((d >> 4) & 1u) << 1   /* b <- e */
         | ((d >> 5) & 1u) << 2   /* c <- f */
         | ((d >> 0) & 1u) << 3   /* d <- a */
         | ((d >> 1) & 1u) << 4   /* e <- b */
         | ((d >> 2) & 1u) << 5   /* f <- c */
         | ((d >> 6) & 1u) << 6   /* g <- g */
         | (d & 0x80);            /* dp <- dp */
}

static void tm_delay(void) {
    volatile unsigned char i;
    for (i = 0; i < 10; i++) { /* ~us 级，满足 TM1639 时序 */
    }
}

/* 低位先发；CLK 上升沿锁存 DIO */
static void tm_write_byte(unsigned char dat) {
    unsigned char i;
    for (i = 0; i < 8; i++) {
        P2 &= ~TM_CLK_MASK;
        if (dat & 0x01) P2 |= TM_DIO_MASK; else P2 &= ~TM_DIO_MASK;
        tm_delay();
        P2 |= TM_CLK_MASK;
        tm_delay();
        dat >>= 1;
    }
}

void tm1639_init(void) {
    unsigned char blank[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    P2 |= TM_STB_MASK | TM_CLK_MASK | TM_DIO_MASK; /* 三线 idle 高 */
    tm1639_write_display(blank);
    tm1639_pwm_init();
}

/* data[0..7] 对应 GRID1..GRID8（TM1639 每 GRID 占 2 字节）。
   本板为 24 脚封装：a-d 段接 SEG1-4（低字节 bit0-3），
   e-g/dp 段实际接 SEG9-12（高字节 bit0-3）；SEG5-8 未引出，置 0。 */
void tm1639_write_display(const unsigned char data[8]) {
    unsigned char i;
    unsigned char et0 = ET0;
    ET0 = 0;                       /* 写数据期间关 PWM 中断, 防插帧破坏 */
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x40);              /* 数据命令：写显示，地址自增 */
    P2 |= TM_STB_MASK; tm_delay();

    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0xC0);              /* 起始地址 00H(GRID1 低字节) */
    for (i = 0; i < 8; i++) {
        tm_write_byte(data[i] & 0x0F);        /* 低字节 SEG1-4 = a-d */
        tm_write_byte((data[i] >> 4) & 0x0F); /* 高字节 SEG9-12 = e-g,dp */
    }
    P2 |= TM_STB_MASK; tm_delay();

    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(0x88 | g_bright);  /* 显示控制：开 + 当前占空比(0..7) */
    P2 |= TM_STB_MASK;
    ET0 = et0;
}

void tm1639_set_brightness(unsigned char level) {
    unsigned char cmd;
    unsigned char et0 = ET0;
    if (level > 7) level = 7;
    g_bright = level;
    cmd = 0x88 | level;   /* 显示开 + 占空比 0..7（0=1/16 最暗，7=最亮） */
    ET0 = 0;                       /* 防与 PWM 中断冲突 */
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(cmd);
    P2 |= TM_STB_MASK;
    ET0 = et0;
}

/* 仅切换显示开关(不动显示数据): on=1 → 0x88|g_bright(亮), on=0 → 0x80(灭) */
static void tm_set_display_enable(unsigned char on) {
    unsigned char cmd = on ? (0x88 | g_bright) : 0x80;
    P2 &= ~TM_STB_MASK; tm_delay();
    tm_write_byte(cmd);
    P2 |= TM_STB_MASK;
}

/* 启用/关闭极暗 PWM。enable=0 时确保显示常亮于当前档。 */
void tm1639_ultradim(unsigned char enable, unsigned char on_steps) {
    unsigned char et0 = ET0;
    ET0 = 0;
    pwm_active = enable ? 1 : 0;
    if (on_steps > PWM_STEPS) on_steps = PWM_STEPS;
    pwm_on_steps = on_steps;
    if (!enable) { pwm_phase = 0; tm_set_display_enable(1); }
    ET0 = et0;
}

/* 亮度总入口: 亮区走硬件 8 档; 暗区(>512)以 1/16 为基档叠加软件 PWM 压到 1/16 之下。 */
void tm1639_set_light(unsigned int light) {
    if (light <= 512) {
        unsigned char lvl = (unsigned char)(7 - (light * 7) / 512);
        if (lvl != cur_lvl || cur_pwm != 0) { tm1639_set_brightness(lvl); cur_lvl = lvl; }
        if (cur_pwm != 0) { tm1639_ultradim(0, PWM_STEPS); cur_pwm = 0; cur_on = PWM_STEPS; }
    } else {
        unsigned char on = (unsigned char)(PWM_STEPS - ((light - 512) * (PWM_STEPS - 1)) / 511);
        if (cur_lvl != 0) { tm1639_set_brightness(0); cur_lvl = 0; }
        if (cur_pwm != 1 || cur_on != on) { tm1639_ultradim(1, on); cur_pwm = 1; cur_on = on; }
    }
}

static void tm1639_pwm_init(void) {
    TMOD = (TMOD & 0x0F) | 0x01;     /* T0 模式1(16位); 复位默认 12T */
    TH0 = PWM_TH; TL0 = PWM_TL;
    TF0 = 0;
    ET0 = 1;                         /* 允许 T0 中断(初始 pwm_active=0, ISR 不动作) */
    EA = 1;
    TR0 = 1;
}

void tm1639_pwm_isr(void) __interrupt(1) {
    TH0 = PWM_TH; TL0 = PWM_TL;      /* 16 位模式手动重装 */
    TF0 = 0;
    if (pwm_active) {
        if (++pwm_phase >= PWM_STEPS) pwm_phase = 0;
        tm_set_display_enable(pwm_phase < pwm_on_steps);
    }
}
