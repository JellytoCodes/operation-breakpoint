from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT)); sys.path.insert(0,str(ROOT/'firmware'))
from aim_analysis import AimSample
from camera_view import CameraFrame
from led_detection import DetectionFrame
from ir_calibration import TARGETS,FIELDS,center_source,fit_profile,profile_point,sensor_corners,encoded_values
from recalibrate import expected_update,write_profile


def sample(target,order=(3,1,0,2)):
    final=[(2020+(u-target[0])*1250,1570+(v-target[1])*1400) for u,v in ((.3,0),(.7,0),(.3,1),(.7,1))]
    raw=[(round((4095-x)/4),round(y/4)) for x,y in final]
    exact=[(4095-4*x,4*y) for x,y in raw]
    legacy=[(int(x*1920/4092),int(y*1080/3072)) for x,y in exact]
    # Deliberately wrong old board aim must not enter the new fit.
    coords=tuple(v for p in legacy+[(4200,400),(960,540)] for v in p)
    return AimSample(DetectionFrame(1,15,0,tuple(raw[i] for i in order)),CameraFrame(coords))


class FitTests(unittest.TestCase):
    def captures(self): return [[sample(t) for _ in range(10)] for t in TARGETS]

    def test_fits_sight_center_and_all_axes_without_using_bad_board_aim(self):
        captures=self.captures(); values,report=fit_profile(captures)
        self.assertLess(abs(values['AdjX']-2020),3)
        self.assertLess(abs(values['AdjY']-1570),3)
        for target,batch in zip(TARGETS,captures):
            point=profile_point(batch[0],values)
            self.assertLess(max(abs(a-b) for a,b in zip(target,point)),.004)
        self.assertLess(max(report['fit_errors']),.004)

    def test_sensor_slot_order_does_not_change_correspondence(self):
        self.assertEqual(sensor_corners(sample((.5,.5))),sensor_corners(sample((.5,.5),(0,1,2,3))))

    def test_missing_or_mismatched_measurements_cannot_calibrate(self):
        s=sample((.5,.5))
        missing=AimSample(DetectionFrame(1,7,0,s.detection.slots),s.camera)
        with self.assertRaises(ValueError): center_source([missing])
        wrong=AimSample(s.detection,CameraFrame(tuple(v+100 for v in s.camera.coords)))
        with self.assertRaises(ValueError): sensor_corners(wrong)

    def test_reversed_or_insufficient_captures_are_rejected(self):
        captures=self.captures(); captures[3],captures[4]=captures[4],captures[3]
        with self.assertRaises(ValueError): fit_profile(captures)
        with self.assertRaises(ValueError): fit_profile(self.captures()[:4])

    def test_wrong_target_capture_fails_residual_check(self):
        captures=self.captures(); captures[4]=[sample((.70,.65))]*10
        with self.assertRaises(ValueError): fit_profile(captures)

    def test_bad_source_cannot_be_encoded_or_saved(self):
        values,_=fit_profile(self.captures()); values['AdjX']=4296.5
        with self.assertRaises(ValueError): encoded_values(values)


class Port:
    def __init__(self,corrupt=False): self.buffer=bytearray(); self.writes=[]; self.corrupt=corrupt
    @property
    def in_waiting(self): return len(self.buffer)
    def reset_input_buffer(self): pass
    def read(self,count):
        result=bytes(self.buffer[:count]); del self.buffer[:count]; return result
    def write(self,data):
        self.writes.append(data)
        if data==b'\x01\x02': self.buffer.extend(b'6.1-ca30987-obir1\xferpipico\xfeUSB')
        elif data==b'\xaa\x01': self.buffer.extend(b'\xaa')
        elif data[:1]==b'\xae':
            self.buffer.extend((b'X'+data[2:]) if self.corrupt else data[1:])
        elif data==b'\xfc': self.buffer.extend(b'\xfc\x01')
        elif data==b'\x06\x00\x00': self.buffer.extend(b'\x97\x00')
    def flush(self): pass


class SaveTests(unittest.TestCase):
    def values(self): return fit_profile(FitTests().captures())[0]
    def test_only_active_profile_calibration_fields_change(self):
        before=dict(version='6.1-ca30987-obir1',board='rpipico',settings=dict(pins={'Trigger':'0f'},profiles={'CurrentProf':'00','IrLayout/0':'00000000','Name/0':'old','AdjX/1':'unchanged'}))
        after,profile=expected_update(before,self.values())
        self.assertEqual(profile,0)
        self.assertEqual(after['settings']['pins'],before['settings']['pins'])
        self.assertEqual(after['settings']['profiles']['AdjX/1'],'unchanged')
        self.assertEqual(after['settings']['profiles']['Name/0'],'old')
        self.assertEqual(set(after['settings']['profiles'])-set(before['settings']['profiles']),{f'{k}/0' for k in FIELDS})

    def test_save_echo_ack_and_same_profile_refresh(self):
        port=Port()
        with patch('recalibrate.time.sleep'): write_profile(port,0,encoded_values(self.values()))
        self.assertEqual(len([w for w in port.writes if w[:1]==b'\xae']),8)
        self.assertEqual(port.writes[-3:],[b'\xfc',b'\x06\0\0',b'\xfe'])
        self.assertFalse(any(w in (b'\x0f',b'\x10') for w in port.writes))

    def test_echo_failure_aborts_without_save(self):
        port=Port(corrupt=True)
        with patch('recalibrate.time.sleep'),self.assertRaisesRegex(ValueError,'echo'):
            write_profile(port,0,encoded_values(self.values()))
        self.assertNotIn(b'\xfc',port.writes)
        self.assertEqual(port.writes[-2:],[b'\xfe',b'\xfe'])

    def test_non_calibration_write_is_rejected_before_opening_transaction(self):
        port=Port(); values=encoded_values(self.values()); values['Trigger']='0f'
        with self.assertRaises(ValueError): write_profile(port,0,values)
        self.assertEqual(port.writes,[])


if __name__=='__main__': unittest.main()
