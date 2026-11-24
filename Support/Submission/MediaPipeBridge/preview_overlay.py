"""Cached Korean text patches for the OpenCV preview and Unreal camera feed."""
from functools import lru_cache
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


@lru_cache(maxsize=16)
def _font(size):
    # Windows includes Malgun Gothic; it is also available to frozen helpers.
    path = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts/malgun.ttf'
    return ImageFont.truetype(str(path), size)


@lru_cache(maxsize=256)
def _text_patch(text, size, bgr, outline):
    font = _font(size)
    bounds = font.getbbox(text, stroke_width=outline)
    width, height = max(1,bounds[2]-bounds[0]), max(1,bounds[3]-bounds[1])
    patch = Image.new('RGBA', (width,height))
    ImageDraw.Draw(patch).text((-bounds[0],-bounds[1]), text, font=font,
        fill=(*reversed(bgr),255), stroke_width=outline, stroke_fill=(0,0,0,255))
    rgba = np.asarray(patch)
    alpha = rgba[:,:,3:4].astype(np.uint16)
    return rgba[:,:,:3][:,:,::-1].astype(np.uint16)*alpha, 255-alpha


def put_text(frame, text, position, _font_face=0, scale=.6, color=(255,255,255), thickness=1):
    """Use OpenCV's baseline-style call shape without re-rendering static labels."""
    if not text: return frame
    patch, inverse_alpha = _text_patch(text, max(14,round(scale*30)), tuple(color), int(thickness>1))
    height, width = patch.shape[:2]
    x, y = int(position[0]), int(position[1])-height
    left, top = max(0,x), max(0,y)
    right, bottom = min(frame.shape[1],x+width), min(frame.shape[0],y+height)
    if left < right and top < bottom:
        crop = np.s_[top-y:bottom-y,left-x:right-x]
        background = frame[top:bottom,left:right]
        background[:] = ((background.astype(np.uint16)*inverse_alpha[crop]+patch[crop]+127)//255).astype(np.uint8)
    return frame
