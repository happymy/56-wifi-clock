#include "proto.h"

uint8_t g_cfg[CFG_LEN];
volatile bool g_cfg_valid = false;

/* ---- 收帧状态机（与 51 uart_poll 同构） ---- */
static uint8_t f_st, f_cmd, f_len, f_chk, f_idx;
static uint8_t f_buf[64];

void proto_rx(uint8_t b) {
    switch (f_st) {
        case 0: if (b == 0xAA) f_st = 1; break;
        case 1: f_st = (b == 0x55) ? 2 : 0; break;
        case 2: f_cmd = b; f_st = 3; break;
        case 3: f_len = b; f_idx = 0; f_chk = f_cmd ^ f_len; f_st = (f_len == 0) ? 5 : (f_len <= sizeof(f_buf)) ? 4 : 0; break;
        case 4:
            if (f_idx < f_len) { f_buf[f_idx++] = b; f_chk ^= b; }
            if (f_idx == f_len) f_st = 5;
            break;
        case 5:
            if (b == f_chk) proto_on_frame(f_cmd, f_buf, f_len);
            f_st = 0;
            break;
    }
}

/* ---- 发送 ---- */
static unsigned long last_tx;
static const unsigned long TX_GAP_MS = 5;

static void tx(uint8_t b) { Serial.write(b); }

void proto_send(uint8_t cmd, const uint8_t *p, uint8_t len) {
    unsigned long now = millis();
    if (now - last_tx < TX_GAP_MS) delay(TX_GAP_MS - (now - last_tx));   /* 5 分钟级别告警阈值无关 */
    uint8_t chk = cmd ^ len;
    tx(0xAA); tx(0x55); tx(cmd); tx(len);
    for (uint8_t i = 0; i < len; i++) { tx(p[i]); chk ^= p[i]; }
    tx(chk);
    last_tx = millis();
}

void proto_send_null(uint8_t cmd) { proto_send(cmd, NULL, 0); }

void send_set_time(const uint8_t *bcd8) { proto_send(CMD_SET_TIME, bcd8, 8); }
void send_set_cfg()                   { proto_send(CMD_SET_CFG, g_cfg, CFG_LEN); }
void send_net_stat(uint8_t st)        { uint8_t v = st; proto_send(CMD_NET_STAT, &v, 1); }
void send_sta_ip(uint32_t ip) {
    uint8_t b[4]; b[0] = ip & 0xFF; b[1] = (ip >> 8) & 0xFF; b[2] = (ip >> 16) & 0xFF; b[3] = (ip >> 24) & 0xFF;
    proto_send(CMD_STA_IP, b, 4);
}
void send_ap_ready() { proto_send_null(CMD_AP_READY); }

void send_disp_override(uint8_t mode, uint8_t mm, uint8_t ss) {
    uint8_t b[3];
    if (mode == DO_MODE_FREE) { b[0] = DO_MODE_FREE; proto_send(CMD_DISP_OVERRIDE, b, 1); return; }  /* mode0=释放, 51 需 len>=1, 空帧会被丢 */
    if (mode == DO_MODE_CD)   { b[0] = DO_MODE_CD; b[1] = mm; b[2] = ss; proto_send(CMD_DISP_OVERRIDE, b, 3); return; }
    b[0] = DO_MODE_RING; proto_send(CMD_DISP_OVERRIDE, b, 1);
}