#include "countdown.h"
#include "proto.h"

static uint8_t preset_min = 5;
static uint8_t preset_sec = 0;
static enum { CD_IDLE, CD_RUN, CD_PAUSE } st = CD_IDLE;
static uint32_t remain_s;
static bool ring_active;

void cd_set_preset(uint8_t m, uint8_t s) {
    if (m >= 1 && m <= 99) preset_min = m;
    if (s <= 59) preset_sec = s;
}

void cd_start() {
    /* 首帧立即推 MM:SS（十进制值字节，满时长如 2:45）；remain 从整秒计，
       首个 tick(1s 后) 减为 2:44，帧序不重复且总时长满 preset。归零走 tick→mode2 响铃。 */
    remain_s = (uint32_t)preset_min * 60 + preset_sec;
    st = CD_RUN; ring_active = false;
    send_disp_override(DO_MODE_CD, preset_min, preset_sec);
}

void cd_pause_resume() {
    if (st == CD_RUN) { st = CD_PAUSE; return; }
    if (st == CD_PAUSE) { st = CD_RUN; send_disp_override(DO_MODE_CD, remain_s / 60, remain_s % 60); return; }
    /* st==CD_IDLE：与 51 状态脱节（如归零响铃后用户单击 SET，或 8266 刚重启残留 cd_disp）
       回发 mode0 放权释放，避免 51 屏幕卡死在末帧倒计时 */
    send_disp_override(DO_MODE_FREE, 0, 0);
}

void cd_cancel() {
    st = CD_IDLE; ring_active = false;
    send_disp_override(DO_MODE_FREE, 0, 0);   /* 释放显示（幂等；即使非激活也回发，防 51 残留 cd_disp 卡死） */
}

bool cd_active() { return st != CD_IDLE; }

void cd_tick() {
    if (st == CD_RUN) {
        if (remain_s > 0) {
            remain_s--;
            if (remain_s == 0) {               /* 归零：先推末帧 00:00（显示停在 0000 而非前帧 00:01），再响铃；释放只靠取消→mode0（协议 §6.3/T11b） */
                send_disp_override(DO_MODE_CD, 0, 0);
                send_disp_override(DO_MODE_RING, 0, 0);
                st = CD_IDLE; ring_active = true;
            } else {
                send_disp_override(DO_MODE_CD, remain_s / 60, remain_s % 60);
            }
        }
    }
    /* ring_active 由 51 侧响铃约 58s（ring_ticks=240 @240ms/拍）自行结束；51 不回报，8266 不强制清 */
}