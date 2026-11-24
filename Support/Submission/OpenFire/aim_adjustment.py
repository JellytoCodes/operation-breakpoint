"""Output-stage sight zero and Y trim, shared by IR diagnostics and UE."""
import json
import math
from pathlib import Path
import sys
import time

# Source tools and frozen helpers import the same calibration implementation.
if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'DeviceSettings'))
from device_settings import load_section, save_section, migrate_section, DeviceChannel
from encoder import counter_delta

TRIM_LIMIT_MM = 500.


def default_path():
    if getattr(sys,'frozen',False):
        root=Path(sys.executable).resolve().parent
        for folder in (root,root/'OperationBreakpoint'):
            candidate=folder/'Saved/ControllerInput/OpenFire/aim-adjustment.json'
            if candidate.exists(): return candidate
        return root/'Saved/ControllerInput/OpenFire/aim-adjustment.json'
    return Path(__file__).resolve().parents[3]/'Saved/ControllerInput/OpenFire/aim-adjustment.json'


class AimAdjustment:
    def __init__(self,height_mm,*,path=None,display_id='',base_mm=50.,step_mm=2.,shared=False):
        if not math.isfinite(height_mm) or not 100<=height_mm<=3000: raise ValueError('실제 화면 높이(mm)가 필요합니다.')
        if not all(math.isfinite(v) for v in (base_mm,step_mm)) or not -200<=base_mm<=200 or not 0<step_mm<=5:
            raise ValueError('Invalid Y adjustment')
        self.height_mm=height_mm; self.base_mm=base_mm; self.step_mm=step_mm
        self.path=Path(path) if path else None; self.display_id=display_id
        self.trim_mm=0.; self.last=None; self.received=0.; self.dirty=False; self.changed=0.
        self.x_shift=0.
        self.shared=shared; self.shared_section="Gun:"+display_id.encode().hex(); self.last_reload=0.
        if self.path and self.path.exists():
            data=json.loads(self.path.read_text(encoding='utf-8'))
            entry=data.get('displays',{}).get(display_id,{})
            value=entry.get('trim_mm',0.)
            if type(value) not in (int,float) or not math.isfinite(value) or abs(value)>TRIM_LIMIT_MM:
                raise ValueError('Invalid saved encoder trim')
            self.trim_mm=float(value)
            value=entry.get('x_shift',0.)
            if type(value) not in (int,float) or not math.isfinite(value) or abs(value)>.5:
                raise ValueError('Invalid saved X sight zero')
            self.x_shift=float(value)

        if self.shared:
            values=migrate_section(self.shared_section,dict(trim_mm=self.trim_mm,x_shift=self.x_shift,
                height_mm=self.height_mm,base_up_mm=self.base_mm,step_mm=self.step_mm))
            self._apply_shared(values)
            save_section('Gun', dict(active_display=display_id,section_id=display_id.encode().hex()))

    def _apply_shared(self, values):
        limits={'trim_mm':(-500,500),'x_shift':(-.5,.5),'height_mm':(100,3000),
                'base_up_mm':(-200,200),'step_mm':(.01,5)}
        attrs={'height_mm':'height_mm','base_up_mm':'base_mm','step_mm':'step_mm'}
        for key,(low,high) in limits.items():
            value=values.get(key)
            if type(value) in (int,float) and math.isfinite(value) and low<=value<=high:
                setattr(self,attrs.get(key,key),float(value))

    def reload_shared(self):
        if not self.shared or time.monotonic()-self.last_reload<.2: return
        self.last_reload=time.monotonic()
        values=load_section(self.shared_section)
        # The encoder owns only its unsaved Y trim. F6's X zero and other
        # settings must still update while the dial is moving or settling.
        if self.dirty: values.pop('trim_mm',None)
        self._apply_shared(values)

    @property
    def total_mm(self): return self.base_mm+self.trim_mm

    @property
    def label(self): return f'X 영점 {self.x_shift*100:+.2f}% · Y 위쪽 {self.total_mm:+.1f} mm · 기본 {self.base_mm:.1f} + 미세 {self.trim_mm:+.1f} · KY-040 한 칸 {self.step_mm:g} mm · 범위 ±{TRIM_LIMIT_MM:g} mm'

    def apply(self,point):
        # Screen coordinates increase downwards; user +Y means physically up.
        return point[0]+self.x_shift,point[1]-self.total_mm/self.height_mm

    def reset_session(self): self.last=None; self.received=0.

    def accept_encoder(self,frame,now=None):
        now=time.monotonic() if now is None else now
        self.received=now
        if self.last is None: self.last=frame; return False
        delta=counter_delta(frame.position,self.last.position)
        pressed=counter_delta(frame.presses,self.last.presses)
        self.last=frame
        # A reboot or an implausible jump resynchronizes without moving aim.
        if abs(delta)>64 or not 0<=pressed<=4: return False
        value=0. if pressed else max(-TRIM_LIMIT_MM,min(TRIM_LIMIT_MM,self.trim_mm+delta*self.step_mm))
        if value==self.trim_mm: return False
        self.trim_mm=value; self.dirty=True; self.changed=now
        return True

    def flush(self,force=False):
        if not self.path or not self.dirty or (not force and time.monotonic()-self.changed<.7): return
        if self.shared:
            # An encoder update owns only trim; preserve concurrent Unreal edits
            # to height, sight zero, and the base offset.
            save_section(self.shared_section,dict(trim_mm=self.trim_mm))
            self.dirty=False
            self._apply_shared(load_section(self.shared_section))
            return
        data=json.loads(self.path.read_text(encoding='utf-8')) if self.path.exists() else dict(version=1,displays={})
        data.setdefault('displays',{}).setdefault(self.display_id,{}).update(
            trim_mm=self.trim_mm,height_mm=self.height_mm,base_up_mm=self.base_mm,x_shift=self.x_shift)
        self.path.parent.mkdir(parents=True,exist_ok=True)
        temporary=self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
        temporary.replace(self.path); self.dirty=False


def load_adjustment(display=None,path=None,height_mm=None,base_mm=50.):
    from display_target import target_display
    display=display or load_section('Gun').get('display','PRIMARY')
    target=target_display(display)
    saved=load_section('Gun:'+target['device_id'].encode().hex())
    return AimAdjustment(height_mm or saved.get('height_mm') or target.get('physical_height_mm',0),path=path or default_path(),
                         display_id=target['device_id'],base_mm=base_mm,shared=True)
