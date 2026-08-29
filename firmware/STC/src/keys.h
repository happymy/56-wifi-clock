#ifndef KEYS_H
#define KEYS_H

#include "stc15.h"

#define KEY_SET 0
#define KEY_UP  1

#define EV_NONE   0
#define EV_SINGLE 1   /* 单击 */
#define EV_DOUBLE 2   /* 双击 */
#define EV_LONG   3   /* 长按(>1s) */

typedef struct { unsigned char btn; unsigned char ev; } key_ev_t;

void keys_init(void);
void keys_scan(void);                 /* 每 ~20ms 调用一次 */
unsigned char key_get(__xdata key_ev_t *e);  /* 取一个事件，有则返回 1 */
unsigned char key_both_hold(void);   /* 当前两键是否同按(长按判定用) */

#endif
