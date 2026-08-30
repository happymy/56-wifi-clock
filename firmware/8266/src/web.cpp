#include "web.h"
#include "proto.h"
#include "store.h"
#include "countdown.h"
#include "wifi_task.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

static ESP8266WebServer srv(80);

#define HT "text/html; charset=utf-8"

/* ---- AP 配网页：只填 WiFi 账号（两页分离铁律） ---- */
static const char PAGE_AP[] PROGMEM =
    "<meta charset=utf-8><title>56dz 时钟配网</title><h2>WiFi 时钟配网</h2>"
    "<form method=post action=/wifi>"
    "WiFi 名称：<input name=ssid required autofocus><br>"
    "WiFi 密码：<input name=pwd type=password><br>"
    "<button>连接</button></form>";

static const char PAGE_AP_OK[] PROGMEM =
    "<meta charset=utf-8><title>成功</title><h2>已保存，尝试连接…</h2>"
    "<p>本热点即将关闭；时钟会在 30 秒内连上 WiFi 并对时。</p>";

static void h_ap_wifi() {
    store_save_wifi(srv.arg("ssid").c_str(), srv.arg("pwd").c_str());
    srv.send_P(200, HT, PAGE_AP_OK);
    delay(1000);
    ESP.restart();    /* 重启后无 AP 凭据路径已改由 store 判定，正常走 STA */
}

/* ---- STA 配置页：全功能单页（默认值回填） ---- */
static const char HDR[] PROGMEM =
    "<meta charset=utf-8><title>56dz 时钟</title><style>"
    "input{width:9em;padding:4px}td{padding:3px}tr td:first-child{width:12em}</style>"
    "<h2>WiFi 时钟设置</h2>"
    "<form method=post action=/save><table>";

static const char FTR[] PROGMEM =
    "</table><br><button>保存</button>"
    " <a href=/cd style=margin-left:12px>倒计时</a></form>";

static void row(const char *label, const char *name, long val) {
    char b[48];   /* 最长 "<input name=display_mode value=255>"=35B，24 会截断 value 回填 */
    srv.sendContent("<tr><td>"); srv.sendContent(label); srv.sendContent("</td><td>");
    snprintf_P(b, sizeof(b), PSTR("<input name=%s value=%ld>"), name, val);
    srv.sendContent(b); srv.sendContent("</td></tr>");
}

static unsigned bcd2dec(unsigned v) { return (v >> 4) * 10 + (v & 0x0F); }
static unsigned dec2bcd(unsigned v) { return ((v / 10) << 4) | (v % 10); }

static void h_sta_root() {
    /* chunked 流式响应的正确开场：必须先声明长度未定并发出 200 空串，
       否则 sendContent 只有正文、没有响应头（ESP8266WebServer-impl.h 要求） */
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(HDR);

    row("大屏模式(0固定/1轮换)", "display_mode", g_cfg[0]);
    row("亮度模式(0自动/1手动)", "bright_mode", g_cfg[1]);
    row("亮度档(1-8)", "bright_lvl", g_cfg[2]);
    row("温度补偿(°C)", "temp_offset", (signed char)g_cfg[3]);
    row("温度单位(0°C/1°F)", "temp_unit", g_cfg[53]);

    for (int n = 0; n < 3; n++) {
        char f[16], b[48];
        snprintf_P(f, sizeof(f), PSTR("闹钟%d开(0/1)"), n + 1); snprintf_P(b, sizeof(b), PSTR("a%d_on"), n);
        row(f, b, g_cfg[4 + n * 3]);
        snprintf_P(f, sizeof(f), PSTR("闹钟%d时"), n + 1);   snprintf_P(b, sizeof(b), PSTR("a%d_hh"), n);
        row(f, b, bcd2dec(g_cfg[4 + n * 3 + 1]));
        snprintf_P(f, sizeof(f), PSTR("闹钟%d分"), n + 1);   snprintf_P(b, sizeof(b), PSTR("a%d_mm"), n);
        row(f, b, bcd2dec(g_cfg[4 + n * 3 + 2]));
    }

    row("时区(UTC+8=8)", "tz", (signed char)g_cfg[13]);
    row("SMG1(0温度/1日期)", "smg1_mode", g_cfg[21]);
    row("贪睡(0/5/10)", "snooze", g_cfg[19]);
    row("状态灯(1开/0关)", "led_en", g_cfg[20]);
    row("关屏时(255=禁用)", "off_s", g_cfg[14]);
    row("关屏分", "off_sm", g_cfg[15]);
    row("开屏时(255=禁用)", "off_e", g_cfg[16]);
    row("开屏分", "off_em", g_cfg[17]);

    srv.sendContent(FTR);
}

static unsigned pg(const char *name, unsigned dflt) {
    if (srv.hasArg(name)) {
        long v = strtol(srv.arg(name).c_str(), nullptr, 10);
        if (v >= 0 && v <= 255) return (unsigned)v;
    }
    return dflt;
}

static void h_sta_save() {
    g_cfg[0] = (uint8_t)pg("display_mode", g_cfg[0]);
    g_cfg[1] = (uint8_t)pg("bright_mode", g_cfg[1]);   /* 0=自动 1=手动 */
    unsigned lv = pg("bright_lvl", g_cfg[2]); if (lv >= 1 && lv <= 8) g_cfg[2] = (uint8_t)lv;
    long to = strtol(srv.arg("temp_offset").c_str(), nullptr, 10);
    if (to >= -99 && to <= 99) g_cfg[3] = (uint8_t)(signed char)to;   /* §六: 参考温度单点标定 offset ±99°C */
    g_cfg[19] = (uint8_t)pg("snooze", g_cfg[19]);
    long tz = strtol(srv.arg("tz").c_str(), nullptr, 10);
    if (tz >= -12 && tz <= 14) g_cfg[13] = (uint8_t)(signed char)tz;
    g_cfg[21] = (uint8_t)pg("smg1_mode", g_cfg[21]);
    g_cfg[20] = (uint8_t)pg("led_en", g_cfg[20]);
    g_cfg[14] = (uint8_t)pg("off_s", g_cfg[14]);
    g_cfg[15] = (uint8_t)pg("off_sm", g_cfg[15]);
    g_cfg[16] = (uint8_t)pg("off_e", g_cfg[16]);
    g_cfg[17] = (uint8_t)pg("off_em", g_cfg[17]);

    long tu = pg("temp_unit", g_cfg[53]);
    g_cfg[53] = (uint8_t)(tu ? 1 : 0);

    /* 闹钟：时/分用 BCD 下发，回显用 decimal（§5 铁律） */
    for (int n = 0; n < 3; n++) {
        char f[16];
        snprintf_P(f, sizeof(f), PSTR("a%d_on"), n);
        g_cfg[4 + n * 3] = pg(f, g_cfg[4 + n * 3]) ? 1 : 0;
        snprintf_P(f, sizeof(f), PSTR("a%d_hh"), n);
        long hh = (long)dec2bcd(pg(f, bcd2dec(g_cfg[4 + n * 3 + 1])));
        g_cfg[4 + n * 3 + 1] = (uint8_t)hh;
        snprintf_P(f, sizeof(f), PSTR("a%d_mm"), n);
        long mm = (long)dec2bcd(pg(f, bcd2dec(g_cfg[4 + n * 3 + 2])));
        g_cfg[4 + n * 3 + 2] = (uint8_t)mm;
    }

    if (g_cfg_valid) {
        send_set_cfg();               /* 完整 54B 下发（整帧覆盖，铁律） */
        store_save_cfg_blob();
    }
    srv.send_P(200, HT, PSTR("<meta charset=utf-8><title>已保存</title><h2>已保存</h2><p>配置已下推时钟。</p><p><a href=/>返回</a></p>"));
}

/* ---- 倒计时页（Web 侧管理，驱动协议帧） ---- */
static const char PAGE_CD[] PROGMEM =
    "<meta charset=utf-8><title>倒计时</title>"
    "<h2>倒计时</h2>"
    "<form method=post action=/cdstart>"
    "时长(分, 1-99)：<input name=min size=4 required><br>"
    "<button>开始</button></form>"
    "<p><a href=/cd>暂停/继续</a> | <a href=/cdcancel>取消</a></p>"
    "<p><a href=/>返回设置</a></p>";

static void h_cd() { srv.send_P(200, HT, PAGE_CD); }
static void h_cd_start() {
    long m = strtol(srv.arg("min").c_str(), nullptr, 10);
    if (m >= 1 && m <= 99) { cd_set_preset((uint8_t)m); store_save_cd((uint8_t)m); cd_start(); }
    srv.send_P(200, HT, PSTR("<meta charset=utf-8><title>OK</title><h2>倒计时已开始</h2><a href=/cd>返回</a>"));
}
static void h_cd_pause() { cd_pause_resume(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }
static void h_cd_cancel() { cd_cancel(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }

/* 根页面：按当前模式分流——AP 配网页（仅 WiFi 账号） / STA 全功能页（两页分离铁律） */
static void h_root() {
    if (wifi_ap_active()) srv.send_P(200, HT, PAGE_AP);
    else                  h_sta_root();
}

static void h_404() { srv.send_P(404, HT, PSTR("<h2>404</h2>")); }

void web_setup(bool ap) {
    (void)ap;   /* 模式运行时判定，见 h_root */
    srv.on("/", HTTP_GET, h_root);
    srv.on("/wifi", h_ap_wifi);              /* 配网页：仅 WiFi 账号 */
    srv.on("/save", HTTP_POST, h_sta_save);  /* STA 全功能页 */
    srv.on("/cd", h_cd);
    srv.on("/cdstart", HTTP_POST, h_cd_start);
    srv.on("/cdpause", h_cd_pause);
    srv.on("/cdcancel", h_cd_cancel);
    srv.onNotFound(h_404);
    srv.begin();
}

void web_loop() {
    if (wifi_ap_active() || WiFi.isConnected()) srv.handleClient();
    /* 伪待机（RF 关/未关联）时无网可服务；仅 STA 关联或 AP 开启时轮询 */
}