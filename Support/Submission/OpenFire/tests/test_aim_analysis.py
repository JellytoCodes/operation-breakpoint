from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from aim_analysis import AimSample, ObservationGeometry, aim_for_frame
from camera_view import CameraFrame
from diagnostics import DiagnosticParser
from led_detection import DetectionFrame, LEDTracker
from led_view import draw_detection_scene
from test_camera_view import Canvas
from test_led_detection import frame, packet

CAMERA = CameraFrame((150, 200, 1800, 200, 150, 900, 1800, 900, 1200, 650, 960, 550))


class AimTests(unittest.TestCase):
    def test_screen_plane_is_independent_of_raw_sensor_plane(self):
        raw = frame()
        sample = AimSample(raw, CAMERA)
        point, text = aim_for_frame(raw, sample, fresh=True, sample_fresh=True)
        self.assertEqual(point, (1200/1920, 650/1080))
        canvas = Canvas()
        draw_detection_scene(canvas, raw, LEDTracker(), fresh=True, fullscreen=True,
                             sample=sample, sample_fresh=True)
        screen = next(i[1] for i in canvas.items if i[2].get('tags') == 'aim_screen')
        marker = next(i[1] for i in canvas.items if i[2].get('tags') == 'screen_aim')
        x, y = (marker[0]+marker[2])/2, (marker[1]+marker[3])/2
        self.assertAlmostEqual((x-screen[0])/(screen[2]-screen[0]), point[0])
        self.assertAlmostEqual((y-screen[1])/(screen[3]-screen[1]), point[1])
        self.assertEqual(sum(i[2].get('tags') == 'camera_axis' for i in canvas.items), 2)

    def test_partial_stale_mismatched_or_degenerate_frames_hide_aim(self):
        raw = frame()
        sample = AimSample(raw, CAMERA)
        for value, fresh, sample_fresh in ((raw, False, True), (raw, True, False),
                                           (frame((0, 1, 2)), True, True), (frame(seq=2), True, True),
                                           (DetectionFrame(1, 15, 0, ((512, 384),)*4), True, True)):
            canvas = Canvas()
            draw_detection_scene(canvas, value, LEDTracker(), fresh=fresh, fullscreen=True,
                                 sample=sample, sample_fresh=sample_fresh)
            self.assertFalse(any(i[2].get('tags') == 'screen_aim' for i in canvas.items))

    def test_offscreen_aim_is_reported_without_clamping_to_edge(self):
        raw = frame()
        coords = CAMERA.coords[:8]+(-200, 1300)+CAMERA.coords[10:]
        sample = AimSample(raw, CameraFrame(coords))
        point, text = aim_for_frame(raw, sample, fresh=True, sample_fresh=True)
        self.assertLess(point[0], 0)
        self.assertGreater(point[1], 1)
        self.assertIn('왼쪽 아래', text)
        self.assertIn('X -10.4%', text)
        canvas = Canvas()
        draw_detection_scene(canvas, raw, LEDTracker(), fresh=True, fullscreen=True,
                             sample=sample, sample_fresh=True)
        self.assertFalse(any(i[2].get('tags') == 'screen_aim' for i in canvas.items))
        arrow = next(i[1] for i in canvas.items if i[2].get('tags') == 'offscreen_aim')
        screen = next(i[1] for i in canvas.items if i[2].get('tags') == 'aim_screen')
        self.assertLess(arrow[2], arrow[0])
        self.assertGreater(arrow[3], arrow[1])
        self.assertTrue(screen[0] <= arrow[2] <= screen[2])
        self.assertTrue(screen[1] <= arrow[3] <= screen[3])

    def test_offscreen_arrows_require_a_current_valid_paired_sample(self):
        raw = frame()
        outside = CameraFrame(CAMERA.coords[:8]+(2400, -120)+CAMERA.coords[10:])
        sample = AimSample(raw, outside)
        for fresh, sample_fresh in ((False, True), (True, False)):
            canvas = Canvas()
            draw_detection_scene(canvas, raw, LEDTracker(), fresh=fresh,
                                 sample=sample, sample_fresh=sample_fresh)
            self.assertFalse(any(i[2].get('tags') == 'offscreen_aim' for i in canvas.items))

    def test_parser_pairs_only_adjacent_complete_raw_and_legacy_packets(self):
        raw = frame(seq=91)
        legacy = b'\x96'+struct.pack('<12i', *CAMERA.coords)
        stream = packet(raw)+legacy+legacy
        for split in range(len(stream)+1):
            parser = DiagnosticParser()
            events = parser.feed(stream[:split])+parser.feed(stream[split:])
            samples = [v for kind, v in events if kind == 'sample']
            self.assertEqual(samples, [AimSample(raw, CAMERA)])
        for interrupted in (packet(raw)+b'noise'+legacy, packet(raw)+b'\x90\0'+legacy):
            self.assertFalse(any(k == 'sample' for k, _ in DiagnosticParser().feed(interrupted)))


class GeometryTests(unittest.TestCase):
    def test_distance_like_scale_shrinks_spans_and_separation(self):
        original = frame()
        first = ObservationGeometry.measure(original)
        scaled = DetectionFrame(2, 15, 0, tuple((int(512+(x-512)*.5), int(384+(y-384)*.5)) for x, y in original.slots))
        second = ObservationGeometry.measure(scaled)
        self.assertTrue(first.valid_quad and second.valid_quad)
        self.assertAlmostEqual(second.span_x/first.span_x, .5)
        self.assertAlmostEqual(second.span_y/first.span_y, .5)
        self.assertAlmostEqual(second.min_separation/first.min_separation, .5)

    def test_narrow_or_edge_observations_have_separate_advisories(self):
        narrow = DetectionFrame(1, 15, 0, ((500, 380), (510, 380), (500, 390), (510, 390)))
        near_edge = DetectionFrame(1, 15, 0, ((0, 0), (1023, 0), (0, 767), (1023, 767)))
        self.assertTrue(any('간격이 작' in n for n in ObservationGeometry.measure(narrow).notes))
        self.assertTrue(any('시야 경계' in n for n in ObservationGeometry.measure(near_edge).notes))
        self.assertEqual(ObservationGeometry.measure(frame((0, 1))).count, 2)

    def test_collinear_or_interior_blob_is_not_a_valid_four_corner_projection(self):
        for points in (((100, 100), (200, 100), (300, 100), (400, 100)),
                       ((100, 100), (500, 100), (300, 600), (300, 300))):
            self.assertFalse(ObservationGeometry.measure(DetectionFrame(1, 15, 0, points)).valid_quad)


if __name__ == '__main__': unittest.main()
