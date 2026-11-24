"""Read a named display in physical pixels for the isolated test launcher."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json


def edid_height_mm(device_id):
    """Read advertised active-image height; reject ambiguous monitor models."""
    import winreg
    model=device_id.split('\\')[1]
    sizes=set()
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'SYSTEM\CurrentControlSet\Enum\DISPLAY'+'\\'+model) as key:
            index=0
            while True:
                try: instance=winreg.EnumKey(key,index)
                except OSError: break
                index+=1
                try:
                    with winreg.OpenKey(key,instance+r'\Device Parameters') as parameters:
                        data=winreg.QueryValueEx(parameters,'EDID')[0]
                    if len(data)>=128 and data[:8]==b'\x00\xff\xff\xff\xff\xff\xff\x00' and data[21] and data[22]:
                        sizes.add(data[22]*10)
                except OSError: pass
    except OSError: return None
    return sizes.pop() if len(sizes)==1 else None


def enumerate_displays():
    user = C.WinDLL('user32', use_last_error=True)
    # Match Unreal's per-monitor physical coordinates, independent of shell DPI.
    user.SetProcessDpiAwarenessContext.argtypes = [C.c_void_p]
    user.SetProcessDpiAwarenessContext(C.c_void_p(-4))

    class Info(C.Structure):
        _fields_ = [('size', W.DWORD), ('monitor', W.RECT), ('work', W.RECT),
                    ('flags', W.DWORD), ('device', W.WCHAR*32)]

    callback_type = C.WINFUNCTYPE(W.BOOL, W.HANDLE, W.HDC, C.POINTER(W.RECT), W.LPARAM)
    displays = []
    user.GetMonitorInfoW.argtypes = [W.HANDLE, C.POINTER(Info)]
    user.GetMonitorInfoW.restype = W.BOOL

    def receive(handle, _dc, _rect, _data):
        info = Info(); info.size = C.sizeof(info)
        if not user.GetMonitorInfoW(handle, C.byref(info)): return False
        r = info.monitor
        displays.append(dict(device=info.device, primary=bool(info.flags & 1),
                             x=r.left, y=r.top, width=r.right-r.left, height=r.bottom-r.top))
        return True

    callback = callback_type(receive)
    user.EnumDisplayMonitors.argtypes = [W.HDC, C.POINTER(W.RECT), callback_type, W.LPARAM]
    user.EnumDisplayMonitors.restype = W.BOOL
    if not user.EnumDisplayMonitors(None, None, callback, 0): raise C.WinError(C.get_last_error())
    return displays


def target_display(device='DISPLAY2'):
    displays=enumerate_displays()
    user=C.WinDLL('user32',use_last_error=True)
    selected = [d for d in displays if d['primary']] if device.upper() == 'PRIMARY' else [
        d for d in displays if d['device'].rsplit('\\', 1)[-1].upper() == device.upper()]
    if len(selected) != 1: raise RuntimeError(f'Could not identify one display: {device}')
    target = selected[0]

    class DisplayDevice(C.Structure):
        _fields_ = [('size', W.DWORD), ('name', W.WCHAR*32), ('description', W.WCHAR*128),
                    ('flags', W.DWORD), ('id', W.WCHAR*128), ('key', W.WCHAR*128)]

    user.EnumDisplayDevicesW.argtypes = [W.LPCWSTR, W.DWORD, C.POINTER(DisplayDevice), W.DWORD]
    user.EnumDisplayDevicesW.restype = W.BOOL
    for index in range(16):
        monitor = DisplayDevice(); monitor.size = C.sizeof(monitor)
        if not user.EnumDisplayDevicesW(target['device'], index, C.byref(monitor), 0): break
        if monitor.flags & 1 and not monitor.flags & 8:
            target['device_id'] = monitor.id
            target['physical_height_mm'] = edid_height_mm(monitor.id)
            return target
    raise RuntimeError(f'Could not identify the active monitor ID: {device}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', default='DISPLAY2')
    args = parser.parse_args()
    print(json.dumps(target_display(args.device)))
