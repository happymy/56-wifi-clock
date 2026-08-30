# display.c 渲染逻辑 PC 验证（与固件同算法）。
# 验证 §7 冻结显示约定，防历史 bug（TEMP 个位显成 E、SMG1 温度 32→23）回归。
# 运行：python display_logic_test.py

SEG_FONT = [
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,
]

def seg_rotate180(d):
    return (((d >> 3) & 1) << 0
            | ((d >> 4) & 1) << 1
            | ((d >> 5) & 1) << 2
            | ((d >> 0) & 1) << 3
            | ((d >> 1) & 1) << 4
            | ((d >> 2) & 1) << 5
            | ((d >> 6) & 1) << 6
            | (d & 0x80))

def abs_div10(v):
    a = (-v) if v < 0 else v
    d = 0
    while a >= 10:
        a -= 10
        d += 1
    if d > 99:
        d = 99
    return d

class T:
    def __init__(self, year, month, date, weekday, hr, min, sec):
        self.year = year; self.month = month; self.date = date
        self.weekday = weekday; self.hr = hr; self.min = min; self.sec = sec

def disp_render(mode, t, temp_x10, smg1_sel, temp_unit):
    disp = [0] * 8
    if mode == 2:  # DISP_DATE
        disp[0] = SEG_FONT[(t.month >> 4) & 0x0F]
        disp[1] = SEG_FONT[t.month & 0x0F]
        disp[2] = seg_rotate180(SEG_FONT[(t.date >> 4) & 0x0F])
        disp[3] = SEG_FONT[t.date & 0x0F]
        disp[7] = SEG_FONT[(t.year >> 4) & 0x0F]
        disp[6] = SEG_FONT[t.year & 0x0F]
        disp[5] = SEG_FONT[t.weekday & 0x0F]
        disp[4] = 0
    elif mode == 3:  # DISP_TEMP
        v = temp_x10
        if v <= -990:
            v = 250
        if temp_unit != 0:
            v = v + (v >> 1) + (v >> 2) + (v >> 4) + 320
        neg = v < 0
        deg = abs_div10(v)
        disp[0] = 0x40 if neg else 0
        disp[1] = SEG_FONT[deg // 10]
        disp[2] = seg_rotate180(SEG_FONT[deg % 10])
        disp[3] = 0x71 if temp_unit != 0 else 0x39
    else:  # DISP_TIME
        disp[0] = SEG_FONT[(t.hr >> 4) & 0x0F]
        disp[1] = SEG_FONT[t.hr & 0x0F] | 0x80
        disp[2] = seg_rotate180(SEG_FONT[(t.min >> 4) & 0x0F] | 0x80)
        disp[3] = SEG_FONT[t.min & 0x0F]
        disp[7] = SEG_FONT[(t.sec >> 4) & 0x0F]
        disp[6] = SEG_FONT[t.sec & 0x0F]
        if smg1_sel == 0:
            td = abs_div10(temp_x10)
            disp[5] = SEG_FONT[td // 10]
            disp[4] = SEG_FONT[td % 10] | 0x80
        else:
            disp[5] = SEG_FONT[(t.date >> 4) & 0x0F]
            disp[4] = SEG_FONT[t.date & 0x0F]
    return disp

fails = 0
def check(name, cond):
    global fails
    print(("OK  " if cond else "FAIL") + " " + name)
    if not cond:
        fails += 1

# 1) seg_rotate180 对合：rotate(rotate(x))==x（含 dp 位）
ok_inv = all(seg_rotate180(seg_rotate180(x)) == x for x in range(256))
check("rotate180 对合(全256值)", ok_inv)
# 段翻转方向正确：a(0x01)↔d(0x08), b(0x02)↔e(0x10), c(0x04)↔f(0x20)
r = seg_rotate180(0x01)
check("rotate180 a->d", r == 0x08)
check("rotate180 b->e", seg_rotate180(0x02) == 0x10)
check("rotate180 c->f", seg_rotate180(0x04) == 0x20)
check("rotate180 dp 保持", seg_rotate180(0x80) == 0x80)

t = T(0x26, 0x12, 0x31, 0x05, 0x12, 0x34, 0x56)  # 2026-12-31 周5 12:34:56

# 2) DISP_TIME：GRID3(disp[2]) 必过 seg_rotate180
d = disp_render(0, t, 250, 0, 0)
exp2 = seg_rotate180(SEG_FONT[(t.min >> 4) & 0x0F] | 0x80)
check("TIME disp[2]=rotate180(分十位|dp)", d[2] == exp2)
check("TIME disp[0]=时十位", d[0] == SEG_FONT[1])
check("TIME disp[1]=时个位|dp", d[1] == SEG_FONT[2] | 0x80)
check("TIME disp[3]=分个位", d[3] == SEG_FONT[4])
# SMG2 十位=左管(disp[7]) 个位=右管(disp[6])：秒=56
check("TIME SMG2 十位(秒5)->disp[7]", d[7] == SEG_FONT[5])
check("TIME SMG2 个位(秒6)->disp[6]", d[6] == SEG_FONT[6])
# SMG1=温度：25.0°C -> 十位2 个位5|dp
check("TIME SMG1 温度十位(2)->disp[5]", d[5] == SEG_FONT[2])
check("TIME SMG1 温度个位(5|dp)->disp[4]", d[4] == SEG_FONT[5] | 0x80)
# SMG1=日期：日=31
d2 = disp_render(0, t, 250, 1, 0)
check("TIME SMG1 日期十位(3)->disp[5]", d2[5] == SEG_FONT[3])
check("TIME SMG1 日期个位(1)->disp[4]", d2[4] == SEG_FONT[1])

# 3) DISP_DATE：GRID3=日十位 必过 rotate180；SMG1=星期
dd = disp_render(2, t, 250, 0, 0)
check("DATE disp[2]=rotate180(日十位)", dd[2] == seg_rotate180(SEG_FONT[(t.date >> 4) & 0x0F]))
check("DATE disp[0]=月十位", dd[0] == SEG_FONT[1])
check("DATE disp[1]=月个位", dd[1] == SEG_FONT[2])
check("DATE disp[3]=日个位", dd[3] == SEG_FONT[1])
check("DATE SMG1=星期(5)->disp[5]", dd[5] == SEG_FONT[5])
# SMG2=年 YY=26
check("DATE SMG2 十位(2)->disp[7]", dd[7] == SEG_FONT[2])
check("DATE SMG2 个位(6)->disp[6]", dd[6] == SEG_FONT[6])

# 4) DISP_TEMP：GRID3=个位 必过 rotate180；哨兵兜底 25°C；°F 单调
dt = disp_render(3, t, 250, 0, 0)  # 25.0°C
check("TEMP disp[2]=rotate180(个位5)", dt[2] == seg_rotate180(SEG_FONT[5]))
check("TEMP disp[1]=十位2", dt[1] == SEG_FONT[2])
check("TEMP 单位=C(0x39)", dt[3] == 0x39)
ds = disp_render(3, t, -999, 0, 0)  # 开路哨兵 -> 25°C
check("TEMP 哨兵(-999)->兜底25°C 个位5", ds[2] == seg_rotate180(SEG_FONT[5]))
df0 = disp_render(3, t, 0, 0, 1)    # 0°C -> °F 32
df10 = disp_render(3, t, 100, 0, 1) # 10°C -> °F 50
deg0 = abs_div10(0 + 0 + 0 + 0 + 320)
deg10 = abs_div10(100 + 50 + 25 + 6 + 320)
check("TEMP °F 0°C->32°F", deg0 == 32)
check("TEMP °F 10°C->50°F 且单调增", deg10 == 50 and deg10 > deg0)

print("\nDISPLAY LOGIC TEST:", "PASS" if fails == 0 else f"FAIL ({fails})")
import sys
sys.exit(1 if fails else 0)
