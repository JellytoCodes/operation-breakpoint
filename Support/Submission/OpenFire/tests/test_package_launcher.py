import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import package_launcher as launcher

DISPLAY=dict(device='\\\\.\\DISPLAY2',device_id='display-2',width=3840,height=2160,x=0,y=0,primary=True)
XWF=dict(index=1,name='XWF-1080p6 Camera',id='@device:pnp:xwf')
WEBCAM=dict(index=2,name='USB Camera',id='@device:pnp:usb')
VIRTUAL=dict(index=0,name='OBS Virtual Camera',id='@device:sw:obs')


def port(name,usb_id=launcher.OPENFIRE_USB_ID):
    return SimpleNamespace(device=name,description=name,vid=usb_id[0],pid=usb_id[1])


class PackageLauncherTests(unittest.TestCase):
    def setUp(self):
        calibration=tempfile.TemporaryDirectory()
        self.addCleanup(calibration.cleanup)
        isolated=patch.dict(os.environ,{'OB_DEVICE_SETTINGS':str(Path(calibration.name)/'DeviceCalibration.ini')})
        isolated.start()
        self.addCleanup(isolated.stop)

    def test_display_and_identity_are_individual_arguments_without_legacy_input(self):
        args=launcher.game_arguments(DISPLAY,Path('a folder/learner.json'),port=19027)
        self.assertIn('-OBOpenFire',args); self.assertIn('-OBOpenFirePort=19027',args)
        self.assertIn('-ResX=3840',args); self.assertIn('-OBMonitorX=0',args)
        self.assertNotIn('-fullscreen',args)
        self.assertEqual(sum(a.startswith('-OBLearnerProfile=') for a in args),1)
        self.assertFalse(any('ESP32' in a or 'BNO' in a for a in args))
        with self.assertRaises(ValueError): launcher.game_arguments(DISPLAY,Path('learner.json'),extra=['-OBOpenFireDemo'])

    def test_mouse_launch_does_not_create_ir_receiver(self):
        args=launcher.game_arguments(DISPLAY,Path('learner.json'),controller=False)
        self.assertFalse(any(a.startswith('-OBOpenFire') for a in args))

    def test_config_rejects_unknown_legacy_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            self.assertEqual(launcher.load_config(root)['port'],'AUTO')
            (root/'OB.json').write_text(json.dumps({'sensor_mode':'rotation'}),encoding='utf-8')
            with self.assertRaises(ValueError): launcher.load_config(root)

    def test_demo_identity_is_generated_only_for_blank_fields(self):
        with patch.object(launcher.secrets,'token_hex',return_value='ab12'),patch.object(launcher.secrets,'randbelow',return_value=12345678):
            demo=launcher.launch_profile('','')
            self.assertEqual(demo.name,'시연병사AB12')
            self.assertRegex(demo.service_number,r'^[0-9]{2}-12345678$')
            self.assertEqual(launcher.launch_profile('홍길동','26-71000001').name,'홍길동')
            self.assertEqual(launcher.launch_profile('홍길동','').name,'홍길동')
            self.assertEqual(launcher.launch_profile('','26-71000001').service_number,'26-71000001')
            with self.assertRaises(ValueError):launcher.launch_profile('홍길동','잘못된 군번')

    def test_demo_sessions_receive_different_valid_profiles(self):
        with patch.object(launcher.secrets,'randbelow',side_effect=range(20)):
            profiles=[launcher.launch_profile('','') for _ in range(20)]
        self.assertEqual(len({p.service_number for p in profiles}),20)
        for profile in profiles:launcher.validate_learner_profile(profile.name,profile.service_number)

    def test_device_selection_is_saved_without_replacing_calibration(self):
        from device_settings import load_section,save_section
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); config=launcher.load_config(root)
            save_section('Camera',{'minimum_visibility':.75,'mirror_input':True})
            camera=dict(index=3,name='선택한 카메라',id='stable-camera-id')
            selected=launcher.save_launch_selection(config,'COM9',camera,'DISPLAY2')
            self.assertTrue(selected['controller'] and selected['mediapipe'])
            self.assertEqual(load_section('Gun')['port'],'COM9')
            self.assertEqual(load_section('Gun')['display'],'DISPLAY2')
            shared=load_section('Camera')
            self.assertEqual(shared['camera_id'],'stable-camera-id')
            self.assertEqual(shared['camera_index'],3)
            self.assertEqual(shared['minimum_visibility'],.75)
            self.assertTrue(shared['mirror_input'])
            self.assertFalse(shared['camera_auto_fallback'])
            self.assertEqual(launcher.load_config(root),selected)
            disabled=launcher.save_launch_selection(selected,None,None,'PRIMARY')
            self.assertFalse(disabled['controller'] or disabled['mediapipe'])
            self.assertEqual(disabled['port'],'COM9')
            self.assertEqual(load_section('Camera')['camera_id'],'stable-camera-id')
            self.assertFalse((root/'OB.json').exists())

    def test_camera_start_is_delegated_to_game_with_saved_identity(self):
        from device_settings import save_section
        with tempfile.TemporaryDirectory() as folder:
            save_section('Camera', {'camera_id': 'camera-id-with-spaces'})
            report=self.execute(folder,[],name='',number='')
            self.assertEqual(report['camera_id'],'camera-id-with-spaces')
            self.assertIn('-OBMediaPipe', report['game_arguments'])
            profile=next(Path(folder).glob('Saved/Launcher/*/learner-*.json'))
            self.assertTrue(json.loads(profile.read_text(encoding='utf-8'))['learner_name'].startswith('시연병사'))

    def execute(self,folder,events,*,mediapipe=True,name='테스트',number='26-71000001'):
        root=Path(folder); (root/'OperationBreakpoint.exe').touch()
        class Child:
            def __init__(self,command,directory,name,stop_file=None):
                self.name=name; self.command=command; self.folder=directory
            def start(self):events.append(self.name+' start')
            def check(self): pass
            def close(self):events.append(self.name+' close')
        class Game:
            pid=123;returncode=0
            def __init__(self,*a,**k):events.append('game start')
            polls=0
            def poll(self):
                self.polls+=1
                return None if self.polls==1 else 0
        with patch.object(launcher,'ManagedChild',Child),patch.object(launcher,'target_display',return_value=DISPLAY), \
             patch.object(launcher.subprocess,'Popen',Game):
            return launcher.run_session(root,launcher.load_config(root),name,number,mediapipe=mediapipe,status=lambda _:None)

    def test_game_opens_before_device_calibration_and_exit_releases_helpers(self):
        with tempfile.TemporaryDirectory() as folder:
            events=[]; report=self.execute(folder,events)
            self.assertEqual(events,['IR start','game start','IR close'])
            self.assertEqual(report['ir_port'],'AUTO');self.assertEqual(report['game_exit_code'],0)
            profile=next(Path(folder).glob('Saved/Launcher/*/learner-*.json'))
            self.assertEqual(json.loads(profile.read_text(encoding='utf-8'))['learner_id'],'26-71000001')

    def test_disabled_camera_is_not_started_with_the_game(self):
        with tempfile.TemporaryDirectory() as folder:
            events=[]
            report=self.execute(folder,events,mediapipe=False)
            self.assertTrue(report['game_started'])
            self.assertNotIn('-OBMediaPipe',report['game_arguments'])

    def test_auto_launch_uses_live_devices_instead_of_old_pc_defaults(self):
        from device_settings import save_section,load_section
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            save_section('Gun',{'port':'COM6','display':'DISPLAY9'})
            save_section('Camera',{'camera_id':'@device:pnp:disconnected','minimum_visibility':.75})
            with patch.object(launcher,'SingleInstance'), patch.object(launcher.list_ports,'comports',return_value=[port('COM17'),port('COM6',(1,2))]), \
                 patch.object(launcher,'enumerate_cameras',return_value=[VIRTUAL,WEBCAM]), \
                 patch.object(launcher,'enumerate_displays',return_value=[DISPLAY]), \
                 patch.object(launcher,'show_setup') as dialog, patch.object(launcher,'run_session') as run:
                self.assertEqual(launcher.main(['--package',folder]),0)
            dialog.assert_not_called()
            run.assert_called_once()
            self.assertEqual(run.call_args.args[1]['port'],'COM17')
            self.assertEqual(run.call_args.args[1]['display'],'PRIMARY')
            self.assertEqual(load_section('Camera')['camera_id'],WEBCAM['id'])
            self.assertEqual(load_section('Camera')['minimum_visibility'],.75)
            self.assertFalse((root/'OB.json').exists())

    def test_only_ambiguous_device_requires_selection(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(launcher,'SingleInstance'), \
             patch.object(launcher.list_ports,'comports',return_value=[port('COM6'),port('COM17')]), \
             patch.object(launcher,'enumerate_cameras',return_value=[VIRTUAL,XWF]), \
             patch.object(launcher,'enumerate_displays',return_value=[DISPLAY]), \
             patch.object(launcher,'show_setup') as dialog, patch.object(launcher,'run_session') as run:
            self.assertEqual(launcher.main(['--package',folder]),0)
            selected,pending,choices,_=dialog.call_args.args[3]
            self.assertEqual(pending,{'port'})
            self.assertEqual(set(choices['port'].values()),{'COM6','COM17'})
            self.assertEqual(selected['camera'],XWF)
            run.assert_not_called()

    def test_camera_prefers_saved_identity_then_unique_package_model(self):
        cameras=[VIRTUAL,XWF,WEBCAM]
        self.assertEqual(launcher.select_launch_camera(cameras,{'camera_id':WEBCAM['id']},XWF['name']),WEBCAM)
        self.assertEqual(launcher.select_launch_camera(cameras,{},XWF['name']),XWF)
        self.assertIsNone(launcher.select_launch_camera(cameras,{},None))
        self.assertIsNone(launcher.select_launch_camera([VIRTUAL],{},None))

    def test_missing_game_is_rejected_before_helpers(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(FileNotFoundError):
                launcher.run_session(Path(folder),launcher.load_config(Path(folder)),'테스트','26-71000001')


if __name__=='__main__': unittest.main()
