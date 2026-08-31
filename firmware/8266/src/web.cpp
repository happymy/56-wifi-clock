#include "web.h"
#include "proto.h"
#include "store.h"
#include "countdown.h"
#include "wifi_task.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <stdio.h>

static ESP8266WebServer srv(80);

#define HT "text/html; charset=utf-8"

/* ---- AP 配网页：只填 WiFi 账号（两页分离铁律） ---- */
static const char PAGE_AP_FIRST[] PROGMEM =
    "<meta charset=utf-8><title>56dz 时钟配网</title><style>"
    "b.fail{color:#c00}.hint{color:#666;font-size:13px}"
    "</style><h2>WiFi 时钟配网</h2>";

static const char PAGE_AP_FAIL[] PROGMEM =
    "<p><b class=fail>上次配网没有成功，请仔细检查下面两项再试：</b>"
    "<br>① WiFi 名称完全一致（区分大小写，且是 2.4GHz 网络）"
    "<br>② 密码正确（注意大小写与特殊字符）</p>";

static const char PAGE_AP_FORM[] PROGMEM =
    "<form method=post action=/wifi>"
    "WiFi 名称：<input name=ssid required autofocus><br>"
    "WiFi 密码：<input name=pwd type=password><br>"
    "<button>连接</button></form>"
    "<p class=hint>只能连接 2.4GHz 网络。保存后本热点会关闭，时钟将在 30 秒内连接并对时；"
    "若连接失败，本热点会自动重新打开，请回此页重试。</p>";

static const char PAGE_AP_OK[] PROGMEM =
    "<meta charset=utf-8><title>成功</title><h2>已保存，尝试连接…</h2>"
    "<p>本热点即将关闭；时钟会在 30 秒内连上 WiFi 并对时。</p>"
    "<p>若连接失败，本热点会自动重新打开，请重新连接并返回配网页检查。</p>"
    "<p>如何查看时钟 IP：在时钟上<b>双击「SET」键</b>，大屏会显示 P + IP 末段（如 P168）；"
    "用浏览器访问该地址即可打开设置页。</p>";

static void h_ap_wifi() {
    if (!srv.hasArg("ssid") || srv.arg("ssid").length() == 0) { srv.send_P(404, HT, PSTR("<h2>404</h2>")); return; }
    store_save_wifi(srv.arg("ssid").c_str(), srv.arg("pwd").c_str());
    srv.send_P(200, HT, PAGE_AP_OK);
    delay(1000);
    ESP.restart();    /* 重启后无 AP 凭据路径已改由 store 判定，正常走 STA */
}

/* ---- STA 配置页：全功能单页（默认值回填） ---- */
static const char HDR[] PROGMEM =
    "<meta charset=utf-8><title>56dz 时钟</title><style>"
    "td{padding:4px}tr td:first-child{text-align:right}select{min-width:5em}"
    "p.hint{color:#666;font-size:13px}"
    "</style><h2>WiFi 时钟设置</h2>"
    "<p class=hint>重配网：按住「SET」+「UP」两键 5 秒，将清除网络设置并重开配网热点。"
    "<br>查看本机 IP：双击「SET」键，大屏显示 P + IP 末段（如 P168）。</p>"
    "<form method=post action=/save><table>";

static const char FTR[] PROGMEM =
    "</table><br><button>保存</button>"
    " <a href=/cd style=margin-left:12px>倒计时</a></form>";

/* 流式行输出 */
static void row(const char *label) {
    srv.sendContent("<tr><td>"); srv.sendContent(label); srv.sendContent("</td><td>");
}
static void row_end() { srv.sendContent("</td></tr>"); }

/* 数字下拉：lo..hi，回填 cur */
static void sel_num(const char *name, long cur, long lo, long hi) {
    char b[48];
    snprintf_P(b, sizeof(b), PSTR("<select name=%s>"), name);
    srv.sendContent(b);
    for (long i = lo; i <= hi; i++) {
        snprintf_P(b, sizeof(b), i == cur ? PSTR("<option value=%ld selected>%ld</option>") : PSTR("<option value=%ld>%ld</option>"), i, i);
        srv.sendContent(b);
    }
    srv.sendContent("</select>");
}

/* 两档单选：0/1 带自然语言标签 */
static void radio2(const char *name, long cur, const char *l0, const char *l1) {
    char b[96];
    snprintf_P(b, sizeof(b), cur == 0 ? PSTR("<label><input type=radio name=%s value=0 checked>%s</label>") : PSTR("<label><input type=radio name=%s value=0>%s</label>"), name, l0);
    srv.sendContent(b);
    snprintf_P(b, sizeof(b), cur != 0 ? PSTR("<label><input type=radio name=%s value=1 checked>%s</label>") : PSTR("<label><input type=radio name=%s value=1>%s</label>"), name, l1);
    srv.sendContent(b);
}

/* 复选（开关） */
static void chk(const char *name, bool on, const char *label) {
    char b[96];
    snprintf_P(b, sizeof(b), on ? PSTR("<label><input type=checkbox name=%s checked>%s</label>") : PSTR("<label><input type=checkbox name=%s>%s</label>"), name, label);
    srv.sendContent(b);
}

static unsigned bcd2dec(unsigned v) { return (v >> 4) * 10 + (v & 0x0F); }
static unsigned dec2bcd(unsigned v) { return ((v / 10) << 4) | (v % 10); }

static void h_sta_root() {
    /* chunked 流式响应的正确开场：必须先声明长度未定并发出 200 空串，
       否则 sendContent 只有正文、没有响应头（ESP8266WebServer-impl.h 要求） */
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(HDR);

    /* 大屏主模式 */
    row("大屏显示"); radio2("display_mode", g_cfg[0], "固定时间", "自动轮换"); row_end();

    /* 亮度 */
    row("亮度模式"); radio2("bright_mode", g_cfg[1], "自动(光敏)", "手动"); row_end();
    row("手动亮度档"); sel_num("bright_lvl", g_cfg[2], 1, 8); row_end();

    /* 温度单位 + 补偿 */
    row("温度单位"); radio2("temp_unit", g_cfg[53], "摄氏 °C", "华氏 °F"); row_end();
    row("温度补偿"); {
        char b[96];
        /* 拆方向+数值两下拉，杜绝自由输入 */
        long off = (signed char)g_cfg[3];
        long dir = off < 0 ? 1 : 0;
        long av = off < 0 ? -off : off;
        snprintf_P(b, sizeof(b), PSTR("<select name=temp_offset_dir>"));
        srv.sendContent(b);
        snprintf_P(b, sizeof(b), dir == 0 ? PSTR("<option value=0 selected>偏低</option><option value=1>偏高</option>") : PSTR("<option value=0>偏低</option><option value=1 selected>偏高</option>"));
        srv.sendContent(b);
        srv.sendContent("</select>");
        sel_num("temp_offset_v", av, 0, 99);
        srv.sendContent(" 度");
    } row_end();

    /* 3 组闹钟：开关(独立 name) + 时/分下拉 单行 */
    for (int n = 0; n < 3; n++) {
        char nm[16], b[64];
        snprintf_P(b, sizeof(b), PSTR("闹钟%d%s"), n + 1, n == 0 ? "(主)" : n == 1 ? "(备用)" : "");
        row(b);
        snprintf_P(nm, sizeof(nm), PSTR("a%d_on"), n);
        chk(nm, g_cfg[4 + n * 3] != 0, "");
        snprintf_P(nm, sizeof(nm), PSTR("a%d_hh"), n);
        sel_num(nm, bcd2dec(g_cfg[4 + n * 3 + 1]), 0, 23);
        srv.sendContent(" 时 ");
        snprintf_P(nm, sizeof(nm), PSTR("a%d_mm"), n);
        sel_num(nm, bcd2dec(g_cfg[4 + n * 3 + 2]), 0, 59);
        srv.sendContent(" 分");
        row_end();
    }

    /* 时区 */
    row("时区(UTC)"); sel_num("tz", (signed char)g_cfg[13], -12, 14); row_end();

    /* SMG1 选显 */
    row("下排小屏显示"); radio2("smg1_mode", g_cfg[21], "温度", "日期"); row_end();

    /* 贪睡：仅 0(关)/5/10 三档 */
    row("贪睡时长"); {
        char b[64];
        long sn = g_cfg[19];
        if (sn != 0 && sn != 5 && sn != 10) sn = 0;
        srv.sendContent("<select name=snooze>");
        snprintf_P(b, sizeof(b), sn == 0 ? PSTR("<option value=0 selected>关闭</option>") : PSTR("<option value=0>关闭</option>"));
        srv.sendContent(b);
        snprintf_P(b, sizeof(b), sn == 5 ? PSTR("<option value=5 selected>5 分钟</option>") : PSTR("<option value=5>5 分钟</option>"));
        srv.sendContent(b);
        snprintf_P(b, sizeof(b), sn == 10 ? PSTR("<option value=10 selected>10 分钟</option>") : PSTR("<option value=10>10 分钟</option>"));
        srv.sendContent(b);
        srv.sendContent("</select>");
    } row_end();

    /* 状态灯（红 LED_T，由 51 驱动，联网同步后点亮） */
    row("状态灯(红色)"); chk("led_en", g_cfg[20] != 0, "亮灯(联网时)"); row_end();

    /* 关屏时段：启用开关 + 起止时/分分组 */
    {
        unsigned sh = g_cfg[14], sm = g_cfg[15], eh = g_cfg[16], em = g_cfg[17];
        bool en = (sh != 0xFF && eh != 0xFF);
        row("关屏时段"); chk("off_enable", en, "启用 (到点熄灭大屏)"); row_end();
        row("  开始"); sel_num("off_s", sh == 0xFF ? 0 : sh, 0, 23); srv.sendContent(" 时 ");
                     sel_num("off_sm", sm == 0xFF ? 0 : sm, 0, 59); srv.sendContent(" 分"); row_end();
        row("  结束"); sel_num("off_e", eh == 0xFF ? 0 : eh, 0, 23); srv.sendContent(" 时 ");
                     sel_num("off_em", em == 0xFF ? 0 : em, 0, 59); srv.sendContent(" 分"); row_end();
    }

    srv.sendContent(FTR);
}

/* 表单值解析：缺省/越界回落 dflt */
static long pg(const char *name, long dflt, long lo, long hi) {
    if (srv.hasArg(name)) {
        long v = strtol(srv.arg(name).c_str(), nullptr, 10);
        if (v >= lo && v <= hi) return v;
    }
    return dflt;
}

static void h_sta_save() {
    g_cfg[0] = (uint8_t)pg("display_mode", g_cfg[0], 0, 1);
    g_cfg[1] = (uint8_t)pg("bright_mode", g_cfg[1], 0, 1);
    g_cfg[2] = (uint8_t)pg("bright_lvl", g_cfg[2], 1, 8);
    g_cfg[53] = (uint8_t)pg("temp_unit", g_cfg[53], 0, 1);
    g_cfg[21] = (uint8_t)pg("smg1_mode", g_cfg[21], 0, 1);
    long sn = pg("snooze", g_cfg[19], 0, 10); g_cfg[19] = (uint8_t)((sn == 5 || sn == 10) ? sn : 0);
    g_cfg[20] = (uint8_t)(srv.hasArg("led_en") ? 1 : 0);

    /* 温度补偿：方向(偏低/偏高) × 数值 0-99 → 有符号 */
    long dir = pg("temp_offset_dir", 0, 0, 1);
    long v = pg("temp_offset_v", 0, 0, 99);
    g_cfg[3] = (uint8_t)(signed char)(dir ? -v : v);

    /* 闹钟：开关(独立 name) + 时/分下拉(十进制) → BCD 下发（§5 铁律） */
    for (int n = 0; n < 3; n++) {
        char nm[16];
        snprintf_P(nm, sizeof(nm), PSTR("a%d_on"), n);
        g_cfg[4 + n * 3] = (uint8_t)(srv.hasArg(nm) ? 1 : 0);
        snprintf_P(nm, sizeof(nm), PSTR("a%d_hh"), n);
        long hh = pg(nm, bcd2dec(g_cfg[4 + n * 3 + 1]), 0, 23);
        snprintf_P(nm, sizeof(nm), PSTR("a%d_mm"), n);
        long mm = pg(nm, bcd2dec(g_cfg[4 + n * 3 + 2]), 0, 59);
        g_cfg[4 + n * 3 + 1] = (uint8_t)dec2bcd(hh);
        g_cfg[4 + n * 3 + 2] = (uint8_t)dec2bcd(mm);
    }

    /* 关屏时段：开=用下拉值；关=0xFF 禁用 */
    if (srv.hasArg("off_enable")) {
        g_cfg[14] = (uint8_t)pg("off_s", 0, 0, 23);
        g_cfg[15] = (uint8_t)pg("off_sm", 0, 0, 59);
        g_cfg[16] = (uint8_t)pg("off_e", 0, 0, 23);
        g_cfg[17] = (uint8_t)pg("off_em", 0, 0, 59);
    } else {
        g_cfg[14] = g_cfg[15] = g_cfg[16] = g_cfg[17] = 0xFF;
    }

    if (g_cfg_valid) {
        send_set_cfg();               /* 完整 54B 下发（整帧覆盖，铁律） */
        store_save_cfg_blob();
    }
    srv.send_P(200, HT, PSTR("<meta charset=utf-8><title>已保存</title><h2>已保存</h2><p>配置已下推时钟。</p><p><a href=/>返回</a></p>"));
}

/* ---- 倒计时页（Web 侧管理，驱动协议帧） ---- */
static void h_cd() {
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(PSTR("<meta charset=utf-8><title>倒计时</title><h2>倒计时</h2>"
        "<form method=post action=/cdstart>时长："));
    sel_num("min", store_get_cd_min(), 1, 99);
    srv.sendContent(" 分 ");
    sel_num("sec", store_get_cd_sec(), 0, 59);
    srv.sendContent(" 秒<button style=margin-left:12px>开始</button></form>"
        "<p><a href=/cdpause>暂停/继续</a> | <a href=/cdcancel>取消</a></p>"
        "<p><a href=/>返回设置</a></p>");
}

static void h_cd_start() {
    long m = pg("min", 1, 1, 99);
    long s = pg("sec", 0, 0, 59);
    cd_set_preset((uint8_t)m, (uint8_t)s);
    store_save_cd((uint8_t)m, (uint8_t)s);
    cd_start();
    srv.send_P(200, HT, PSTR("<meta charset=utf-8><title>OK</title><h2>倒计时已开始</h2><a href=/cd>返回</a>"));
}
static void h_cd_pause() { cd_pause_resume(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }
static void h_cd_cancel() { cd_cancel(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }

/* 根页面：按当前模式分流——AP 配网页（仅 WiFi 账号） / STA 全功能页（两页分离铁律） */
static void h_root() {
    if (wifi_ap_active()) {
        srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
        srv.send(200, HT, "");
        srv.sendContent(PAGE_AP_FIRST);
        /* 有已存凭据却仍开 AP = 上次尝试连接失败（否则不会进 AP），据此提醒用户检查 */
        char ssid[33], pwd[65];
        if (store_get_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd)))
            srv.sendContent(PAGE_AP_FAIL);
        srv.sendContent(PAGE_AP_FORM);
    } else {
        h_sta_root();
    }
}

static void h_404() { srv.send_P(404, HT, PSTR("<h2>404</h2>")); }

void web_setup(bool ap) {
    (void)ap;   /* 模式运行时判定，见 h_root */
    srv.on("/", HTTP_GET, h_root);
    srv.on("/wifi", HTTP_POST, h_ap_wifi);   /* 配网页：仅 WiFi 账号，仅 POST（GET 访问不得清配置） */
    srv.on("/save", HTTP_POST, h_sta_save);  /* STA 全功能页 */
    srv.on("/cd", h_cd);
    srv.on("/cdstart", HTTP_POST, h_cd_start);
    srv.on("/cdpause", h_cd_pause);
    srv.on("/cdcancel", h_cd_cancel);
    srv.onNotFound(h_404);
    srv.begin();
}

void web_loop() {
    if (wifi_ap_active() || WiFi.isConnected()) {
        wifi_touch();                                   /* 服务 Web=RF 活跃：刷新闲置计时，配置页长停留不被打断 */
        srv.handleClient();
    }
    /* 伪待机（RF 关/未关联）时无网可服务；仅 STA 关联或 AP 开启时轮询 */
}