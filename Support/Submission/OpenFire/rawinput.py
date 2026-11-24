"""Windows device-specific absolute mouse input, with a private message window.

Never registers NOLEGACY, never hooks global keys, never samples GetCursorPos.
The original HID continues to work when the bridge is closed.
"""
import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import os


@dataclass(frozen=True)
class Device:
    handle: int
    path: str

    @property
    def label(self):
        return self.path.split('#')[1] if '#' in self.path else self.path


class RawInput:
    def __init__(self, callback):
        if os.name != 'nt':
            raise OSError('Windows Raw Input is required')
        self.callback = callback
        self.error = ''
        self.user = C.WinDLL('user32', use_last_error=True)
        kernel = C.WinDLL('kernel32', use_last_error=True)
        UINT = W.UINT
        PTR = C.c_size_t
        RESULT = C.c_ssize_t
        self.WNDPROC = C.WINFUNCTYPE(RESULT, W.HWND, UINT, PTR, C.c_ssize_t)

        class WindowClass(C.Structure):
            _fields_ = [('style', UINT), ('proc', self.WNDPROC), ('clsExtra', C.c_int),
                        ('wndExtra', C.c_int), ('instance', W.HINSTANCE), ('icon', W.HICON),
                        ('cursor', W.HANDLE), ('background', W.HBRUSH),
                        ('menu', W.LPCWSTR), ('name', W.LPCWSTR)]

        class Registration(C.Structure):
            _fields_ = [('page', W.USHORT), ('usage', W.USHORT),
                        ('flags', W.DWORD), ('target', W.HWND)]

        class DeviceList(C.Structure):
            _fields_ = [('handle', W.HANDLE), ('kind', W.DWORD)]

        class Header(C.Structure):
            _fields_ = [('kind', W.DWORD), ('size', W.DWORD),
                        ('device', W.HANDLE), ('wparam', PTR)]

        class Buttons(C.Structure):
            _fields_ = [('flags', W.USHORT), ('data', W.USHORT)]

        class ButtonUnion(C.Union):
            _fields_ = [('raw', W.ULONG), ('parts', Buttons)]

        class Mouse(C.Structure):
            _fields_ = [('flags', W.USHORT), ('buttons', ButtonUnion),
                        ('rawButtons', W.ULONG), ('x', W.LONG), ('y', W.LONG),
                        ('extra', W.ULONG)]

        self.Registration, self.DeviceList, self.Header, self.Mouse = Registration, DeviceList, Header, Mouse
        self.user.DefWindowProcW.argtypes = [W.HWND, UINT, PTR, C.c_ssize_t]
        self.user.DefWindowProcW.restype = RESULT
        self.user.RegisterClassW.argtypes = [C.POINTER(WindowClass)]
        self.user.RegisterClassW.restype = W.ATOM
        self.user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD,
                                            C.c_int, C.c_int, C.c_int, C.c_int,
                                            W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
        self.user.CreateWindowExW.restype = W.HWND
        self.user.RegisterRawInputDevices.argtypes = [C.POINTER(Registration), UINT, UINT]
        self.user.GetRawInputDeviceList.argtypes = [C.POINTER(DeviceList), C.POINTER(UINT), UINT]
        self.user.GetRawInputDeviceList.restype = UINT
        self.user.GetRawInputDeviceInfoW.argtypes = [W.HANDLE, UINT, C.c_void_p, C.POINTER(UINT)]
        self.user.GetRawInputDeviceInfoW.restype = UINT
        self.user.GetRawInputData.argtypes = [W.HANDLE, UINT, C.c_void_p, C.POINTER(UINT), UINT]
        self.user.GetRawInputData.restype = UINT
        self.user.DestroyWindow.argtypes = [W.HWND]
        self.user.UnregisterClassW.argtypes = [W.LPCWSTR, W.HINSTANCE]
        kernel.GetModuleHandleW.argtypes = [W.LPCWSTR]
        kernel.GetModuleHandleW.restype = W.HMODULE
        self.instance = kernel.GetModuleHandleW(None)
        self.class_name = 'OBOpenFireRaw_' + str(id(self))
        self.proc = self.WNDPROC(self._message)
        self.wc = WindowClass(0, self.proc, 0, 0, self.instance, None, None, None, None, self.class_name)
        if not self.user.RegisterClassW(C.byref(self.wc)):
            raise C.WinError(C.get_last_error())
        # HWND_MESSAGE: private window, no visible console or taskbar entry.
        self.window = self.user.CreateWindowExW(0, self.class_name, '', 0, 0, 0, 0, 0,
                                                W.HWND(-3), None, self.instance, None)
        if not self.window:
            self.user.UnregisterClassW(self.class_name, self.instance)
            raise C.WinError(C.get_last_error())
        rid = Registration(1, 2, 0x100 | 0x2000, self.window)  # INPUTSINK | DEVNOTIFY
        if not self.user.RegisterRawInputDevices(C.byref(rid), 1, C.sizeof(rid)):
            self.close()
            raise C.WinError(C.get_last_error())

    def _message(self, hwnd, message, wparam, lparam):
        if message == 0x00FF:  # WM_INPUT
            try:
                size = W.UINT()
                header_size = C.sizeof(self.Header)
                if self.user.GetRawInputData(lparam, 0x10000003, None, C.byref(size), header_size) == 0:
                    data = C.create_string_buffer(size.value)
                    got = self.user.GetRawInputData(lparam, 0x10000003, data, C.byref(size), header_size)
                    if got == size.value and size.value >= header_size + C.sizeof(self.Mouse):
                        header = self.Header.from_buffer_copy(data)
                        if header.kind == 0:
                            mouse = self.Mouse.from_buffer_copy(data, header_size)
                            self.callback(int(header.device or 0), bool(mouse.flags & 1),
                                          mouse.x, mouse.y, mouse.buttons.parts.flags)
            except Exception as error:
                self.error = str(error)
        # DefWindowProc must clean up foreground WM_INPUT after reading it.
        return self.user.DefWindowProcW(hwnd, message, wparam, lparam)

    def devices(self):
        count = W.UINT()
        if self.user.GetRawInputDeviceList(None, C.byref(count), C.sizeof(self.DeviceList)) == 0xFFFFFFFF:
            raise C.WinError(C.get_last_error())
        entries = (self.DeviceList * count.value)()
        got = self.user.GetRawInputDeviceList(entries, C.byref(count), C.sizeof(self.DeviceList))
        if got == 0xFFFFFFFF:
            raise C.WinError(C.get_last_error())
        result = []
        for entry in entries[:got]:
            if entry.kind != 0:
                continue
            length = W.UINT()
            self.user.GetRawInputDeviceInfoW(entry.handle, 0x20000007, None, C.byref(length))
            if not length.value:
                continue
            name = C.create_unicode_buffer(length.value + 1)
            if self.user.GetRawInputDeviceInfoW(entry.handle, 0x20000007, name, C.byref(length)) != 0xFFFFFFFF:
                result.append(Device(int(entry.handle), name.value))
        return result

    def close(self):
        if self.window:
            rid = self.Registration(1, 2, 1, None)  # REMOVE
            self.user.RegisterRawInputDevices(C.byref(rid), 1, C.sizeof(rid))
            self.user.DestroyWindow(self.window)
            self.window = None
            self.user.UnregisterClassW(self.class_name, self.instance)
