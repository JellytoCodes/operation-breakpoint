"""DirectShow camera identities in OpenCV CAP_DSHOW order, without opening cameras.

Uses the System Device Enumerator and IPropertyBag described at:
https://learn.microsoft.com/en-us/windows/win32/directshow/selecting-a-capture-device
"""
import ctypes as C
import uuid


class GUID(C.Structure):
    _fields_ = [("bytes", C.c_ubyte * 16)]

    def __init__(self, value):
        super().__init__()
        self.bytes[:] = uuid.UUID(value).bytes_le


class VariantValue(C.Union):
    _fields_ = [("bstr", C.c_void_p), ("record", C.c_void_p * 2)]


class Variant(C.Structure):
    _fields_ = [("vt", C.c_ushort), ("reserved", C.c_ushort * 3), ("value", VariantValue)]


def _call(pointer, slot, result, argtypes, *args):
    table = C.cast(pointer, C.POINTER(C.POINTER(C.c_void_p))).contents
    return C.WINFUNCTYPE(result, C.c_void_p, *argtypes)(table[slot])(pointer, *args)


def _check(hr):
    if hr < 0:
        raise OSError(f"Camera enumeration failed: HRESULT 0x{hr & 0xffffffff:08X}")


def _release(pointer):
    if pointer:
        _call(pointer, 2, C.c_ulong, [])


def enumerate_cameras():
    """Return index, name and stable moniker id; does not capture frames."""
    ole = C.WinDLL("ole32")
    automation = C.WinDLL("oleaut32")
    ole.CoInitializeEx.argtypes = [C.c_void_p, C.c_ulong]
    ole.CoInitializeEx.restype = C.c_long
    ole.CoCreateInstance.argtypes = [C.POINTER(GUID), C.c_void_p, C.c_ulong,
                                   C.POINTER(GUID), C.POINTER(C.c_void_p)]
    ole.CoTaskMemFree.argtypes = [C.c_void_p]
    automation.VariantClear.argtypes = [C.POINTER(Variant)]
    # S_OK/S_FALSE both need CoUninitialize; an existing STA is also usable.
    hr = ole.CoInitializeEx(None, 2)
    initialized = hr >= 0
    if hr != -2147417850:  # RPC_E_CHANGED_MODE: use the existing apartment.
        _check(hr)
    device_enum, monikers = C.c_void_p(), C.c_void_p()
    cameras = []
    try:
        _check(ole.CoCreateInstance(C.byref(GUID("62be5d10-60eb-11d0-bd3b-00a0c911ce86")), None, 1,
                                   C.byref(GUID("29840822-5b84-11d0-bd3b-00a0c911ce86")), C.byref(device_enum)))
        hr = _call(device_enum, 3, C.c_long, [C.POINTER(GUID), C.POINTER(C.c_void_p), C.c_ulong],
                   C.byref(GUID("860bb310-5d01-11d0-bd3b-00a0c911ce86")), C.byref(monikers), 0)
        _check(hr)
        if hr == 1:  # No video input devices.
            return []
        while True:
            moniker = C.c_void_p()
            hr = _call(monikers, 3, C.c_long, [C.c_ulong, C.POINTER(C.c_void_p), C.c_void_p],
                       1, C.byref(moniker), None)
            _check(hr)
            if hr == 1:
                break
            bag, display = C.c_void_p(), C.c_void_p()
            value = Variant()
            try:
                _check(_call(moniker, 20, C.c_long, [C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p)],
                             None, None, C.byref(display)))
                identity = C.wstring_at(display)
                _check(_call(moniker, 9, C.c_long, [C.c_void_p, C.c_void_p, C.POINTER(GUID), C.POINTER(C.c_void_p)],
                             None, None, C.byref(GUID("55272a00-42cb-11ce-8135-00aa004bb851")), C.byref(bag)))
                hr = _call(bag, 3, C.c_long, [C.c_wchar_p, C.POINTER(Variant), C.c_void_p],
                           "FriendlyName", C.byref(value), None)
                name = C.wstring_at(value.value.bstr) if hr >= 0 and value.vt == 8 else f"Camera {len(cameras)}"
                cameras.append(dict(index=len(cameras), name=name, id=identity))
            finally:
                automation.VariantClear(C.byref(value))
                if display:
                    ole.CoTaskMemFree(display)
                _release(bag)
                _release(moniker)
        return cameras
    finally:
        _release(monikers)
        _release(device_enum)
        if initialized:
            ole.CoUninitialize()


def resolve_camera(identity, cameras=None):
    cameras = enumerate_cameras() if cameras is None else cameras
    selected = next((camera for camera in cameras if camera["id"] == identity), None)
    if selected is None:
        raise RuntimeError("선택한 카메라의 연결이 변경되었습니다. 런처에서 카메라를 다시 선택해 주세요.")
    return selected


def selected_camera_config(config, identity):
    """Re-resolve after calibration; never fall back to a different camera."""
    camera = resolve_camera(identity)
    print(f"[CAMERA] Selected {camera['name']} / index={camera['index']} / DSHOW", flush=True)
    return {**config, "camera_index": camera["index"], "camera_backend": "DSHOW", "camera_auto_fallback": False}


def configured_camera_config(config, identity=None):
    """Pin the configured physical device, independently of virtual-camera indices.

    A unique model name permits moving the demonstration camera to another USB
    port. An explicit moniker is strict and never switches to another device.
    """
    identity = identity or config.get("camera_id")
    if identity:
        return selected_camera_config(config, identity)
    name = config.get("camera_name")
    if not name:
        return config
    matches = [camera for camera in enumerate_cameras() if camera["name"] == name]
    if not matches:
        raise RuntimeError(f"시연용 카메라 {name}가 연결되지 않았습니다. 본체 USB 연결을 확인해 주세요.")
    if len(matches) != 1:
        raise RuntimeError(f"{name}가 여러 대입니다. config.json의 camera_id로 사용할 장치를 지정해 주세요.")
    return selected_camera_config(config, matches[0]["id"])
