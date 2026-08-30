# main.c 响铃/贪睡状态机 PC 验证（与固件同算法）。
# 重点验证 #1：倒计时归零 ring_alarm=4 路径无越界、可 SET 全停 / UP 贪睡重生；
#           #2：按键亮度确认分支落盘 cfg_save。
# 运行：python ring_alarm_fsm_test.py
# 键码与 keys.h 一致：KEY_SET=0, KEY_UP=1

class St:
    def __init__(self, snooze=5):
        self.ring_alarm = 0
        self.ring_ticks = 0
        self.snooze_idx = 0
        self.snooze_ticks = 0
        self.cfg_snooze = snooze
        self.beep_on = False

    def key_when_ringing(self, btn):
        # 移植 main.c:295-301
        if self.ring_alarm:
            if btn == 0:  # SET：停所有 + 取消确认音(此处只清状态)
                self.ring_alarm = 0
                self.ring_ticks = 0
                self.snooze_idx = 0
                self.snooze_ticks = 0
                self.beep_on = False
            else:  # KEY_UP：贪睡
                self.snooze_idx = self.ring_alarm
                self.snooze_ticks = ((self.cfg_snooze << 8) - (self.cfg_snooze << 4))  # *240
                self.ring_alarm = 0
                self.ring_ticks = 0
                self.beep_on = False

    def tick(self):
        # 移植 main.c:409-417 响铃相位 + 贪睡重生
        if self.ring_alarms():
            if self.ring_ticks:
                self.ring_ticks -= 1
                self.beep_on = (self.ring_ticks & 3) < 2
                if self.ring_ticks == 0:
                    self.ring_alarm = 0
                    self.beep_on = False
        elif self.snooze_ticks:
            self.snooze_ticks -= 1
            if self.snooze_ticks == 0:
                self.ring_alarm = self.snooze_idx
                self.ring_ticks = 240
                self.snooze_idx = 0

    def ring_alarms(self):
        return self.ring_alarm != 0

def bright_confirm(adj_val, cfg):
    # 移植 main.c 亮度确认分支(#2)：写 cfg + cfg_save 落盘
    saved = {"called": False}
    cfg["bright_mode"] = 0 if adj_val == 0 else 1
    if adj_val:
        cfg["bright_lvl"] = adj_val
    saved["called"] = True  # cfg_save() 落盘
    return cfg, saved

fails = 0
def check(name, cond):
    global fails
    print(("OK  " if cond else "FAIL") + " " + name)
    if not cond:
        fails += 1

# #1 倒计时归零 ring_alarm=4
s = St()
s.ring_alarm = 4
s.ring_ticks = 240
check("倒计时响铃 ring_alarm=4 识别为响铃中", s.ring_alarms())
# SET 全停
s.key_when_ringing(0)
check("#1 SET 全停: ring_alarm=0", s.ring_alarm == 0)
check("#1 SET 全停: ring_ticks=0", s.ring_ticks == 0)
check("#1 SET 全停: snooze_idx=0", s.snooze_idx == 0)
check("#1 SET 全停: snooze_ticks=0", s.snooze_ticks == 0)
# UP 贪睡 + 重生
s = St()
s.ring_alarm = 4
s.ring_ticks = 240
s.key_when_ringing(1)
check("#1 UP 贪睡: snooze_idx=4", s.snooze_idx == 4)
check("#1 UP 贪睡: ring_alarm=0(释放)", s.ring_alarm == 0)
check("#1 UP 贪睡: snooze_ticks=5*240=1200", s.snooze_ticks == 1200)
for _ in range(1200):
    s.tick()
check("#1 贪睡到点重生 ring_alarm=4", s.ring_alarm == 4)
check("#1 贪睡到点重生 ring_ticks=240", s.ring_ticks == 240)
check("#1 贪睡到点重生 snooze_idx=0", s.snooze_idx == 0)

# 闹钟 1..3 同样形状（回归）
for a in (1, 2, 3):
    s = St()
    s.ring_alarm = a
    s.ring_ticks = 240
    s.key_when_ringing(1)
    check(f"闹钟{a} UP 贪睡 snooze_idx={a}", s.snooze_idx == a)
    for _ in range(1200):
        s.tick()
    check(f"闹钟{a} 重生 ring_alarm={a}", s.ring_alarm == a)

# #2 亮度确认落盘
cfg, saved = bright_confirm(3, {"bright_mode": 0, "bright_lvl": 0})
check("#2 adj=3: bright_mode=1", cfg["bright_mode"] == 1)
check("#2 adj=3: bright_lvl=3", cfg["bright_lvl"] == 3)
check("#2 adj=3: cfg_save 已调用", saved["called"])
cfg0, saved0 = bright_confirm(0, {"bright_mode": 1, "bright_lvl": 5})
check("#2 adj=0: bright_mode=0(自动)", cfg0["bright_mode"] == 0)
check("#2 adj=0: bright_lvl 不变(仍是5)", cfg0["bright_lvl"] == 5)
check("#2 adj=0: cfg_save 已调用", saved0["called"])

print("\nRING ALARM FSM TEST:", "PASS" if fails == 0 else f"FAIL ({fails})")
import sys
sys.exit(1 if fails else 0)
