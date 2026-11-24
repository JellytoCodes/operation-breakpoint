from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from led_layout import LED_NAMES, square_targets, draw_alignment_guide
from led_detection import DetectionFrame, LEDTracker
from led_view import draw_detection_scene
from test_camera_view import Canvas


class LayoutTests(unittest.TestCase):
    def test_original_app_widescreen_and_four_three_ratios(self):
        self.assertEqual(square_targets(1920, 1080), ((576., 0.), (1344., 0.), (576., 1080.), (1344., 1080.)))
        self.assertEqual(square_targets(1600, 1200), ((320., 0.), (1280., 0.), (320., 1200.), (1280., 1200.)))
        self.assertEqual(square_targets(700, 500)[0][0], 140.)  # Original >1.4 threshold
        for width, height in ((0, 100), (100, 0), (-1, 100)):
            with self.assertRaises(ValueError): square_targets(width, height)

    def test_native_fullscreen_targets_meet_edges_on_each_resolution(self):
        for width, height in ((3840, 2160), (1920, 1080), (1280, 1024)):
            canvas = Canvas(width, height)
            points = draw_alignment_guide(canvas)
            markers = [i for i in canvas.items if 'guide_target' in i[2].get('tags', ())]
            self.assertEqual(len(markers), 4)
            for item, (x, y) in zip(markers, points):
                x0, y0, x1, y1 = item[1]
                self.assertEqual((x0+x1)/2, x)
                self.assertEqual((y0+y1)/2, y)
                self.assertIn(y, (0, height))
            self.assertEqual(sum(i[2].get('tags') == 'guide_column' for i in canvas.items), 2)
            for name in LED_NAMES:
                self.assertTrue(any(i[2].get('text', '').startswith(name+' · 가로') for i in canvas.items))

    def test_diamond_board_cannot_show_square_identity_as_confirmed(self):
        value = DetectionFrame(1, 15, 1, ((280, 150), (750, 150), (280, 610), (750, 610)))
        tracker = LEDTracker()
        self.assertFalse(tracker.begin(value, 1))
        tracker.positions, tracker.visible = list(value.points), [True]*4
        canvas = Canvas()
        draw_detection_scene(canvas, value, tracker, fresh=True, fullscreen=True)
        self.assertFalse(any(i[2].get('tags') == 'registered_outline' for i in canvas.items))
        self.assertTrue(any('Square 설정이 필요' in i[2].get('text', '') for i in canvas.items))
        self.assertTrue(any(i[2].get('text') == '1 상단 왼쪽 · 미확인' for i in canvas.items))


if __name__ == '__main__': unittest.main()
