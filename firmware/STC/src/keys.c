#include "keys.h"

#define SCAN_MS       20
#define LONG_CNT      50      /* 1000ms / 20ms = 50 拍 */
#define DBL_CNT       15      /* 300ms / 20ms = 15 拍 */
#define QSIZE         6

/* 按键采样：P3.2=UP, P3.3=SET，active-low（内部上拉=按下读0） */
static unsigned char pressed(unsigned char btn) {
    if (btn == KEY_SET) return (unsigned char)(!(P3 & 0x08));
    return (unsigned char)(!(P3 & 0x04));
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

static unsigned char both_hold = 0;

void keys_init(void) {
    q_head = q_tail = q_cnt = 0;
    st[0].down = st[1].down = 0;
    st[0].t_down = st[1].t_down = 0;
    st[0].t_up = st[1].t_up = 0;
    st[0].long_fired = st[1].long_fired = 0;
    st[0].dbl_pending = st[1].dbl_pending = 0;
    both_hold = 0;
}

/* 处理单个键：cur=当前采样, s=该键状态 */
static void scan_one(unsigned char cur, __xdata kst_t *s, unsigned char btn) {
    if (cur && !s->down) {               /* 按下边沿 */
        s->down = 1; s->t_down = 0; s->long_fired = 0;
    }
    if (cur) {
        s->t_down++;
        if (!s->long_fired && s->t_down >= LONG_CNT) {
            emit(btn, EV_LONG); s->long_fired = 1; s->dbl_pending = 0;
        }
    } else {
        if (s->down) {                   /* 松开边沿 */
            if (!s->long_fired) {
                if (s->t_up < DBL_CNT) { emit(btn, EV_DOUBLE); s->dbl_pending = 0; }
                else { s->dbl_pending = 1; }
            }
            s->down = 0; s->t_up = 0;
        } else {
            s->t_up++;
            if (s->dbl_pending && s->t_up >= DBL_CNT) {
                emit(btn, EV_SINGLE); s->dbl_pending = 0;
            }
        }
    }
}

void keys_scan(void) {
    unsigned char p0 = pressed(KEY_SET);
    unsigned char p1 = pressed(KEY_UP);
    if (p0 && p1) { both_hold = 1; return; }
    both_hold = 0;
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

unsigned char key_both_hold(void) { return both_hold; }
