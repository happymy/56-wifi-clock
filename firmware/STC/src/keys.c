#include "keys.h"

#define SCAN_MS       10      /* 名义节拍；实际由 main.c 主循环 delay_ms(10) 驱动 */
#define LONG_CNT      100     /* 1000ms / 10ms = 100 拍 */
#define DBL_CNT       15      /* 150ms / 10ms = 15 拍 */
#define QSIZE         6

/* 按键采样：UP=P3.2, SET=P3.3（per 原理图.md §1 / 新版时钟功能.md），active-low */
static unsigned char pressed(unsigned char btn) {
    if (btn == KEY_SET) return (unsigned char)(!(P3 & 0x08));   /* SET = P3.3 */
    return (unsigned char)(!(P3 & 0x04));                        /* UP  = P3.2 */
}

static __xdata unsigned char q[QSIZE][2];
static unsigned char q_head, q_tail, q_cnt;

static void emit(unsigned char btn, unsigned char ev) {
    if (q_cnt >= QSIZE) return;          /* 队列满丢弃(避免覆盖) */
    q[q_head][0] = btn; q[q_head][1] = ev;
    if (++q_head >= QSIZE) q_head = 0;
    q_cnt++;
}

/* st[0]=SET, st[1]=UP。字段全部 uchar：t_down/t_up 计扫描拍数(免 int 运算) */
typedef struct {
    unsigned char down;
    unsigned char t_down;
    unsigned char t_up;
    unsigned char long_fired;
    unsigned char dbl_pending;
} kst_t;
static __xdata kst_t st[2];

static unsigned char both_latch = 0;   /* 双键手势锁: 两键曾同按即置1, 两键都松才清(吞异步松手) */

void keys_init(void) {
    q_head = q_tail = q_cnt = 0;
    st[0].down = st[1].down = 0;
    st[0].t_down = st[1].t_down = 0;
    st[0].t_up = st[1].t_up = 0;
    st[0].long_fired = st[1].long_fired = 0;
    st[0].dbl_pending = st[1].dbl_pending = 0;
    both_latch = 0;
}

/* 处理单个键：cur=当前采样, s=该键状态(节拍 ~10ms，见 main.c)。
    单击: 松开后等 DBL_CNT(15) 拍无第二击→EV_SINGLE；双击: 第一击 pending 内再按下(第二击按下边沿)即发 EV_DOUBLE；
    长按: 按住达 LONG_CNT(100) 拍→EV_LONG(松开不再发单击/双击)。 */
static void scan_one(unsigned char cur, __xdata kst_t *s, unsigned char btn) {
    if (cur) {
        if (!s->down) {                       /* 按下边沿 */
            s->down = 1; s->t_down = 0; s->long_fired = 0;
            if (s->dbl_pending) { s->dbl_pending = 0; emit(btn, EV_DOUBLE); s->down = 0; } /* 第二击→双击, 假装已松防尾随单击 */
        }
        s->t_down++;
        if (!s->long_fired && s->t_down >= LONG_CNT) {
            emit(btn, EV_LONG); s->long_fired = 1;
        }
    } else {
        if (s->down) {                        /* 松开边沿 */
            s->down = 0; s->t_up = 0;
            if (s->long_fired) { s->long_fired = 0; return; }      /* 长按已处理 */
            s->dbl_pending = 1;                                   /* 第一击→待第二击 */
        } else {
            if (s->dbl_pending && ++s->t_up >= DBL_CNT) {
                emit(btn, EV_SINGLE); s->dbl_pending = 0;
            }
        }
    }
}

void keys_scan(void) {
    unsigned char p0 = pressed(KEY_SET);
    unsigned char p1 = pressed(KEY_UP);
    if (p0 && p1) {                       /* 双键同按: 启手势锁, 清单键状态防异步松手误发 */
        both_latch = 1;
        st[0].down = st[1].down = 0;
        st[0].dbl_pending = st[1].dbl_pending = 0;
        return;
    }
    if (both_latch) {                      /* 手势中(一先松): 继续吞, 两键都松才解除 */
        if (!p0 && !p1) both_latch = 0;
        return;
    }
    scan_one(p0, &st[0], KEY_SET);
    scan_one(p1, &st[1], KEY_UP);
}

unsigned char key_get(__xdata key_ev_t *e) {
    if (q_cnt == 0) return 0;
    e->btn = q[q_tail][0]; e->ev = q[q_tail][1];
    if (++q_tail >= QSIZE) q_tail = 0;
    q_cnt--;
    return 1;
}

unsigned char key_both_hold(void) { return both_latch; }   /* 手势进行中=两键曾同按且未都松 */
