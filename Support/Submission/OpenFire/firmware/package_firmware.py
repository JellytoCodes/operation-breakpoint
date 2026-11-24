"""Package the UF2, modified firmware source, notices and reproducible metadata."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import zipfile


def package(source, build, output):
    output.mkdir(parents=True, exist_ok=True)
    uf2 = output / 'OpenFIRE-rpipico-obir3.uf2'
    shutil.copyfile(build / 'OpenFIREmain.ino.uf2', uf2)
    binary = uf2.read_bytes()
    ranges = []
    if len(binary) % 512:
        raise ValueError('UF2 has a partial block')
    for offset in range(0, len(binary), 512):
        magic1, magic2, flags, address, size, _, _, family = struct.unpack_from('<8I', binary, offset)
        if (magic1, magic2) != (0x0a324655, 0x9e5d5157) or size > 476 or not size:
            raise ValueError('Invalid UF2 block')
        if not flags & 0x2000 or family != 0xe48bff56:
            raise ValueError('UF2 is not for RP2040')
        if not 0x10000000 <= address < address+size <= 0x101ef000:
            raise ValueError('UF2 writes outside the pinned application partition')
        ranges.append((address, address+size))
    with zipfile.ZipFile(output / 'OpenFIRE-obir3-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob('*')):
            relative = path.relative_to(source)
            if path.is_file() and not any(p in ('.git', 'build', '__pycache__') for p in relative.parts):
                archive.write(path, 'OpenFIRE-obir3/'+relative.as_posix())
        for path in Path(__file__).parent.rglob('*'):
            relative = path.relative_to(Path(__file__).parent)
            if path.is_file() and '__pycache__' not in relative.parts:
                archive.write(path, 'OBIR-build/'+relative.as_posix())
    shutil.copyfile(source / 'LICENSE', output / 'LICENSE-OpenFIRE.txt')
    shutil.copyfile(Path(__file__).with_name('README.md'), output / '사용법.md')
    manifest = dict(version='6.1-ca30987-obir3', base='ca30987ab07dfdf980f258683648469a023de0b0',
                    board='rpipico (RP2040, 2MB)', core='rp2040:rp2040@4.5.4-tusb',
                    flags='freq=133,usbstack=tinyusb,flash=2097152_65536,opt=Optimize3',
                    sha256=hashlib.sha256(binary).hexdigest(),
                    application_range=f'{min(a for a, _ in ranges):#x}..{max(b for _, b in ranges)-1:#x}',
                    writes_filesystem=False)
    (output / 'manifest-obir3.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('source', 'build', 'output'): parser.add_argument(name, type=Path)
    args = parser.parse_args()
    package(args.source.resolve(), args.build.resolve(), args.output.resolve())
