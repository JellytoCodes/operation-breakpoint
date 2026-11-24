import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from breach_guide import make_breach_guide_payload
from mediapipe_motion_bridge import BreachDetector, PlayerROI, PosePoint


class BreachGuideTests(unittest.TestCase):
    def payload(self, **changes):
        values = dict(frame_size=(640, 480), mirrored=False, mode=1, player_tracked=True,
                      left_roi=(.6475, .3875, .7825, .5625), right_roi=(.2175, .3875, .3525, .5625),
                      left_wrist=(.7, .45, .9), right_wrist=(.28, .45, .9), minimum_visibility=.6,
                      left_inside=True, right_inside=True, progress=1., ready=True)
        values.update(changes)
        return make_breach_guide_payload(**values)

    def test_actual_frame_dimensions_and_float_schema(self):
        p = self.payload(frame_size=(1920, 1080))
        self.assertEqual(len(p), 24)
        self.assertTrue(all(type(v) is float for v in p))
        self.assertEqual(p[1:3], [1920., 1080.])

    def test_post_mirror_coordinates_not_flipped_twice(self):
        p = self.payload(mirrored=True)
        self.assertEqual(p[3], 1.)
        self.assertEqual(p[14:16], [.7, .45])
        self.assertEqual(p[6:10], [.6475, .3875, .7825, .5625])

    def test_lost_or_low_visibility_hand_clears_success(self):
        for wrist in (None, (.7, .45, .59), (float('nan'), .45, .9)):
            with self.subTest(wrist=wrist):
                p = self.payload(left_wrist=wrist)
                self.assertEqual(p[14:17], [0., 0., 0.])
                self.assertEqual(p[20], 0.)
                self.assertEqual(p[22:24], [0., 0.])

    def test_lost_player_and_other_modes_clear_feedback(self):
        for change in (dict(player_tracked=False), dict(mode=2), dict(mode=3)):
            p = self.payload(**change)
            self.assertEqual(p[14:24], [0.] * 10)

    def test_invalid_geometry_rejected(self):
        for changes in (dict(frame_size=(0, 480)), dict(left_roi=(.5, .4, .5, .8)), dict(right_roi=(-.1, .1, .5, .5))):
            with self.assertRaises(ValueError):
                self.payload(**changes)

    def test_visibility_boundary_and_offscreen_landmark(self):
        self.assertEqual(self.payload(left_wrist=(.7, .45, .6))[16], .6)
        self.assertEqual(self.payload(left_wrist=(1.1, -.1, .9), left_inside=False)[14:16], [1.1, -.1])
        self.assertEqual(self.payload(progress=5)[22], 1.)

    def test_real_detector_hold_exit_and_reset(self):
        detector = BreachDetector(left_hand_roi=PlayerROI(.6475, .3875, .7825, .5625),
                                  right_hand_roi=PlayerROI(.2175, .3875, .3525, .5625),
                                  minimum_visibility=.6, stable_frames=4)
        left, right = PosePoint(.7, .45, 0, .9), PosePoint(.28, .45, 0, .9)
        for i in range(1, 5):
            l, r, ready = detector.update(left, right)
            p = self.payload(left_inside=l, right_inside=r, ready=ready, progress=detector.progress)
            self.assertEqual(p[22], i / 4)
            self.assertEqual(bool(p[23]), i == 4)
        l, r, ready = detector.update(PosePoint(.9, .45, 0, .9), right)
        self.assertFalse(ready)
        self.assertEqual(self.payload(left_inside=l, right_inside=r, ready=ready, progress=detector.progress)[22:24], [0., 0.])
        detector.update(left, right)
        detector.reset()
        self.assertEqual(detector.progress, 0.)

    def test_authored_two_hand_regions_are_half_size_and_raised(self):
        config = json.loads((Path(__file__).resolve().parents[1] / 'config.json').read_text(encoding='utf8'))
        regions = [PlayerROI(**config[key]) for key in ('breach_left_hand_roi', 'breach_right_hand_roi')]
        for region in regions:
            self.assertAlmostEqual(region.right - region.left, .0675)
            self.assertAlmostEqual(region.bottom - region.top, .0875)
            self.assertAlmostEqual((region.top + region.bottom) / 2, .325)
        detector = BreachDetector(left_hand_roi=regions[0], right_hand_roi=regions[1], minimum_visibility=.6, stable_frames=4)
        left, right = PosePoint(.715, .325, 0, .9), PosePoint(.285, .325, 0, .9)
        for _ in range(4):
            result = detector.update(left, right)
        self.assertEqual(result, (True, True, True))
        self.assertFalse(detector.update(PosePoint(.715, .475, 0, .9), right)[2])
        self.assertFalse(detector.update(PosePoint(.65, .325, 0, .9), right)[2])


if __name__ == '__main__':
    unittest.main()
