from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from camera_view import CameraFrame, CameraProjection, draw_camera_scene, demo_frame


COORDS = (150, 200, 1800, 200, 150, 900, 1800, 900, 1200, 650, 960, 550)


class Canvas:
    """Capture drawing calls to verify the externally defined six-point contract."""
    def __init__(self, width=1920, height=1080): self.width, self.height, self.items = width, height, []
    def delete(self, _): self.items.clear()
    def winfo_width(self): return self.width
    def winfo_height(self): return self.height
    def create_rectangle(self, *coords, **options): self.items.append(('rect', coords, options))
    def create_line(self, *coords, **options): self.items.append(('line', coords, options))
    def create_text(self, *coords, **options): self.items.append(('text', coords, options))
    def create_oval(self, *coords, **options): self.items.append(('oval', coords, options))


class CameraViewTests(unittest.TestCase):
    def test_packet_order_and_reference_polygon_do_not_cross(self):
        frame = CameraFrame(COORDS)
        self.assertEqual(frame.points[4], (1200, 650))  # The gray aim is not the red median.
        self.assertEqual(frame.points[5], (960, 550))
        self.assertEqual(frame.polygon, ((150, 200), (1800, 200), (1800, 900), (150, 900), (150, 200)))

    def test_reference_app_point_colors_and_aim_are_preserved(self):
        canvas = Canvas()
        draw_camera_scene(canvas, CameraFrame(COORDS), fresh=True, fullscreen=True)
        dots = [item for item in canvas.items if item[0] == 'oval']
        self.assertEqual(len(dots), 6)
        self.assertEqual([dot[2]['outline'] for dot in dots],
                         ['#00ff00', '#00ff00', '#00ffff', '#00ffff', '#a0a0a0', '#ff4545'])
        self.assertEqual(dots[4][1], (1175., 625., 1225., 675.))
        self.assertEqual(dots[5][1], (935., 525., 985., 575.))
        box = [item for item in canvas.items if 'camera_box' in item[2].get('tags', ())]
        self.assertEqual([item[1] for item in box],
                         [(150., 200., 1800., 200.), (1800., 200., 1800., 900.),
                          (1800., 900., 150., 900.), (150., 900., 150., 200.)])

    def test_fullscreen_matches_display_and_small_preview_keeps_aspect(self):
        self.assertEqual(CameraProjection(3840, 2160, True).point((1200, 650)), (2400., 1300.))
        # As in the reference App, a non-16:9 fullscreen fills the entire display.
        self.assertEqual(CameraProjection(1280, 1024, True).point((1920, 1080)), (1280., 1024.))
        preview = CameraProjection(960, 220)
        x, y, w, h = preview.rect
        self.assertAlmostEqual(w/h, 16/9)
        self.assertEqual(preview.point((960, 540)), (480., 110.))

    def test_no_data_or_stale_frame_cannot_show_old_points_as_live(self):
        for frame in (None, CameraFrame(COORDS)):
            canvas = Canvas()
            draw_camera_scene(canvas, frame, fresh=False, status='수신 중단')
            self.assertFalse(any(item[0] == 'oval' for item in canvas.items))
            self.assertFalse(any('camera_box' in item[2].get('tags', ()) for item in canvas.items))
            self.assertTrue(any(item[2].get('text') == '수신 중단' for item in canvas.items))

    def test_offscreen_extrapolation_is_clipped_without_rewriting_frame(self):
        coords = (-150, 200, 1800, 200, -150, 900, 1800, 900, 1200, 650, 960, 550)
        frame = CameraFrame(coords)
        canvas = Canvas()
        draw_camera_scene(canvas, frame, fresh=True, fullscreen=True)
        self.assertEqual(frame.coords, coords)
        self.assertEqual(len([item for item in canvas.items if item[0] == 'oval']), 4)
        self.assertEqual(CameraProjection(1920, 1080, True).clipped_line((-150, 200), (1800, 200)), (0., 200., 1800., 200.))

    def test_demo_scene_is_marked_and_malformed_coordinates_rejected(self):
        canvas = Canvas()
        draw_camera_scene(canvas, demo_frame(1.), fresh=True, demo=True)
        self.assertTrue(any('모의' in item[2].get('text', '') for item in canvas.items))
        for coords in ((1,)*11, (float('nan'),)*12, (2**31,)*12, ('150',)*12):
            with self.subTest(coords=coords), self.assertRaises(ValueError): CameraFrame(coords)


if __name__ == '__main__':
    unittest.main()
