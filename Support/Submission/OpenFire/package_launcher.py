"""Portable OB launcher: learner identity, measured IR, posture bridge, game."""
import argparse
import ctypes
from datetime import datetime
import json
import secrets
from pathlib import Path
import subprocess
import sys
import time
import uuid
from ctypes import wintypes

if not getattr(sys,'frozen',False):
    support=Path(__file__).resolve().parents[2]
    for directory in ('Testing/Packaging','Submission/MediaPipeBridge','Submission/DeviceSettings'):
        sys.path.insert(0,str(support/directory))
from learner_profile import validate_learner_profile,write_learner_profile,game_arguments_for_learner
from display_target import target_display,enumerate_displays
from camera_devices import enumerate_cameras
from device_port import OPENFIRE_USB_ID, PortSelectionError, select_port
from device_settings import load_section, migrate_section, save_section
from serial.tools import list_ports
from ir_test_bridge import main as bridge_main


def package_root():
    return Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parents[3]/'Build/Submission/Windows'


def load_config(root):
    config=dict(display='DISPLAY1',port='AUTO',udp_port=9011,mediapipe=True,controller=True)
    path=root/'OB.json'
    if path.exists():
        value=json.loads(path.read_text(encoding='utf-8-sig'))
        if not isinstance(value,dict) or set(value)-set(config): raise ValueError('OB.json 설정 항목을 확인해 주세요.')
        config.update(value)
    if config['display'] not in ('PRIMARY',*[f'DISPLAY{i}' for i in range(1,17)]): raise ValueError('Invalid display')
    if not isinstance(config['port'],str) or type(config['udp_port']) is not int or not 1024<=config['udp_port']<=65535: raise ValueError('Invalid port')
    if type(config['mediapipe']) is not bool: raise ValueError('Invalid mediapipe setting')
    if type(config['controller']) is not bool: raise ValueError('Invalid controller setting')
    shared=migrate_section('Gun',{k:config[k] for k in ('display','port','udp_port')})
    for key in ('display','port','udp_port'): config[key]=shared.get(key,config[key])
    return config


def launch_profile(name,number):
    name=name.strip() or '시연병사'+secrets.token_hex(2).upper()
    number=number.strip() or datetime.now().strftime('%y')+f'-{secrets.randbelow(100000000):08d}'
    return validate_learner_profile(name,number)


def save_launch_selection(config,port,camera,display):
    selected={**config,'display':display,'controller':port is not None,'mediapipe':camera is not None}
    if port is not None:selected['port']=port
    save_section('Gun',{key:selected[key] for key in ('display','port','udp_port')})
    if isinstance(camera,dict):
        save_section('Camera',dict(camera_id=camera['id'],camera_name=camera['name'],camera_index=camera['index'],
                                   camera_backend='DSHOW',camera_auto_fallback=False))
    return selected


def select_launch_camera(cameras, saved, preferred_name):
    physical=[camera for camera in cameras if camera['id'].lower().startswith('@device:pnp:')
              and 'virtual' not in camera['name'].casefold()]
    for key,value in (('id',saved.get('camera_id')),('name',saved.get('camera_name')),('name',preferred_name)):
        if not value:
            continue
        matches=[camera for camera in physical if camera[key]==value]
        if matches:
            return matches[0] if len(matches)==1 else None
    return physical[0] if len(physical)==1 else None


def detect_launch_devices(args,root,config):
    ports=[]; cameras=[]; selected={'port':None,'camera':None}; pending=set()
    if not args.without_controller:
        try:
            ports=[port for port in list_ports.comports() if (port.vid,port.pid)==OPENFIRE_USB_ID]
            selected['port']=select_port(ports)
        except (PortSelectionError,OSError):
            pending.add('port')
    if not args.without_mediapipe:
        preferred_name=None
        try:
            preferred_name=json.loads((root/'MediaPipe/config.json').read_text(encoding='utf-8-sig')).get('camera_name')
        except (OSError,ValueError,AttributeError):
            pass
        try:
            cameras=enumerate_cameras()
            selected['camera']=select_launch_camera(cameras,load_section('Camera'),preferred_name)
        except OSError:
            pass
        if selected['camera'] is None:
            pending.add('camera')
    display=config['display']
    if display!='PRIMARY' and all(d['device'].rsplit('\\',1)[-1]!=display for d in enumerate_displays()):
        display='PRIMARY'
    choices={'port':{f'{p.device} · {p.description}':p.device for p in ports},
             'camera':{f"{c['index']}: {c['name']}":c for c in cameras}}
    return selected,pending,choices,display


def game_arguments(display,profile,*,controller=True,port=9011,extra=()):
    protected=('-obopenfire','-oblearnerprofile=','-obmonitorx=','-obmonitory=','-winx=','-winy=','-resx=','-resy=')
    if controller and any(a.lower().startswith(protected) for a in extra):
        raise ValueError('게임 추가 인수에서 IR·화면·사용자 설정을 덮어쓸 수 없습니다.')
    args=['-nosplash','-NoHMD','-ForceRes',f'-ResX={display["width"]}',f'-ResY={display["height"]}',
          f'-OBMonitorX={display["x"]}',f'-OBMonitorY={display["y"]}',f'-WinX={display["x"]}',f'-WinY={display["y"]}','-SAVEWINPOS=0']
    if controller: args += ['-OBOpenFire',f'-OBOpenFirePort={port}']
    return game_arguments_for_learner([*args,*extra],profile)


def worker_command(arguments):
    if getattr(sys,'frozen',False): return [sys.executable,'--bridge-worker',*arguments]
    return [sys.executable,str(Path(__file__).resolve()),'--bridge-worker',*arguments]


class SingleInstance:
    def __enter__(self):
        self.api=ctypes.WinDLL('kernel32',use_last_error=True)
        self.api.CreateMutexW.argtypes=[ctypes.c_void_p,wintypes.BOOL,wintypes.LPCWSTR]
        self.api.CreateMutexW.restype=wintypes.HANDLE
        self.api.CloseHandle.argtypes=[wintypes.HANDLE]
        ctypes.set_last_error(0)
        self.handle=self.api.CreateMutexW(None,False,'Local\\OperationBreakpoint.IRLauncher')
        if not self.handle: raise ctypes.WinError(ctypes.get_last_error())
        if ctypes.get_last_error()==183:
            self.api.CloseHandle(self.handle); self.handle=None
            raise RuntimeError('이미 OB 실행기가 켜져 있습니다. 기존 게임 또는 실행기를 먼저 종료해 주세요.')
        return self

    def __exit__(self,*_):
        if self.handle:self.api.CloseHandle(self.handle)


class ManagedChild:
    def __init__(self,command,folder,name,stop_file=None):
        self.command=list(map(str,command)); self.folder=Path(folder); self.name=name; self.stop_file=stop_file
        self.process=None; self.stream=None

    def start(self):
        self.folder.mkdir(parents=True,exist_ok=True)
        self.stream=(self.folder/(self.name+'.log')).open('w',encoding='utf-8')
        self.process=subprocess.Popen(self.command,cwd=str(self.folder),stdin=subprocess.DEVNULL,
            stdout=self.stream,stderr=subprocess.STDOUT,shell=False,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))

    def check(self):
        if self.process and self.process.poll() is not None: raise RuntimeError(f'{self.name} 연결이 종료됐습니다. {self.folder}의 로그를 확인해 주세요.')

    def close(self):
        try:
            if self.process and self.process.poll() is None:
                if self.stop_file: Path(self.stop_file).write_text('stop\n',encoding='utf-8')
                try: self.process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    # Only a helper process that this launcher created is targeted.
                    if sys.platform=='win32':
                        subprocess.run(['taskkill','/PID',str(self.process.pid),'/T','/F'],stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10,check=False)
                    else: self.process.terminate()
                    self.process.wait(timeout=8)
        finally:
            if self.stream: self.stream.close()


def run_session(root,config,name,number,*,controller=True,mediapipe=True,extra=(),status=print):
    root=Path(root).resolve(); profile=launch_profile(name,number)
    game=root/'OperationBreakpoint.exe'
    if not game.is_file(): raise FileNotFoundError(f'본 게임이 없습니다: {game}')
    display=target_display(config['display'])
    folder=root/'Saved/Launcher'/(datetime.now().strftime('%Y%m%d_%H%M%S')+'_'+uuid.uuid4().hex[:8])
    folder.mkdir(parents=True)
    learner=write_learner_profile(folder,profile)
    children=[]; game_process=None
    record=dict(display=display,physical_ir=controller,mediapipe=mediapipe,game_started=False)
    try:
        if controller:
            status('IR 건컨트롤러를 연결하고 있습니다…')
            port=config['port']  # Connection/reconnection is handled inside F6, without blocking game startup.
            offset=root/'Saved/ControllerInput/OpenFire/aim-adjustment.json'
            stop=folder/'ir.stop'
            ir=ManagedChild(worker_command(['--port',port,'--display',config['display'],'--udp-port',str(config['udp_port']),
                '--offset-settings',str(offset),'--log-dir',str(folder/'IR'),'--stop-file',str(stop)]),folder,'IR',stop)
            children.append(ir); ir.start()
            record['ir_port']=port
        args=game_arguments(display,learner,controller=controller,port=config['udp_port'],extra=extra)
        if mediapipe:
            record['camera_id']=load_section('Camera').get('camera_id')
            args.append('-OBMediaPipe')
        record['game_arguments']=args
        status('본 게임을 실행합니다…')
        game_process=subprocess.Popen([str(game),*args],cwd=str(root),shell=False)
        record.update(game_started=True,game_pid=game_process.pid)
        warned=False
        while game_process.poll() is None:
            if not warned:
                try:
                    for c in children: c.check()
                except RuntimeError as error:
                    record['helper_error']=str(error); status(str(error)+' · 게임의 F6/F7에서 다시 연결할 수 있습니다.'); warned=True
            time.sleep(.15)
        record['game_exit_code']=game_process.returncode
        if game_process.returncode: raise RuntimeError(f'게임이 오류 코드 {game_process.returncode}로 종료됐습니다.')
        return record
    except BaseException as error:
        record['error']=str(error); raise
    finally:
        for c in reversed(children): c.close()
        (folder/'session.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')


def launch_selected(args,root,config,selected,display,name,number,*,status=print):
    launch=save_launch_selection(config,selected['port'],selected['camera'],display)
    return run_session(root,launch,name,number,controller=launch['controller'],mediapipe=launch['mediapipe'],
                       extra=args.game_arg,status=status)


def show_setup(args,root,config,detection):
    import tkinter as tk
    from tkinter import ttk,messagebox
    import queue,threading

    window=tk.Tk()
    window.title('Operation Breakpoint · 장치 연결')
    frame=ttk.Frame(window,padding=24)
    frame.pack(fill='both',expand=True)
    ttk.Label(frame,text='실행 장치 확인',font=('맑은 고딕',21,'bold')).pack(anchor='w',pady=(0,8))
    ttk.Label(frame,text='연결할 장치를 선택하거나, 사용하지 않고 시작할 수 있습니다.',
              wraplength=540).pack(anchor='w')
    devices=ttk.Frame(frame,padding=(0,12))
    devices.pack(fill='x')
    devices.columnconfigure(1,weight=1)
    message=tk.StringVar()
    ttk.Label(frame,textvariable=message,wraplength=540).pack(anchor='w',pady=(0,10))

    labels={'port':'건컨트롤러','camera':'카메라'}
    selectors={}
    values={}
    detected,pending,choices,display=detection
    messages=queue.SimpleQueue()
    running=False

    def show_devices():
        for child in devices.winfo_children():
            child.destroy()
        selectors.clear()
        values.clear()
        for row,key in enumerate(labels):
            ttk.Label(devices,text=labels[key]).grid(row=row,column=0,sticky='w',padx=(0,14),pady=5)
            if key in pending:
                mapping=choices[key]
                value=tk.StringVar(value='')
                box=ttk.Combobox(devices,textvariable=value,state='readonly',
                                 values=[*mapping,'사용 안 함'])
                box.grid(row=row,column=1,sticky='ew',pady=5)
                selectors[key]=box
                values[key]=value
            else:
                item=detected[key]
                name=item['name'] if isinstance(item,dict) else item
                ttk.Label(devices,text=f'자동 선택 · {name}' if name else '사용 안 함',
                          wraplength=435).grid(row=row,column=1,sticky='w')
        if pending:
            missing=[labels[key] for key in labels if key in pending and not choices[key]]
            if missing:
                message.set('찾지 못한 장치 · '+', '.join(missing)+'\nUSB 연결을 확인한 뒤 새로고침해 주세요.')
            else:
                message.set('후보가 여러 개이거나 자동으로 고를 수 없는 장치가 있습니다.')
        else:
            message.set('장치를 확인했습니다. 게임을 시작할 수 있습니다.')

    def refresh():
        nonlocal detected,pending,choices,display
        detected,pending,choices,display=detect_launch_devices(args,root,config)
        show_devices()

    def start():
        nonlocal running
        if running:
            return
        selected=detected.copy()
        for key,value in values.items():
            option=value.get()
            if not option:
                message.set(labels[key]+'를 선택하거나 사용 안 함을 선택하세요.')
                return
            selected[key]=choices[key].get(option)
        running=True
        button.configure(state='disabled')
        refresh_button.configure(state='disabled')
        for selector in selectors.values():
            selector.configure(state='disabled')

        def work():
            try:
                launch_selected(args,root,config,selected,display,args.name,args.service_number,
                                status=lambda value:messages.put(('status',value)))
                messages.put(('done',None))
            except Exception as error:
                messages.put(('error',str(error)))
        threading.Thread(target=work,daemon=False).start()

    def tick():
        nonlocal running
        while not messages.empty():
            kind,value=messages.get()
            if kind=='status':
                message.set(value)
                if value.startswith('본 게임을 실행'):
                    window.withdraw()
                elif running:
                    window.deiconify()
            elif kind=='done':
                window.destroy()
                return
            elif kind=='error':
                running=False
                window.deiconify()
                button.configure(state='normal')
                refresh_button.configure(state='normal')
                for selector in selectors.values():
                    selector.configure(state='readonly')
                messagebox.showerror('OB 실행 오류',value,parent=window)
        window.after(100,tick)

    actions=ttk.Frame(frame)
    actions.pack(fill='x')
    refresh_button=ttk.Button(actions,text='새로고침',command=refresh)
    refresh_button.pack(side='left')
    button=ttk.Button(actions,text='게임 시작',command=start)
    button.pack(side='right')
    show_devices()
    window.update_idletasks()
    window.minsize(max(590,window.winfo_reqwidth()),window.winfo_reqheight())
    window.protocol('WM_DELETE_WINDOW',lambda: None if running else window.destroy())
    window.after(100,tick)
    if args.smoke_test:
        def smoke():
            args.smoke_test.parent.mkdir(parents=True,exist_ok=True)
            report=dict(title=window.title(),
                controller=detected['port'] is not None,mediapipe=detected['camera'] is not None,
                selectors={key:list(box['values']) for key,box in selectors.items()},
                detected={key:item['name'] if isinstance(item,dict) else item for key,item in detected.items()},
                size=[window.winfo_width(),window.winfo_height()],
                required=[window.winfo_reqwidth(),window.winfo_reqheight()])
            args.smoke_test.write_text(json.dumps(report,ensure_ascii=False),encoding='utf-8')
            window.destroy()
        window.after(500,smoke)
    window.mainloop()


def main(argv=None):
    try: ctypes.WinDLL('user32').SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except (AttributeError,OSError): pass
    values=list(sys.argv[1:] if argv is None else argv)
    if values[:1]==['--bridge-worker']:
        try:return bridge_main(values[1:])
        except Exception:return 1  # The bridge status JSON supplies the parent error; no worker popup.
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,default=package_root())
    parser.add_argument('--name',default='');parser.add_argument('--service-number',default='')
    parser.add_argument('--without-controller',action='store_true');parser.add_argument('--without-mediapipe',action='store_true')
    parser.add_argument('--no-ui',action='store_true');parser.add_argument('--smoke-test',type=Path)
    parser.add_argument('--game-arg',action='append',default=[])
    args=parser.parse_args(values); root=args.package.resolve(); config=load_config(root)
    with SingleInstance():
        if args.no_ui:
            run_session(root,config,args.name,args.service_number,controller=config['controller'] and not args.without_controller,
                mediapipe=config['mediapipe'] and not args.without_mediapipe,extra=args.game_arg)
        else:
            detected,pending,choices,display=detect_launch_devices(args,root,config)
            if args.smoke_test:
                show_setup(args,root,config,(detected,{'port','camera'},choices,display))
            elif pending:
                show_setup(args,root,config,(detected,pending,choices,display))
            else:
                launch_selected(args,root,config,detected,display,args.name,args.service_number)
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except Exception as error:
        if '--no-ui' not in sys.argv and '--bridge-worker' not in sys.argv:
            ctypes.WinDLL('user32').MessageBoxW(None,str(error),'OB 실행 오류',0x10)
        raise SystemExit(1)
