"""One per-PC calibration store for Unreal and both device applications.

Values are JSON literals in an INI envelope. Readers see an atomic snapshot;
writers lock and merge only their own section (never overwrite other devices).
"""
import configparser
from contextlib import contextmanager
import json
import os
from pathlib import Path
import time
import uuid


def settings_path():
    override = os.environ.get('OB_DEVICE_SETTINGS')
    return Path(override) if override else Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'OperationBreakpoint' / 'DeviceCalibration.ini'


def runtime_dir():
    folder = settings_path().parent / 'DeviceRuntime'
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def _read():
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser.read(settings_path(), encoding='utf-8-sig')
    return parser


def _replace(source, target, *, optional=False):
    # Unreal briefly opens preview/config files without FILE_SHARE_DELETE.
    # A busy preview must drop one update, never terminate device recognition.
    for attempt in range(8):
        try:
            os.replace(source, target)
            return True
        except PermissionError:
            if optional:
                # Live preview has a newer frame in ~33ms. Never stall tracking
                # for up to 35ms waiting for Unreal to release the old file.
                return False
            if attempt == 7:
                raise
            time.sleep(.005)


def load_section(section, defaults=None):
    result = dict(defaults or {})
    parser = _read()
    if parser.has_section(section):
        for key, value in parser.items(section):
            try:
                result[key] = json.loads(value)
            except ValueError:
                result[key] = value
    return result


@contextmanager
def _lock():
    path = settings_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = path.with_suffix('.lock')
    deadline = time.monotonic() + 3
    while True:
        try:
            descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            break
        except FileExistsError:
            # A killed writer must not permanently disable calibration.
            try:
                if time.time() - lock.stat().st_mtime > 15:
                    lock.unlink(missing_ok=True)
            except FileNotFoundError:
                pass
            if time.monotonic() >= deadline:
                raise TimeoutError('DeviceCalibration.ini is busy')
            time.sleep(.02)
    try:
        os.close(descriptor)
        yield
    finally:
        lock.unlink(missing_ok=True)


def save_section(section, values, *, initialize=False):
    with _lock():
        parser = _read()
        if not parser.has_section(section):
            parser.add_section(section)
        for key, value in values.items():
            if not initialize or not parser.has_option(section, key):
                parser.set(section, key, json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False))
        path = settings_path()
        temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
        try:
            with temporary.open('w', encoding='utf-8', newline='\n') as stream:
                parser.write(stream, space_around_delimiters=False)
            _replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def migrate_section(section, defaults):
    save_section(section, defaults, initialize=True)
    return load_section(section, defaults)


class DeviceChannel:
    """Single owner per physical device, plus small local control files."""
    def __init__(self, name):
        self.name = name
        self.folder = runtime_dir()
        self.owner = self.folder / (name + '.owner')
        self.handle = self.owner.open('a+b')
        import msvcrt
        self.handle.seek(0)
        if self.owner.stat().st_size == 0:
            self.handle.write(b'0')
            self.handle.flush()
        self.handle.seek(0)
        try:
            msvcrt.locking(self.handle.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            self.handle.close()
            raise RuntimeError(f'{name} 장치가 이미 실행 중입니다.') from None
        self.started = time.time()
        self.last_preview = 0.
        self.preview_session = uuid.uuid4().hex
        self.preview_sequence = 0
        self.frame_id = ''

    def commands(self):
        result = []
        for path in sorted(self.folder.glob(self.name + '-*.cmd')):
            try:
                # Old commands from a previous run must never recalibrate a new user.
                stamp = path.stat()
                if stamp.st_mtime >= self.started - 1:
                    result.append((stamp.st_mtime_ns, path.name, path.read_text(encoding='utf-8-sig').strip()))
                path.unlink(missing_ok=True)
            except OSError:
                continue
        # UUID filenames are not chronological. A quick C4 -> two-hand switch
        # must apply the last requested mode, not whichever UUID sorts last.
        return [value for _, _, value in sorted(result)]

    def publish(self, values, frame=None, *, force=False):
        now = time.monotonic()
        # Leave headroom for the camera's 30fps cadence instead of decimating it
        # to 8-10fps. Gun status keeps its cheaper 10Hz update.
        interval = 1 / 60 if self.name == 'camera' else .1
        if not force and now - self.last_preview < interval:
            return
        self.last_preview = now
        if frame is not None:
            import cv2
            ok, encoded = cv2.imencode('.bmp', cv2.resize(frame, (960, 540)))
            if ok:
                temporary = self.folder / (self.name + '.tmp')
                temporary.write_bytes(encoded.tobytes())
                if _replace(temporary, self.folder / (self.name + '.bmp'), optional=True):
                    self.preview_sequence += 1
                    self.frame_id = f'{self.preview_session}-{self.preview_sequence}'
            values = dict(values, frame_id=self.frame_id)
        text = '[Live]\n' + '\n'.join(f'{key}={value}' for key, value in values.items()) + '\n'
        temporary = self.folder / (self.name + '.ini.tmp')
        temporary.write_text(text, encoding='utf-8')
        _replace(temporary, self.folder / (self.name + '.ini'), optional=True)

    def close(self):
        if not self.handle.closed:
            import msvcrt
            self.handle.seek(0)
            msvcrt.locking(self.handle.fileno(), msvcrt.LK_UNLCK, 1)
            self.handle.close()
