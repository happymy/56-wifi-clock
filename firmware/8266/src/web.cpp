#include "web.h"
#include "proto.h"
#include "store.h"
#include "countdown.h"
#include "wifi_task.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static ESP8266WebServer srv(80);

#define HT "text/html; charset=utf-8"

/* HTML 转义 &<>"'：SSID 来自周边扫描（可被伪造），嵌入页面属性/文本前必须转义防注入 */
static size_t esc(const char *in, char *out, size_t ocap) {
    size_t o = 0;
    for (; *in && o + 6 < ocap; in++) {
        switch (*in) {
            case '&':  o += (size_t)snprintf_P(out + o, ocap - o, PSTR("&amp;"));   break;
            case '<':  o += (size_t)snprintf_P(out + o, ocap - o, PSTR("&lt;"));    break;
            case '>':  o += (size_t)snprintf_P(out + o, ocap - o, PSTR("&gt;"));    break;
            case '"':  o += (size_t)snprintf_P(out + o, ocap - o, PSTR("&quot;"));  break;
            case '\'': o += (size_t)snprintf_P(out + o, ocap - o, PSTR("&#39;"));   break;
            default:   out[o++] = *in;
        }
    }
    out[o] = 0;
    return o;
}

/* ---- AP 配网页：只填 WiFi 账号（两页分离铁律）；样式用共享 CSS_STA ---- */
static const char PAGE_AP_FIRST[] PROGMEM =
    "<meta charset=utf-8><title>56dz 时钟配网</title>"
    "<h2>WiFi 时钟配网</h2>";

static const char PAGE_AP_FAIL[] PROGMEM =
    "<p><b class=fail>上次配网没有成功，请仔细检查下面两项再试：</b>"
    "<br>① WiFi 名称完全一致（区分大小写，且是 2.4GHz 网络）"
    "<br>② 密码正确（注意大小写与特殊字符）</p>";

static const char PAGE_AP_FORM_HEAD[] PROGMEM =
    "<form method=post action=/wifi>"
    "<label>WiFi 名称</label><input name=ssid required autofocus>"
    "<div id=wlList>";

static const char PAGE_AP_FORM_TAIL[] PROGMEM =
    "</div>"
    "<label>WiFi 密码</label><input name=pwd type=password><br><br>"
    "<button type=submit>连接</button> <button type=button onclick=location.reload()>刷新列表</button></form>"
    "<script>function setWifi(v){document.getElementsByName('ssid')[0].value=v;}</script>"
    "<p class=hint>只能连接 2.4GHz 网络。保存后本热点会关闭，时钟将在 30 秒内连接并对时；"
    "若连接失败，本热点会自动重新打开，请回此页重试。</p>";

static const char PAGE_AP_OK[] PROGMEM =
    "<meta charset=utf-8><title>成功</title><h2>已保存，尝试连接…</h2>"
    "<p>本热点即将关闭；时钟会在 30 秒内连上 WiFi 并对时。</p>"
    "<p>若连接失败，本热点会自动重新打开，请重新连接并返回配网页检查。</p>"
    "<p>如何查看时钟 IP：在时钟上<b>双击「SET」键</b>，大屏会显示 P + IP 末段（如 P168）；"
    "用浏览器访问该地址即可打开设置页。</p>";

static void h_ap_wifi() {
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（配网提交） */
    if (!srv.hasArg("ssid") || srv.arg("ssid").length() == 0) { srv.send_P(404, HT, PSTR("<h2>404</h2>")); return; }
    store_save_wifi(srv.arg("ssid").c_str(), srv.arg("pwd").c_str());
    srv.send_P(200, HT, PAGE_AP_OK);
    delay(1000);
    ESP.restart();    /* 重启后无 AP 凭据路径已改由 store 判定，正常走 STA */
}

/* ---- 共享浅色现代样式（AP 配网页/STA 配置页/倒计时页三页统一） ---- */
static const char CSS_STA[] PROGMEM =
    "<meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:-apple-system,'Segoe UI',Roboto,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;color:#222;line-height:1.5}"
    "h2{font-size:20px;margin:8px 0 16px}"
    "h3.sec{font-size:13px;color:#06c;margin:18px 0 6px;font-weight:600;letter-spacing:.5px}"
    ".card{background:#fff;border:1px solid #e3e3e3;border-radius:10px;overflow:hidden;margin-bottom:6px}"
    ".row{display:flex;align-items:center;gap:10px;padding:9px 12px;border-bottom:1px solid #f0f0f0}"
    ".row:last-child{border-bottom:none}"
    ".row .lbl{flex:0 0 auto;min-width:88px;font-size:14px;color:#444}"
    ".row .ctl{flex:1 1 auto;display:flex;flex-wrap:wrap;align-items:center;gap:6px}"
    "select{padding:6px 8px;font-size:14px;border:1px solid #bbb;border-radius:6px;background:#fff;color:#222}"
    "select:focus{outline:none;border-color:#06c;box-shadow:0 0 0 2px rgba(0,102,204,.15)}"
    "input[type=radio],input[type=checkbox]{accent-color:#06c;width:17px;height:17px;vertical-align:-3px}"
    "label.opt{display:flex;align-items:center;gap:4px;font-size:14px;color:#333;cursor:pointer;margin:0}"
    ".hint{color:#666;font-size:12px;margin-top:2px}"
    ".banner{background:#eef4ff;border:1px solid #c9dcff;border-radius:8px;padding:8px 12px;font-size:13px;color:#333;margin-bottom:12px}"
    ".ok{background:#e7f6ec;border:1px solid #b7e3c4;border-radius:8px;padding:10px 12px;color:#1a7a37;font-size:14px;margin-bottom:12px}"
    ".warn{background:#fff5e6;border:1px solid #ffd9a0;border-radius:8px;padding:10px 12px;color:#9a5b00;font-size:14px;margin-bottom:12px}"
    "button{padding:9px 22px;font-size:15px;border:none;border-radius:6px;cursor:pointer;background:#06c;color:#fff}"
    "button:hover{background:#0b5fd9}"
    "a.btn{display:inline-block;padding:9px 22px;font-size:15px;border-radius:6px;background:#eee;color:#333;text-decoration:none;margin-left:8px}"
    "a.btn:hover{background:#e2e2e2}"
    ".foot{display:flex;align-items:center;margin-top:14px}"
    "input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:8px;font-size:15px;border:1px solid #bbb;border-radius:6px}"
    "input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#06c;box-shadow:0 0 0 2px rgba(0,102,204,.15)}"
    "input[type=number]{width:80px;box-sizing:border-box;padding:6px 8px;font-size:14px;border:1px solid #bbb;border-radius:6px;text-align:right}"
    "input[type=number]:focus{outline:none;border-color:#06c;box-shadow:0 0 0 2px rgba(0,102,204,.15)}"
    "button[type=button]{background:#eee;color:#333}"
    "b.fail{color:#c00}"
    "#wlList{margin-top:6px;border:1px solid #e3e3e3;border-radius:6px;overflow:hidden}"
    "#wlList .wi{padding:8px 10px;font-size:14px;border-bottom:1px solid #eee;display:flex;justify-content:space-between;align-items:center;cursor:pointer;background:#fff;color:#222;text-decoration:none}"
    "#wlList .wi:last-child{border-bottom:none}#wlList .wi:hover{background:#eef4ff}"
    "#wlList .wi .bars{color:#06c;font-size:12px;letter-spacing:1px}"
    ".empty{color:#888;font-size:14px;padding:10px;text-align:center}"
    "label{display:block;margin:14px 0 4px;font-size:14px;color:#444}"
    ".wi label{display:flex;align-items:center}"
    "</style>";

/* STA 配置页头/尾（全功能单页，默认值回填；按钮在 form 内随 /save 提交） */
static const char HDR_STA[] PROGMEM =
    "<title>56dz 时钟</title>"
    "<h2>WiFi 时钟设置</h2>"
    "<div class=banner>重配网：按住「SET」+「UP」两键 5 秒，将清除网络设置并重开配网热点。"
    "<br>查看本机 IP：双击「SET」键，大屏显示 P + IP 末段（如 P168）。</div>"
    "<form method=post action=/save>";

static const char FTR_STA[] PROGMEM =
    "<div class=foot><button type=submit>保存全部</button>"
    " <a class=btn href=/cd>倒计时</a></div></form>";

/* 分区标题 + 卡片包裹：section(t)/section_end() 配对 */
static void section(const char *t) { srv.sendContent("<h3 class=sec>"); srv.sendContent(t); srv.sendContent("</h3><div class=card>"); }
static void section_end() { srv.sendContent("</div>"); }

/* 流式行输出（卡片内一行：左标签/右控件） */
static void row(const char *label) {
    srv.sendContent("<div class=row><div class=lbl>"); srv.sendContent(label); srv.sendContent("</div><div class=ctl>");
}
static void row_end() { srv.sendContent("</div></div>"); }

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
    snprintf_P(b, sizeof(b), cur == 0 ? PSTR("<label class=opt><input type=radio name=%s value=0 checked>%s</label>") : PSTR("<label class=opt><input type=radio name=%s value=0>%s</label>"), name, l0);
    srv.sendContent(b);
    snprintf_P(b, sizeof(b), cur != 0 ? PSTR("<label class=opt><input type=radio name=%s value=1 checked>%s</label>") : PSTR("<label class=opt><input type=radio name=%s value=1>%s</label>"), name, l1);
    srv.sendContent(b);
}

/* 复选（开关） */
static void chk(const char *name, bool on, const char *label) {
    char b[96];
    snprintf_P(b, sizeof(b), on ? PSTR("<label class=opt><input type=checkbox name=%s checked>%s</label>") : PSTR("<label class=opt><input type=checkbox name=%s>%s</label>"), name, label);
    srv.sendContent(b);
}

static unsigned bcd2dec(unsigned v) { return (v >> 4) * 10 + (v & 0x0F); }
static unsigned dec2bcd(unsigned v) { return ((v / 10) << 4) | (v % 10); }

static void h_sta_root() {
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（看 STA 配置页） */
    /* chunked 流式响应的正确开场：必须先声明长度未定并发出 200 空串，
       否则 sendContent 只有正文、没有响应头（ESP8266WebServer-impl.h 要求） */
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(CSS_STA);
    srv.sendContent(HDR_STA);

    /* 显示 */
    section("显示");
    row("大屏显示"); radio2("display_mode", g_cfg[0], "固定时间", "自动轮换"); row_end();
    row("下排小屏显示"); radio2("smg1_mode", g_cfg[21], "温度", "日期"); row_end();
    section_end();

    /* 亮度 */
    section("亮度");
    row("亮度模式"); radio2("bright_mode", g_cfg[1], "自动(光敏)", "手动"); row_end();
    row("手动亮度档"); sel_num("bright_lvl", g_cfg[2], 1, 8); srv.sendContent("<span class=hint>1 最暗 ~ 8 最亮</span>"); row_end();
    section_end();

    /* 温度 */
    section("温度");
    row("温度单位"); radio2("temp_unit", g_cfg[53], "摄氏 °C", "华氏 °F"); row_end();
    row("温度校准"); {
        char b[96];
        long offv = (signed char)g_cfg[3];
        snprintf_P(b, sizeof(b), PSTR("当前补偿 %+ld°C"), offv);
        srv.sendContent(b);
        srv.sendContent(PSTR("<div style='width:100%'></div>输入单位 "));
        radio2("cal_unit", g_cfg[53], "摄氏°C", "华氏°F");
        srv.sendContent(PSTR("<div style='width:100%'></div>时钟读数 <input type=number name=cal_disp step=0.1> °"
                             "<div style='width:100%'></div>实际温度 <input type=number name=cal_real step=0.1> °"));
        srv.sendContent(PSTR("<div style='width:100%' class=hint style='color:#c0392b'>上电后请等几分钟，温度读数稳定了再校准（NTC 附近会发热）。</div>"
                             "<span class=hint>先让时钟显示温度，抄下读数；填实际环境温度，保存自动补偿。</span>"));
    } row_end();
    section_end();

    /* 闹钟 */
    section("闹钟");
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
    section_end();

    /* 网络与时间 */
    section("网络与时间");
    row("时区"); sel_num("tz", wifi_tz_h(), -12, 14);
    srv.sendContent("<span class=hint>北京时间 = UTC+8</span>"); row_end();
    section_end();

    /* 杂项 */
    section("其他");
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
    section_end();

    srv.sendContent(FTR_STA);
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
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（保存配置） */
    g_cfg[0] = (uint8_t)pg("display_mode", g_cfg[0], 0, 1);
    g_cfg[1] = (uint8_t)pg("bright_mode", g_cfg[1], 0, 1);
    g_cfg[2] = (uint8_t)pg("bright_lvl", g_cfg[2], 1, 8);
    g_cfg[53] = (uint8_t)pg("temp_unit", g_cfg[53], 0, 1);
    g_cfg[21] = (uint8_t)pg("smg1_mode", g_cfg[21], 0, 1);
    g_cfg[13] = (uint8_t)(signed char)pg("tz", wifi_tz_h(), -12, 14);   /* §5 偏移13：有符号时区 */
    long sn = pg("snooze", g_cfg[19], 0, 10); g_cfg[19] = (uint8_t)((sn == 5 || sn == 10) ? sn : 0);
    g_cfg[20] = (uint8_t)(srv.hasArg("led_en") ? 1 : 0);

    /* 温度校准：读数与实际温度同单位(cal_unit)，统一转°C 后差取整为补偿 */
    if (srv.hasArg("cal_unit") && srv.hasArg("cal_disp") && srv.hasArg("cal_real")) {
        /* 信任边界：atof 可返回 HUGE_VAL/NaN（越界 double→long 为 UB），空串→0.0；
           先判串非空 + 有限值 + 物理合理范围(±200°C) 再运算 */
        const String &sd = srv.arg("cal_disp"), &sr = srv.arg("cal_real");
        float d, r;
        if (sd.length() > 0 && sr.length() > 0 &&
            (d = atof(sd.c_str()), r = atof(sr.c_str()), isfinite(d) && isfinite(r)) &&
            d <= 200.0f && d >= -200.0f && r <= 200.0f && r >= -200.0f) {
            if (pg("cal_unit", g_cfg[53], 0, 1) == 1) { d = (d - 32.0f) * 5.0f / 9.0f; r = (r - 32.0f) * 5.0f / 9.0f; }
            float diff = r - d;
            long off = (long)(diff + (diff >= 0 ? 0.5f : -0.5f));   /* round 到整数°C */
            if (off < -99) off = -99; else if (off > 99) off = 99;
            g_cfg[3] = (uint8_t)(signed char)off;
        }
    }

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
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(PSTR("<meta charset=utf-8><title>保存结果</title>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:-apple-system,'Segoe UI',Roboto,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;color:#222}"
        ".ok{background:#e7f6ec;border:1px solid #b7e3c4;border-radius:8px;padding:12px;font-size:14px;margin-bottom:12px}"
        ".warn{background:#fff5e6;border:1px solid #ffd9a0;border-radius:8px;padding:12px;font-size:14px;margin-bottom:12px}"
        "a.btn{display:inline-block;padding:8px 18px;border-radius:6px;background:#06c;color:#fff;text-decoration:none}</style>"
        "<h2>保存结果</h2>"));
    if (g_cfg_valid)
        srv.sendContent(PSTR("<div class=ok>✓ 配置已保存并下推时钟（亮度、闹钟等即时生效）。</div>"));
    else
        srv.sendContent(PSTR("<div class=warn>⚠ 时钟当前无应答，本次改动未能下推。请点击“保存全部”重试，或刷新后稍候再保存（时钟上线后才生效）。</div>"));
    srv.sendContent(PSTR("<p><a class=btn href=/>返回设置</a></p>"));
}

/* ---- 倒计时页（Web 侧管理，驱动协议帧） ---- */
static void h_cd() {
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（看倒计时页） */
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, HT, "");
    srv.sendContent(CSS_STA);
    srv.sendContent(PSTR("<title>倒计时</title><h2>倒计时</h2>"
        "<form method=post action=/cdstart>"
        "<div class=row><div class=lbl>时长</div><div class=ctl>"));
    sel_num("min", store_get_cd_min(), 1, 99);
    srv.sendContent(" 分 ");
    sel_num("sec", store_get_cd_sec(), 0, 59);
    srv.sendContent(" 秒</div></div>"
        "<div class=foot><button type=submit>开始</button>"
        " <a class=btn href=/cdpause>暂停/继续</a>"
        " <a class=btn href=/cdcancel>取消</a></div></form>"
        "<p class=hint>倒计时由时钟独立计时，到点会响铃提醒；「暂停/继续」可随时暂停恢复，「取消」立即停止。</p>"
        "<p><a href=/>← 返回设置</a></p>");
}

static void h_cd_start() {
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（启动倒计时） */
    long m = pg("min", 1, 1, 99);
    long s = pg("sec", 0, 0, 59);
    cd_set_preset((uint8_t)m, (uint8_t)s);
    store_save_cd((uint8_t)m, (uint8_t)s);
    cd_start();
    srv.send_P(200, HT, PSTR("<meta charset=utf-8><title>OK</title><h2>倒计时已开始</h2><a href=/cd>返回</a>"));
}
static void h_cd_pause() { wifi_touch(); cd_pause_resume(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }
static void h_cd_cancel() { wifi_touch(); cd_cancel(); srv.sendHeader("Location", "/cd"); srv.send(302, HT, ""); }

/* 根页面：按当前模式分流——AP 配网页（仅 WiFi 账号） / STA 全功能页（两页分离铁律） */
static void h_root() {
    wifi_touch();   /* 真实 Web 请求才刷新闲置计时（看首页） */
    srv.sendHeader("Cache-Control", "no-store");   /* 配网页为动态内容，禁止浏览器缓存，防旧页/无列表 */
    if (wifi_ap_active()) {
        srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
        srv.send(200, HT, "");
        srv.sendContent(CSS_STA);
        srv.sendContent(PAGE_AP_FIRST);
        /* 有已存凭据却仍开 AP = 上次尝试连接失败（否则不会进 AP），据此提醒用户检查 */
        char ssid[33], pwd[65];
        if (store_get_wifi(ssid, sizeof(ssid), pwd, sizeof(pwd)))
            srv.sendContent(PAGE_AP_FAIL);
        srv.sendContent(PAGE_AP_FORM_HEAD);
        /* 同步扫描周边网络，渲染可见可点的 WiFi 列表（点击填 SSID，也可手输）。
           open_ap 已 enableSTA(true)+disconnect() 满足扫描前置。 */
        int n = WiFi.scanNetworks(false, true);
        if (n <= 0)
            srv.sendContent(PSTR("<div class=empty>未扫描到附近 WiFi，请在上方手动输入。</div>"));
        for (int i = 0; i < n; i++) {
            String s = WiFi.SSID(i);
            if (s.length() == 0) continue;   /* 跳过空 SSID */
            /* SSID 左对齐靠左、信号条靠右（flex space-between 见样式）；点击填入输入框 */
            char sb[192], b[300];
            esc(s.c_str(), sb, sizeof(sb));   /* 转义后嵌入 onclick(单引号) 与文本，防扫描 SSID 注入 */
            const char *bars = WiFi.RSSI(i) >= -60 ? "▂▄▆█"
                            : WiFi.RSSI(i) >= -70 ? "▂▄▆ "
                            : WiFi.RSSI(i) >= -80 ? "▂▄  "
                            :                       "▂   ";
            const char *note = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "（开放）" : "";
            snprintf_P(b, sizeof(b), PSTR("<a class=wi onclick=setWifi('%s')>%s%s<span class=bars>%s</span></a>"),
                sb, sb, note, bars);
            srv.sendContent(b);
        }
        WiFi.scanDelete();           /* 释放扫描结果内存 */
        srv.sendContent(PAGE_AP_FORM_TAIL);
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
        srv.handleClient();   /* 真正的用户请求在 handler 内 wifi_touch() 刷新闲置计时；
                                 不在本处无条件 touch，否则每 loop 刷新 last_rf_use → 5min
                                 闲置计时永不满足 → 8266 一直联网不进伪休眠 */
    }
    /* 伪待机（RF 关/未关联）时无网可服务；仅 STA 关联或 AP 开启时轮询 */
}
