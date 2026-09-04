#include "wifi_task.h"
#include "proto.h"
#include "store.h"
#include <ESP8266WiFi.h>
#include <time.h>

#define AP_SSID "56dz network clock"
#define STA_TIMEOUT_MS 15000L
#define SYNC_WAIT_MS   10000L
#define IDLE_MS       300000L   /* STA 闲置断网阈值 5min：Web 访问会刷新计时（web.cpp wifi_touch），活跃配置期不断 */
#define SYNC_RETRY_MS  60000L   /* NTP 反复失败时 60s 节流，防 do_sync 阻塞占死主循环 */
#define LED_WIFI      2         /* 蓝 LED6 + 板载 LED 并联同驱动（active-low），方案A：AP 配网时亮、其余灭 */
/* NTP 服务器：国内优先，超时后整组换世界（协议 §6 NTP 列表失败递进） */
static const char *NTP_CN[]    = { "ntp.aliyun.com", "ntp.tencent.com", "cn.ntp.org.cn", nullptr };
static const char *NTP_WORLD[] = { "time.cloudflare.com", "pool.ntp.org", "ntp.aliyun.com", nullptr };

static bool synced;
static bool ap_mode;
static unsigned long last_rf_use;   /* 最近 RF 使用（STA 关联）时刻，0=从未 */

bool wifi_ap_active() { return ap_mode; }

void wifi_touch() { last_rf_use = millis(); }   /* Web 服务时刷新闲置计时：避免配置页被闲置断网打断 */

int wifi_tz_h() {
    /* 可信时区源优先级：① 51 拉到的镜像（g_cfg_valid）；② 本地备份（store_has_cfg，
       覆盖 8266 重启后 store_load 已恢复但 REQ_CFG 未回的时刻，避免把 51 已设 tz 覆盖回默认 8）；
       二者皆无才回默认 +8，防未初始化全 0 误判 tz=0。 */
    if (!g_cfg_valid && !store_has_cfg()) return 8;
    int8_t v = (int8_t)g_cfg[4];            /* §5 偏移4：tz 有符号原生（非 BCD） */
    return (v >= -12 && v <= 14) ? v : 8;
}

bool wifi_synced() { return synced; }

static uint8_t to_bcd(unsigned v) { return (uint8_t)((v / 10 << 4) | (v % 10)); }

static void push_set_time() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < (2016 - 1900)) return;        /* 时钟未被 SNTP 校正，放弃 */
    uint8_t b[8];
    b[0] = to_bcd(t.tm_year % 100);
    b[1] = to_bcd(t.tm_mon + 1);
    b[2] = to_bcd(t.tm_mday);
    b[3] = to_bcd(t.tm_wday); /* 星期 0-6: 0=周日 (C tm_wday 0=Sun..6=Sat) */
    b[4] = to_bcd(t.tm_hour);
    b[5] = to_bcd(t.tm_min);
    b[6] = to_bcd(t.tm_sec);
    b[7] = (uint8_t)(int8_t)wifi_tz_h();
    send_set_time(b);
    synced = true;
}

/* 开 AP 配网（真实模式无 STA 时）。
   用 WIFI_AP_STA 双模以便配网页可扫描周边网络（软 AP 固定信道 6，防 STA 扫描/连接
   拖走射频导致客户端掉线，见 esp8266/Arduino#817：单独射频被拖走时 softAP 信道跟随）。
   配网态不发起 STA 连接，故无非预期信道跳变。
   STA 扫描前置（scanNetworks 官方示例要求 STA 已启用并断开，否则扫描不触发返回空/list）：
   enableSTA(true)+disconnect() 确保 STA 接口就绪、处于未连接态，供配网页 h_root 扫描。 */
static void open_ap() {
    ap_mode = true;
    digitalWrite(LED_WIFI, LOW);      /* 蓝/板载 LED 亮：配网模式指示（active-low） */
    WiFi.mode(WIFI_AP_STA);
    WiFi.enableSTA(true);             /* 显式启用 STA 接口（不关软 AP），扫描前置 */
    WiFi.disconnect();                /* 确保处于未连接态，扫描才可触发（官方前置） */
    delay(100);
    WiFi.softAP(AP_SSID, store_get_ap_pwd(), 6);
}

/* 阻塞型流程：唤醒 RF → 连 STA → NTP 对时 → 推 SET_TIME → 保持 STA 服务 Web。
   最长阻塞 ~35s（15s STA + 2×10s NTP），期间主循环停、Serial 不消费：
   51 心跳/REQ_CFG 高频周期帧丢帧可容忍（会重发），CD_CTRL/REQ_TIME 稀有按键事件可重按。
   返回 true=已连过 STA（此时可服务 Web）；false=连不上（开 AP 等配网，协议 §7）。 */
static bool do_sync() {
    char ssid[33], pwd[65];
    if (!store_get_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd))) {
        if (!ap_mode) open_ap();
        return false;
    }

    WiFi.forceSleepWake();                        /* 支持 RF_DEFAULT 默认参 */
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.softAPdisconnect(true);
    WiFi.begin(ssid, pwd);
    unsigned long t0 = millis();
    while (millis() - t0 < STA_TIMEOUT_MS && WiFi.status() != WL_CONNECTED) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.forceSleepBegin();
        if (!ap_mode) open_ap();                  /* STA 连不上 → 开 AP 配网 */
        return false;
    }
    ap_mode = false;
    digitalWrite(LED_WIFI, HIGH);     /* 出 AP 配网态 → 蓝灯灭 */
    last_rf_use = millis();
    send_sta_ip(WiFi.localIP());                  /* 协议 §3: 配网/对时成功后向 51 报 IP */

    for (int round = 0; round < 2 && !synced; round++) {
        const char **srv = (round == 0) ? NTP_CN : NTP_WORLD;
        /* ponytail: 时区用整数偏移(秒)变体, DST 暂不叠加(中国无夏令时)；
           需求方接欧美时改用 configTime(POSIX TZ 字符串)变体 */
        configTime(wifi_tz_h() * 3600, 0, srv[0], srv[1], srv[2]);
        unsigned long w0 = millis();
        while (millis() - w0 < SYNC_WAIT_MS && time(nullptr) <= 1483228800L) delay(100);
    }
    if (time(nullptr) > 1483228800L) push_set_time();   /* 2017-01-01 UTC 起才可信 */
    /* 此处不断 RF：保持 STA 让 Web 配置页可达，闲置 5min（Web 访问刷新）由 wifi_loop 定时断回伪待机（协议 §6） */
    return true;
}

void wifi_force_sync() {
    /* 配网态（ap_mode）不响应 REQ_TIME：do_sync 内部 WiFi.mode(WIFI_STA) 会关掉软 AP，
       而 ap_mode 标志仍 true → 连 STA 失败后不重开 AP，wifi_loop:139 又永久 return，
       设备陷入既不能配网也不能联网的卡死态。AP 态下忽略对时请求最干净。 */
    if (ap_mode) return;
    do_sync();
}

void wifi_setup() {
    pinMode(LED_WIFI, OUTPUT);
    digitalWrite(LED_WIFI, HIGH);     /* 蓝/板载 LED 初始灭（伪待机）；仅 AP 配网时亮（方案A） */
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    char ssid[33], pwd[65];
    if (!store_get_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd))) {
        open_ap();                                    /* 未配网 / ENTER_AP 后：直接开 AP 等配网 */
        return;
    }
    WiFi.forceSleepBegin();                           /* 默认伪待机，首次对时由 wifi_loop 触发 */
    last_rf_use = 0;
}

void wifi_ep_ap_mode() {
    store_save_wifi("", "");                          /* 清 STA 凭据（协议 §3 ENTER_AP） */
    ESP.restart();
}

void wifi_loop() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last < 1000) return;                    /* 1s 调度粒度 */
    last = now;
    if (ap_mode) return;                              /* AP 配网由 web 层 handleClient */

    if (!synced) {
        /* NTP 失败时 do_sync 最长阻塞 ~35s（15s STA + 2×20s NTP），逐秒重试会占死主循环。
           只在 STA 未连接（伪待机断网态）才按 SYNC_RETRY_MS 节流重试唤醒：
           若 STA 已连上（do_sync 返回 true、WL_CONNECTED）则应保持 RF 服务 Web 并走下方闲置
           计时断回伪待机，而非每 60s forceSleepWake+重连——否则 do_sync:97 每次重置 last_rf_use，
           且唤醒抢占 RF，置闲 5min 判定永不触发，NTP 失败时 STA 永在线（原始 bug 根因）。 */
        static unsigned long retry_at = 0;
        if (retry_at == 0
            || (WiFi.status() != WL_CONNECTED && now - retry_at >= SYNC_RETRY_MS)) {
            retry_at = now;
            do_sync();
        }
        /* 未同步但 STA 在线也按闲置断回伪待机（协议 §6 伪待机是常态）：与 synced 分支(§179)同逻辑，
           Web 访问(web.cpp wifi_touch)刷新 last_rf_use 续期，无活动 5min 才断网休眠，
           配置页在 NTP 失败期间仍可达、断网后由上方 retry 唤醒。 */
        if ((WiFi.getMode() & WIFI_STA) && !ap_mode
            && last_rf_use && (now - last_rf_use) >= IDLE_MS) {
            WiFi.forceSleepBegin();
        }
        return;
    }
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    /* 每日 00:00/12:00 自定时对时（协议 §6）: 0点/12点为两个独立槽，各自每天触发一次 */
    if (tm.tm_year >= (2016 - 1900) && (tm.tm_hour == 0 || tm.tm_hour == 12)) {
        static int last_sync_key = -1;
        int key = (tm.tm_mday * 2) + (tm.tm_hour == 12);
        if (key != last_sync_key) {
            last_sync_key = key;
            do_sync();
        }
    }
    /* STA 闲置 5min 回伪待机（协议 §6；Web 访问由 wifi_touch 刷新计时） */
    if (last_rf_use && (now - last_rf_use) >= IDLE_MS && (WiFi.getMode() & WIFI_STA)) {
        WiFi.forceSleepBegin();
    }
}