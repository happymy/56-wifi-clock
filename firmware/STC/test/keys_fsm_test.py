# keys.c 按键状态机 PC 验证（与固件同算法）。验证 单击/双击/长按。
KEY_SET, KEY_UP = 0, 1
EV_SINGLE, EV_DOUBLE, EV_LONG = 1, 2, 3
LONG_CNT, DBL_CNT, QSIZE = 100, 15, 6  # 与 firmware/STC/src/keys.c 一致

class K:
    def __init__(s):
        s.down=s.t_down=s.t_up=s.long_fired=s.dbl_pending=0

p3 = 0x0C  # P3.2=UP(0x04) P3.3=SET(0x08) 默认高=未按下
q=[]; qh=0; qt=0; qc=0; both=0
st=[K(), K()]

def pressed(b):
    if b==KEY_SET: return int(not (p3 & 0x08))
    return int(not (p3 & 0x04))

def emit(b,e):
    global qh,qc
    if qc>=QSIZE: return
    q.append((b,e)); qh=(qh+1)%QSIZE; qc+=1

def scan_one(cur,s,btn):
    if cur:
        if not s.down:
            s.down=1; s.t_down=0
            if s.dbl_pending: s.dbl_pending=0; emit(btn,EV_DOUBLE); s.down=0; s.long_fired=1
        s.t_down+=1
        if not s.long_fired and s.t_down>=LONG_CNT:
            emit(btn,EV_LONG); s.long_fired=1
    else:
        if s.down:
            s.down=0; s.t_up=0
            if s.long_fired: s.long_fired=0; return
            s.dbl_pending=1
        else:
            if s.dbl_pending:
                s.t_up += 1
                if s.t_up >= DBL_CNT:
                    emit(btn,EV_SINGLE); s.dbl_pending=0

def keys_scan():
    global both
    p0=pressed(KEY_SET); p1=pressed(KEY_UP)
    if p0 and p1: both=1; return
    both=0
    scan_one(p0,st[0],KEY_SET); scan_one(p1,st[1],KEY_UP)

def key_get():
    global qt,qc
    if qc==0: return None
    b,e=q[qt]; qt=(qt+1)%QSIZE; qc-=1; return (b,e)

def drive(n,up,set_):
    global p3
    for _ in range(n):
        p3=0x0C
        if up: p3&=~0x04
        if set_: p3&=~0x08
        keys_scan()

def reset():
    global q,qh,qt,qc,both
    q=[]; qh=qt=qc=0; both=0
    st[0]=K(); st[1]=K()

def drain():
    out=[]
    while True:
        e=key_get()
        if e is None: break
        out.append(e)
    return out

def nm(e): return {EV_SINGLE:'SINGLE',EV_DOUBLE:'DOUBLE',EV_LONG:'LONG'}.get(e,'?')

fail=0
# 单击 UP
reset(); drive(3,1,0); drive(25,0,0)
evs=drain()
print('UP single :', evs, 'OK' if evs==[(KEY_UP,EV_SINGLE)] else 'FAIL'); fail+= 0 if evs==[(KEY_UP,EV_SINGLE)] else 1
# 双击 UP
reset(); drive(3,1,0); drive(5,0,0); drive(3,1,0); drive(25,0,0)
evs=drain()
print('UP double :', evs, 'OK' if evs==[(KEY_UP,EV_DOUBLE)] else 'FAIL'); fail+= 0 if evs==[(KEY_UP,EV_DOUBLE)] else 1
# 长按 UP
reset(); drive(120,1,0); drive(5,0,0)
evs=drain()
print('UP long   :', evs, 'OK' if evs==[(KEY_UP,EV_LONG)] else 'FAIL'); fail+= 0 if evs==[(KEY_UP,EV_LONG)] else 1
# 单击 SET
reset(); drive(3,0,1); drive(25,0,0)
evs=drain()
print('SET single:', evs, 'OK' if evs==[(KEY_SET,EV_SINGLE)] else 'FAIL'); fail+= 0 if evs==[(KEY_SET,EV_SINGLE)] else 1

print('\nFSM TEST:', 'PASS' if fail==0 else f'FAIL ({fail})')
