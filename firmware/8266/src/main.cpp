#include "proto.h"
#include "store.h"
#include "countdown.h"
#include "wifi_task.h"
#include "web.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>

volatile bool s51_alive;   /* 收到 51 任意上行帧 → 确认在线（REQ_CFG 拉配置的前提） */

/* proto_on_frame：一帧完整到达。51 上行命令语义在此处理。 */
void proto_on_frame(uint8_t cmd, const uint8_t *p, uint8_t len) {
    s51_alive = true;   /* 51 活着（任意帧皆可） */
    switch (cmd) {
        case CMD_REQ_TIME:   /* 51 主动要时间 → 触发对时（伪待机唤醒 RF） */
            wifi_force_sync();
            break;
        case CMD_HEARTBEAT:  /* 51 心跳 → 回网络状态 */
            if (wifi_synced()) send_net_stat(STAT_SYNCED); else send_net_stat(0);
            break;
        case CMD_ENTER_AP:   /* 双键≥5s → 清凭据重启进 AP 配网 */
            wifi_ep_ap_mode();
            break;
        case CMD_CD_CTRL:    /* 倒计时控制 0=暂停/恢复 1=取消 */
            if (len >= 1) {
                if (p[0] == 1) cd_cancel();
                else           cd_pause_resume();
            }
            break;
        case CMD_SET_CFG:    /* REQ_CFG 应答：镜像以 51 为权威整帧覆盖 */
            if (len >= CFG_LEN) {
                memcpy(g_cfg, p, CFG_LEN);
                g_cfg_valid = true;
                store_save_cfg_blob();
            }
            break;
        /* SET_TIME/NET_STAT/STA_IP/REQ_CFG/BOOT 均 ESP→MCU 方向，51 不会发来 */
        default:
            break;
    }
}

void setup() {
    Serial.begin(9600);            /* UART1 与 51 唯一链路；伪待机期间保持在线 */
    proto_send_null(CMD_BOOT);     /* 首帧立即发：51 上电仅 2s 窗口（boot_t=8×250ms），
                                      等 loop 的 2s 周期再发必落窗口外 → 51 beep3 进调试模式 */
    store_init();
    store_load();                  /* 载入 WiFi 凭据 / 镜像备份（g_cfg_valid 仍 false，以 51 为准） */
    cd_set_preset(store_get_cd_min(), store_get_cd_sec()); /* 载入倒计时预设（默认 5 分） */
    send_disp_override(DO_MODE_FREE, 0, 0);  /* 上电清 51 残留 cd_disp（协议 §6.3：释放仅靠 mode0，防卡屏） */
    wifi_setup();
    bool ap = (WiFi.getMode() & WIFI_AP);
    web_setup(ap);   /* 模式注册占位；实际分流经 wifi_ap_active() 运行时判定 */
}

static unsigned long last_boot, last_pull, last_tick;

void loop() {
    /* 1) UART 逐字节喂协议机（9600 下每字节 ~1ms，轮询足够） */
    while (Serial.available()) proto_rx((uint8_t)Serial.read());

    /* 2) BOOT 握手：无条件每 2s 周期发（51 侧 esp_online 仅 BOOT 置位且幂等）。
          勿以 s51_alive 停发：51 心跳会使 8266 误判已确认而停发，但 51 晚于 8266
          上电或 51 重启后 esp_online 归零，收不到 BOOT 就永远拒绝 8266 下行帧
          （REQ_CFG 无应答 → g_cfg_valid 永 false → 链路全死）。周期发=自动恢复。 */
    if (millis() - last_boot >= 2000) {
        last_boot = millis();
        proto_send_null(CMD_BOOT);
    }

    /* 3) 镜像拉取：BOOT 后向 51 请求当前配置作底（SET_CFG 完整性铁律前置） */
    if (!g_cfg_valid && s51_alive && millis() - last_pull >= 3000) {
        last_pull = millis();
        proto_send_null(CMD_REQ_CFG);
    }

    /* 4) 1s 级调度：倒计时 tick / 伪待机对时 / 闲置断网 */
    if (millis() - last_tick >= 1000) {
        last_tick = millis();
        cd_tick();
        wifi_loop();
    }

    /* 5) Web（RF 工作时段服务，伪待机时 WiFi 已断自然无请求） */
    web_loop();
}