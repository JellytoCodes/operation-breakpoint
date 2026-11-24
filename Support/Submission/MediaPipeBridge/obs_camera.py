"""Use the selected camera's OBS source without opening its USB device twice."""
from __future__ import annotations

import configparser
import ctypes
import json
import os
import struct
import time
from pathlib import Path

from camera_devices import enumerate_cameras


def virtual_camera_ready() -> bool:
    """Read OBS's existing queue only; never create a mapping or start a service.

    OBS 32.1.2 shared/obs-shared-memory-queue/shared-memory-queue.c:
    uint32 write_idx, read_idx, state; READY=2. Advancing read_idx proves live output.
    """
    if os.name != 'nt':
        return False
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
    kernel.OpenFileMappingW.restype = ctypes.c_void_p
    kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t]
    kernel.MapViewOfFile.restype = ctypes.c_void_p
    kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenFileMappingW(4, False, 'OBSVirtualCamVideo')  # FILE_MAP_READ
    if not handle:
        return False
    view = None
    try:
        view = kernel.MapViewOfFile(handle, 4, 0, 0, 12)
        if not view:
            return False
        _, before, state = struct.unpack('<III', ctypes.string_at(view, 12))
        if state != 2:
            return False
        time.sleep(.08)
        _, after, state = struct.unpack('<III', ctypes.string_at(view, 12))
        return state == 2 and after != before
    finally:
        if view:
            kernel.UnmapViewOfFile(view)
        kernel.CloseHandle(handle)


def active_collection() -> dict:
    base = Path(os.environ.get('APPDATA', '')) / 'obs-studio'
    settings = configparser.ConfigParser(interpolation=None)
    settings.read([base / 'global.ini', base / 'user.ini'], encoding='utf-8-sig')
    name = settings.get('Basic', 'SceneCollectionFile', fallback='')
    if not name or Path(name).name != name:
        return {}
    if not name.endswith('.json'):
        name += '.json'
    return json.loads((base / 'basic/scenes' / name).read_text(encoding='utf-8-sig'))


def shared_camera_config(config: dict) -> dict:
    if not config.get('camera_obs_share', False) or not virtual_camera_ready():
        return config
    try:
        scene = active_collection()
        output = scene.get('virtual-camera', {})
        if output.get('type2') != 2:  # Source output; never feed game/display pixels into tracking.
            return config
        source = next((s for s in scene.get('sources', []) if s.get('name') == output.get('source')), {})
        if source.get('id') != 'dshow_input':
            return config
        cameras = enumerate_cameras()
        selected = next((c for c in cameras if c['index'] == int(config.get('camera_index', 0))), None)
        source_name = source.get('settings', {}).get('video_device_id', '').split(':', 1)[0]
        # A unique physical model prevents redirecting an explicit selection to another identical camera.
        if not selected or selected['name'] != source_name or sum(c['name'] == source_name for c in cameras) != 1:
            return config
        virtual = [c for c in cameras if c['name'] == 'OBS Virtual Camera']
        if len(virtual) != 1:
            return config
        print(f"[CAMERA] Sharing {source_name} through live OBS Virtual Camera; USB capture remains owned by OBS", flush=True)
        return {**config, 'camera_index': virtual[0]['index'], 'camera_backend': 'DSHOW',
                'camera_fourcc': '', 'camera_auto_fallback': False}
    except (OSError, ValueError, KeyError, configparser.Error):
        return config
