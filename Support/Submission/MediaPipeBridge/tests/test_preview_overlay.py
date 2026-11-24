import sys
from pathlib import Path
import unittest

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from preview_overlay import put_text, _text_patch


class PreviewOverlayTests(unittest.TestCase):
    def test_hangul_glyphs_are_rendered_and_cached(self):
        frame=np.zeros((100,400,3),dtype=np.uint8)
        put_text(frame,'플레이어 영역',(10,40),scale=.7,color=(0,255,0),thickness=2)
        self.assertGreater(np.count_nonzero(frame[:,:,1]),100)
        hits=_text_patch.cache_info().hits
        copy=np.zeros_like(frame)
        put_text(copy,'플레이어 영역',(10,40),scale=.7,color=(0,255,0),thickness=2)
        self.assertTrue(np.array_equal(frame,copy))
        self.assertGreater(_text_patch.cache_info().hits,hits)
        first=_text_patch('가',21,(255,255,255),0)[0]
        second=_text_patch('나',21,(255,255,255),0)[0]
        self.assertFalse(np.array_equal(first,second),'Hangul must render as distinct glyphs, not missing-character boxes')

    def test_clipping_at_all_frame_edges(self):
        frame=np.zeros((10,10,3),dtype=np.uint8)
        for point in [(-10,5),(5,5),(15,15),(-100,-100),(0,100)]:
            put_text(frame,'손 위치',point)
        self.assertEqual(frame.shape,(10,10,3))


if __name__=='__main__': unittest.main()
