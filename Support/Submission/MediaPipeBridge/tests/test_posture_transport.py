import unittest
from unittest.mock import Mock

from mediapipe_motion_bridge import CrouchStatePublisher, LiveFrameRate


class PostureTransportTests(unittest.TestCase):
    def test_late_receiver_recovers_a_held_crouch_without_a_new_transition(self):
        client = Mock()
        sender = CrouchStatePublisher(client, '/motion/crouch')
        sender.publish(True, 1.0)  # Receiver not running yet / packet lost.
        client.reset_mock()
        sender.publish(True, 1.1)
        client.send_message.assert_not_called()
        sender.publish(True, 1.21)
        client.send_message.assert_called_once_with('/motion/crouch', 1)
        sender.publish(False, 1.22)
        self.assertEqual(client.send_message.call_args.args, ('/motion/crouch', 0))

    def test_live_rate_reports_delivered_frames_instead_of_requested_30fps(self):
        rate = LiveFrameRate()
        for t in (0., .2, .4, .6):
            fps = rate.update(t)
        self.assertAlmostEqual(fps, 5.)
