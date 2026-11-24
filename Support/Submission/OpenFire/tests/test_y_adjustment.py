from pathlib import Path
import sys
import struct
import tempfile
import unittest
import json
import os
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from aim_adjustment import AimAdjustment
from encoder import EncoderFrame
from diagnostics import DiagnosticParser
from led_detection import crc16
from protocol import InputState
from test_led_detection import frame,packet
from aim_analysis import AimSample
from camera_view import CameraFrame
from ir_test_bridge import IRTestInput


def encoder_packet(position,presses=0):
    body=b'OBYE\x01\x08'+struct.pack('<II',position,presses)
    return b'\x99'+body+struct.pack('<H',crc16(body))


class EncoderTests(unittest.TestCase):
    def test_fragmented_frames_and_raw_legacy_pair_are_preserved(self):
        raw=frame(); legacy=b'\x96'+struct.pack('<12i',*(0,)*8,960,540,960,540)
        stream=encoder_packet(10)+packet(raw)+legacy+encoder_packet(13,1)
        for i in range(len(stream)+1):
            parser=DiagnosticParser(); events=parser.feed(stream[:i])+parser.feed(stream[i:])
            self.assertEqual([v for k,v in events if k=='encoder'],[EncoderFrame(10,0),EncoderFrame(13,1)])
            self.assertEqual(len([1 for k,v in events if k=='sample']),1)

    def test_corrupt_frame_never_changes_trim_and_parser_recovers(self):
        broken=bytearray(encoder_packet(100)); broken[9]^=1
        events=DiagnosticParser().feed(broken+encoder_packet(5))
        self.assertEqual(events,[('encoder',EncoderFrame(5,0))])

    def test_cumulative_counts_direction_reset_and_wrap(self):
        a=AimAdjustment(390)
        a.accept_encoder(EncoderFrame(0,0)); a.accept_encoder(EncoderFrame(3,0))
        self.assertEqual(a.trim_mm,6.)
        a.accept_encoder(EncoderFrame(1,0)); self.assertEqual(a.trim_mm,2.)
        a.accept_encoder(EncoderFrame(0xffffffff,0)); self.assertEqual(a.trim_mm,-2.)
        a.accept_encoder(EncoderFrame(0,0)); self.assertEqual(a.trim_mm,0.)
        a.accept_encoder(EncoderFrame(4,0)); a.accept_encoder(EncoderFrame(4,1))
        self.assertEqual(a.trim_mm,0.); self.assertEqual(a.total_mm,50.)

    def test_reconnect_and_implausible_counter_jump_do_not_move_aim(self):
        a=AimAdjustment(390); a.accept_encoder(EncoderFrame(12,0)); a.accept_encoder(EncoderFrame(14,0))
        a.reset_session(); a.accept_encoder(EncoderFrame(0,0))
        self.assertEqual(a.trim_mm,4.)
        a.accept_encoder(EncoderFrame(20000,0)); self.assertEqual(a.trim_mm,4.)

    def test_trim_persists_without_board_flash_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'offsets.json'
            a=AimAdjustment(390,path=path,display_id='screen')
            a.accept_encoder(EncoderFrame(0,0)); a.accept_encoder(EncoderFrame(4,0)); a.flush(force=True)
            b=AimAdjustment(390,path=path,display_id='screen')
            self.assertEqual(b.trim_mm,8.)
            self.assertEqual(AimAdjustment(390,path=path,display_id='other').trim_mm,0.)

    def test_existing_100mm_limit_can_continue_in_both_directions(self):
        for start,direction in ((-100.,-1),(100.,1)):
            with self.subTest(start=start),tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'offsets.json'
                path.write_text(json.dumps({'displays':{'screen':{'trim_mm':start,'x_shift':.0123}}}),encoding='utf-8')
                a=AimAdjustment(390,path=path,display_id='screen')
                a.accept_encoder(EncoderFrame(20,0))
                a.accept_encoder(EncoderFrame(20+direction,0))
                self.assertEqual(a.trim_mm,start+direction*2.)
                self.assertEqual(a.x_shift,.0123)
                a.flush(force=True)
                self.assertEqual(AimAdjustment(390,path=path,display_id='screen').trim_mm,a.trim_mm)

    def test_wide_trim_limit_persists_reverses_and_reset_keeps_x_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'offsets.json'
            a=AimAdjustment(390,path=path,display_id='screen');a.x_shift=.0123
            a.accept_encoder(EncoderFrame(1000,0))
            for position in (1064,1128,1192,1256):
                a.accept_encoder(EncoderFrame(position,0))
            self.assertEqual(a.trim_mm,500.)
            a.flush(force=True)
            self.assertEqual(AimAdjustment(390,path=path,display_id='screen').trim_mm,500.)
            a.accept_encoder(EncoderFrame(1255,0));self.assertEqual(a.trim_mm,498.)
            for position in range(1191,678,-64):
                a.accept_encoder(EncoderFrame(position,0))
            self.assertEqual(a.trim_mm,-500.)
            a.flush(force=True)
            b=AimAdjustment(390,path=path,display_id='screen')
            self.assertEqual(b.trim_mm,-500.)
            b.accept_encoder(EncoderFrame(0,0));b.accept_encoder(EncoderFrame(1,0))
            self.assertEqual(b.trim_mm,-498.)
            b.accept_encoder(EncoderFrame(1,1))
            self.assertEqual(b.trim_mm,0.);self.assertEqual(b.x_shift,.0123)
            path.write_text(json.dumps({'displays':{'screen':{'trim_mm':501.}}}),encoding='utf-8')
            with self.assertRaises(ValueError):AimAdjustment(390,path=path,display_id='screen')


class OutputTests(unittest.TestCase):
    def test_shared_x_edits_reach_ir_coordinates_once_during_encoder_changes(self):
        from device_settings import save_section,load_section
        with tempfile.TemporaryDirectory() as directory,patch.dict(os.environ,{'OB_DEVICE_SETTINGS':str(Path(directory)/'devices.ini')}):
            adjustment=AimAdjustment(390,path=Path(directory)/'legacy.json',display_id='selected-screen',shared=True)
            bridge=IRTestInput('COM6',adjustment)
            bridge.accept('identity',('6.1-ca30987-obir3','rpipico'),10.)
            raw=frame();sample=AimSample(raw,CameraFrame((0,)*8+(960,540,960,540)))
            bridge.accept('detection',raw,10.);bridge.accept('sample',sample,10.)
            adjustment.accept_encoder(EncoderFrame(0,0),10.)
            adjustment.accept_encoder(EncoderFrame(1,0),10.01)
            for correction in (.1,-.2,.05):
                save_section(adjustment.shared_section,dict(x_shift=correction))
                adjustment.last_reload=0.;adjustment.reload_shared()
                packet=bridge.update(10.02).frame()
                self.assertAlmostEqual(packet['x'],.5+correction)
                self.assertAlmostEqual(packet['y'],.5-52/390)
                self.assertTrue(adjustment.dirty)
                self.assertEqual(adjustment.trim_mm,2.)
            adjustment.flush(force=True)
            saved=load_section(adjustment.shared_section)
            self.assertEqual(saved['x_shift'],.05)
            self.assertEqual(saved['trim_mm'],2.)

    def test_live_x_correction_is_not_blocked_by_pending_encoder_trim(self):
        adjustment=AimAdjustment(390)
        adjustment.shared=True
        adjustment.accept_encoder(EncoderFrame(0,0),10.)
        adjustment.accept_encoder(EncoderFrame(1,0),10.01)
        self.assertTrue(adjustment.dirty)
        with patch('aim_adjustment.load_section',return_value={'x_shift':-.1,'trim_mm':0.}):
            adjustment.reload_shared()
        self.assertEqual(adjustment.x_shift,-.1)
        self.assertEqual(adjustment.trim_mm,2.)
        self.assertTrue(adjustment.dirty)
        self.assertAlmostEqual(adjustment.apply((.5,.5))[0],.4)

    def test_saved_x_zero_preserves_y_trim_and_encoder_reset_only_resets_y(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'zero.json'
            path.write_text(json.dumps({'displays':{'screen':{'trim_mm':-16.,'x_shift':-.0328125}}}),encoding='utf-8')
            a=AimAdjustment(390,path=path,display_id='screen')
            self.assertEqual(a.total_mm,34.)
            self.assertAlmostEqual(a.apply((.5328125,.6))[0],.5)
            self.assertAlmostEqual(a.apply((.5328125,.6))[1],.6-34/390)
            a.accept_encoder(EncoderFrame(0,0)); a.accept_encoder(EncoderFrame(1,0))
            a.flush(force=True)
            b=AimAdjustment(390,path=path,display_id='screen')
            self.assertEqual(b.trim_mm,-14.)
            self.assertEqual(b.x_shift,-.0328125)
            b.accept_encoder(EncoderFrame(1,0)); b.accept_encoder(EncoderFrame(1,1))
            self.assertEqual(b.total_mm,50.)
            self.assertEqual(b.x_shift,-.0328125)

    def test_hid_and_ir_apply_x_zero_once_with_same_y_output(self):
        a=AimAdjustment(390); a.x_shift=-.0328125
        s=InputState(connected=True,enabled=True,point=(1000/1920,500/1080),
                     output_y_shift=50/390,output_x_shift=a.x_shift)
        expected=a.apply(s.point)
        for _ in range(3):
            self.assertEqual((s.frame()['x'],s.frame()['y']),expected)
        self.assertEqual(s.point,(1000/1920,500/1080))
        bridge=IRTestInput('COM6',a)
        bridge.accept('identity',('6.1-ca30987-obir2','rpipico'),10.)
        raw=frame(); sample=AimSample(raw,CameraFrame((0,)*8+(1000,500,960,540)))
        bridge.accept('detection',raw,10.); bridge.accept('sample',sample,10.)
        packet=bridge.update(10.05).frame()
        self.assertEqual((packet['x'],packet['y']),expected)

    def test_five_centimeters_moves_up_and_does_not_change_x_or_clamp(self):
        a=AimAdjustment(390)
        self.assertEqual(a.apply((.6,.5)),(.6,.5-50/390))
        self.assertLess(a.apply((.6,0))[1],0)
        with self.assertRaises(ValueError): AimAdjustment(0)

    def test_hid_output_applies_once_without_mutating_raw_point(self):
        s=InputState(connected=True,enabled=True,point=(.6,.5),output_y_shift=50/390)
        for _ in range(3): self.assertAlmostEqual(s.frame()['y'],.5-50/390)
        self.assertEqual(s.point,(.6,.5))

    def test_ir_trigger_uses_adjusted_point_and_loss_still_blocks_shots(self):
        a=AimAdjustment(390); bridge=IRTestInput('COM6',a)
        bridge.accept('identity',('6.1-ca30987-obir2','rpipico'),10.)
        raw=frame(); sample=AimSample(raw,CameraFrame((0,)*8+(1000,1200,960,540)))
        bridge.accept('detection',raw,10.); bridge.accept('sample',sample,10.)
        self.assertAlmostEqual(bridge.update(10.05).point[1],1200/1080-50/390)
        bridge.accept('button',(0,True),10.06)
        self.assertEqual(bridge.input.counts[0],1)
        bridge.accept('button',(0,False),10.07)
        bridge.accept('button',(0,True),11.)
        self.assertEqual(bridge.input.counts[0],1)


if __name__=='__main__': unittest.main()
