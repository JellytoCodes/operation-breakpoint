"""Guided, measured Square calibration with backup and restricted readback."""
import argparse
from collections import deque
from copy import deepcopy
from dataclasses import asdict
import json
import math
from pathlib import Path
import queue
import statistics
import sys
import threading
import time
import tkinter as tk

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from device_port import wait_for_port
from device_state import snapshot, exact
from diagnostics import identify, run_diagnostics, DiagnosticParser
from display_target import target_display
from ir_calibration import TARGETS, NAMES, FIELDS, matrix_for, point_for, profile_point, fit_profile, encoded_values


def expected_update(before, values):
    if before['version'] not in ('6.1-ca30987-obir1','6.1-ca30987-obir2','6.1-ca30987-obir3') or before['board']!='rpipico':
        raise ValueError('Requires the verified obir1 Pico')
    profile=bytes.fromhex(before['settings']['profiles']['CurrentProf'])[0]
    if before['settings']['profiles'][f'IrLayout/{profile}'] != '00000000':
        raise ValueError('Requires Square layout')
    after=deepcopy(before)
    for key,value in encoded_values(values).items():
        after['settings']['profiles'][f'{key}/{profile}']=value
    return after,profile


def write_profile(port, profile, encoded):
    if set(encoded) != set(FIELDS) or profile not in range(4):
        raise ValueError('Refusing non-calibration settings')
    if any(len(bytes.fromhex(v)) != 4 for v in encoded.values()):
        raise ValueError('Invalid profile field size')
    committing=False
    try:
        port.dtr=True; port.reset_input_buffer()
        port.write(b'\x01\x02')
        header=bytearray(); deadline=time.monotonic()+3
        port.timeout=.15
        while time.monotonic() < deadline:
            data=port.read(max(1,min(256,port.in_waiting)))
            if data: header.extend(data)
            elif header: break
        version,board=identify(bytes(header))
        if version not in ('6.1-ca30987-obir1','6.1-ca30987-obir2','6.1-ca30987-obir3') or board!='rpipico':
            raise ValueError('Unexpected device identity')
        port.timeout=3
        port.write(b'\xaa\x01'); committing=True
        if exact(port,1) != b'\xaa': raise ValueError('No settings transaction ACK')
        for key in FIELDS:
            body=key.encode('ascii')+b'\0\x04'+bytes((profile,))+bytes.fromhex(encoded[key])
            port.write(b'\xae'+body)
            if exact(port,len(body)) != body: raise ValueError('Calibration echo mismatch: '+key)
        port.write(b'\xfc')
        if exact(port,2) != b'\xfc\x01': raise ValueError('Calibration save failed')
        committing=False
        time.sleep(.7)
        # Rebuild the cached perspective using the same profile and new values.
        port.write(bytes((6,profile,0)))
        parser=DiagnosticParser(); acknowledged=False; deadline=time.monotonic()+3
        while time.monotonic()<deadline and not acknowledged:
            for kind,value in parser.feed(port.read(max(1,min(port.in_waiting,512)))):
                if kind=='profile' and value==profile: acknowledged=True
                elif kind=='error': raise ValueError(f'Camera/profile error: {value}')
        if not acknowledged: raise ValueError('Profile refresh acknowledgement timed out')
    finally:
        if committing:
            port.write(b'\xfe'); port.flush(); time.sleep(.1)
        port.write(b'\xfe'); port.flush()


def apply_profile(name, before, values, folder):
    import serial
    expected,profile=expected_update(before,values)
    if snapshot(name) != before:
        raise ValueError('Settings changed during calibration; no values were written')
    (folder/'candidate.json').write_text(json.dumps(values,indent=2),encoding='utf-8')
    with serial.Serial(name,9600,timeout=3,write_timeout=1) as port:
        write_profile(port,profile,encoded_values(values))
    after=snapshot(name)
    (folder/'settings-after.json').write_text(json.dumps(after,indent=2),encoding='utf-8')
    if after != expected:
        raise ValueError('Calibration readback differs; inspect the saved backup before proceeding')
    return dict(saved=True,profile=profile,changed_fields=[key for key in FIELDS if
        before['settings']['profiles'][f'{key}/{profile}'] != after['settings']['profiles'][f'{key}/{profile}']],other_settings_unchanged=True)


class Wizard:
    def __init__(self, root, name, folder, before, display):
        self.root,self.name,self.folder,self.before=root,name,folder,before
        self.events=queue.SimpleQueue(); self.history=deque(maxlen=50)
        self.captures=[]; self.validations=[]; self.values=None; self.fit=None
        self.phase='capture'; self.index=0; self.message='보이는 표적을 가늠좌·가늠쇠로 겨누고 Space를 누르세요.'
        self.latest=None; self.latest_at=0.; self.count=0; self.last_press=0.; self.last_status=0.; self.closed=False
        self.result=dict(saved=False,validated=False,port=name,backup=str(folder/'settings-before.json'))
        self.worker=None; self.stop=threading.Event()
        root.title('Operation Breakpoint · IR 실물 재보정')
        root.geometry(f"650x150{display['x']:+d}{display['y']:+d}")
        tk.Label(root,text='IR 실물 재보정 진행 중 · 표적 화면에서 Space로 등록합니다.',padx=20,pady=40).pack()
        self.window=tk.Toplevel(root); self.window.title('IR 실물 재보정 · 표적')
        self.window.geometry(f"{display['width']}x{display['height']}{display['x']:+d}{display['y']:+d}")
        self.window.overrideredirect(True); self.window.attributes('-topmost',True)
        self.canvas=tk.Canvas(self.window,bg='#101923',highlightthickness=0)
        self.canvas.pack(fill='both',expand=True)
        root.bind_all('<KeyRelease-space>',self.capture); root.bind_all('<KeyRelease-Return>',self.capture)
        root.bind_all('<Escape>',self.close); root.bind_all('<r>',self.restart)
        root.protocol('WM_DELETE_WINDOW',self.close)
        root.report_callback_exception=self.callback_error
        root.deiconify(); self.window.deiconify(); self.window.lift(); self.window.focus_force()
        self.start_worker(); self.tick()

    def record(self):
        data=dict(result=self.result,phase=self.phase,fit=self.fit,values=self.values,
                  captures=[[asdict(s) for s in c] for c in self.captures],validations=self.validations)
        (self.folder/'calibration-progress.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

    def start_worker(self):
        self.stop=threading.Event(); self.history.clear(); self.latest=None; self.count=0
        def run():
            try: run_diagnostics(self.name,self.stop,lambda k,v:self.events.put((k,v,time.monotonic())))
            except Exception as exc: self.events.put(('error',str(exc),time.monotonic()))
        self.worker=threading.Thread(target=run,daemon=True); self.worker.start()

    def batch(self):
        now=time.monotonic()
        values=[(at,s) for at,s in self.history if now-at <= .65]
        if self.count != 4 or now-self.latest_at > .18 or len(values) < 8:
            raise ValueError('LED 4/4가 유지되도록 맞추고 잠시 멈춘 뒤 다시 누르세요.')
        if any(b[0]-a[0] > .16 for a,b in zip(values,values[1:])):
            raise ValueError('좌표 수신이 불안정합니다. 다시 고정해 주세요.')
        samples=[s for _,s in values]
        points=[point_for(s,(2048.,1536.)) for s in samples]
        if max(statistics.pstdev(p[i] for p in points) for i in (0,1)) > .012:
            raise ValueError('움직임이 큽니다. 표적을 겨누고 잠시 멈춰주세요.')
        return samples

    def capture(self,_event=None):
        now=time.monotonic()
        if now-self.last_press < .8 or self.phase in ('saving','done','error'): return 'break'
        self.last_press=now
        try:
            batch=self.batch()
            if self.phase=='capture':
                self.captures.append(batch); self.history.clear(); self.index+=1
                if self.index==5:
                    try: self.values,self.fit=fit_profile(self.captures)
                    except ValueError:
                        self.captures=[]; self.index=0
                        raise
                    self.phase='confirm'; self.index=0
                    self.message='다시 중앙을 겨누세요. 새 보정의 중앙 오차를 확인한 뒤 저장합니다.'
                else: self.message=f'{NAMES[self.index]} 표적을 겨눈 뒤 Space를 누르세요.'
            elif self.phase=='confirm':
                points=[profile_point(s,self.values) for s in batch]
                center=tuple(statistics.median(p[i] for p in points) for i in (0,1))
                if math.dist(center,(.5,.5)) > .025:
                    raise ValueError(f'저장 전 중앙 오차 {math.dist(center,(.5,.5))*100:.1f}%. 다시 겨누거나 R로 재시작하세요.')
                self.phase='saving'; self.result['save_started']=True
                self.message='보드에 저장하고 전체 설정을 다시 읽어 비교하고 있습니다.'
                self.stop.set(); self.root.after(50,self.commit)
            elif self.phase=='verify':
                center=tuple(statistics.median(s.normalized[i] for s in batch) for i in (0,1))
                error=math.dist(center,TARGETS[self.index])
                if error > .035: raise ValueError(f'실물 검증 오차 {error*100:.1f}%. 표적을 다시 정확히 겨눠주세요.')
                self.validations.append(dict(target=TARGETS[self.index],actual=center,error=error,frames=len(batch)))
                self.history.clear(); self.index+=1
                if self.index==5:
                    self.phase='done'; self.result['validated']=True
                    self.message='보드 저장과 실물 5점 검증을 마쳤습니다. Esc로 종료하세요.'
                else: self.message=f'저장된 보드 좌표 검증: {NAMES[self.index]}을 겨누고 Space를 누르세요.'
            self.record()
        except ValueError as exc: self.message=str(exc)
        return 'break'

    def commit(self):
        if self.closed: return
        if self.worker.is_alive(): self.root.after(50,self.commit); return
        def save():
            try: self.events.put(('saved',apply_profile(self.name,self.before,self.values,self.folder),time.monotonic()))
            except Exception as exc: self.events.put(('error',str(exc),time.monotonic()))
        self.worker=threading.Thread(target=save,daemon=True); self.worker.start()

    def restart(self,_event=None):
        if self.result['saved'] or self.phase=='saving': return
        self.captures=[]; self.values=None; self.fit=None; self.index=0; self.history.clear()
        self.phase='capture'; self.message='중앙 표적부터 다시 등록합니다. Space로 등록하세요.'

    def tick(self):
        if self.closed: return
        while not self.events.empty():
            kind,value,at=self.events.get_nowait()
            if kind=='sample':
                self.latest,self.latest_at=value,at; self.count=len(value.detection.points)
                try: matrix_for(value)
                except ValueError: self.history.clear(); self.latest=None
                else: self.history.append((at,value))
            elif kind in ('error','detection_error'):
                self.phase='error'; self.message=str(value); self.result['error']=str(value); self.record()
            elif kind=='saved':
                self.result.update(value); self.phase='verify'; self.index=0
                self.message='저장 완료. 중앙을 겨누고 Space로 실제 보드 좌표를 검증하세요.'
                self.start_worker(); self.record()
        if time.monotonic()-self.last_status > 1:
            status=dict(phase=self.phase,index=self.index,leds=self.count,fresh=time.monotonic()-self.latest_at < .2,
                        message=self.message,saved=self.result['saved'],validated=self.result['validated'])
            (self.folder/'status.json').write_text(json.dumps(status,ensure_ascii=False,indent=2),encoding='utf-8')
            self.last_status=time.monotonic()
        self.draw(); self.root.after(50,self.tick)

    def draw(self):
        c=self.canvas; c.delete('all')
        w,h=max(c.winfo_width(),1),max(c.winfo_height(),1)
        scale=max(.7,min(w/1920,h/1080)); fs=lambda n:max(12,int(n*scale))
        target=TARGETS[self.index] if self.phase in ('capture','verify') else (.5,.5)
        x,y=target[0]*w,target[1]*h
        r=24*scale
        c.create_oval(x-r,y-r,x+r,y+r,outline='#ffd166',width=4)
        c.create_line(x-r*1.6,y,x+r*1.6,y,fill='#ffd166',width=3)
        c.create_line(x,y-r*1.6,x,y+r*1.6,fill='#ffd166',width=3)
        title=f'IR 실물 보정 {self.index+1}/5 · {NAMES[self.index]}' if self.phase=='capture' else f'저장 후 실물 검증 {self.index+1}/5 · {NAMES[self.index]}' if self.phase=='verify' else '저장 전 중앙 확인' if self.phase=='confirm' else '보정 완료' if self.phase=='done' else '보드 저장 중' if self.phase=='saving' else '보정 중단'
        c.create_text(w/2,h*.25,text=title,fill='#e5edf5',font=('맑은 고딕',fs(27),'bold'))
        fresh=time.monotonic()-self.latest_at < .2
        c.create_text(w/2,h*.33,text=f'LED {self.count if fresh else 0}/4 · 네 점이 모두 보일 때만 등록',fill='#49e89c' if fresh and self.count==4 else '#ffd166',font=('맑은 고딕',fs(19)))
        if self.latest and fresh and self.count==4 and self.phase in ('confirm','verify','done'):
            p=self.latest.normalized if self.result['saved'] else profile_point(self.latest,self.values)
            if all(0 <= v <= 1 for v in p):
                px,py=p[0]*w,p[1]*h
                c.create_line(px-12*scale,py,px+12*scale,py,fill='#60dcff',width=3)
                c.create_line(px,py-12*scale,px,py+12*scale,fill='#60dcff',width=3)
            c.create_text(w/2,h*.7,text=f'보정 조준점 X {p[0]*100:.1f}% · Y {p[1]*100:.1f}%',fill='#60dcff',font=('맑은 고딕',fs(18)))
        c.create_text(w/2,h*.77,text=self.message,fill='#e5edf5',font=('맑은 고딕',fs(18)),width=w*.78,justify='center')
        c.create_text(w/2,h*.88,text='가늠좌·가늠쇠로 노란 표적을 겨누세요.  ·  Space / Enter: 등록  ·  R: 재시작  ·  Esc: 종료',fill='#9baec0',font=('맑은 고딕',fs(15)))

    def callback_error(self,kind,value,tb):
        import traceback
        self.result['error']=''.join(traceback.format_exception(kind,value,tb)); self.record(); self.close()

    def close(self,_event=None):
        if self.phase=='saving': self.message='저장과 읽기 검증이 끝난 뒤 종료할 수 있습니다.'; return
        self.closed=True; self.stop.set()
        if self.worker: self.worker.join(timeout=2)
        self.record(); self.root.destroy()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='AUTO'); parser.add_argument('--display',default='DISPLAY2')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    display=target_display(args.display)
    if display['width']/display['height'] <= 1.4: raise ValueError('This guide requires the current wide display')
    args.output.mkdir(parents=True,exist_ok=False)
    name=wait_for_port(args.port)
    before=snapshot(name)
    (args.output/'settings-before.json').write_text(json.dumps(before,indent=2),encoding='utf-8')
    if (before['version'],before['board']) != ('6.1-ca30987-obir1','rpipico'): raise ValueError('Unexpected firmware')
    profile=bytes.fromhex(before['settings']['profiles']['CurrentProf'])[0]
    if before['settings']['profiles'][f'IrLayout/{profile}'] != '00000000': raise ValueError('Requires Square layout')
    root=tk.Tk(); Wizard(root,name,args.output,before,display); root.mainloop()


if __name__=='__main__': main()
