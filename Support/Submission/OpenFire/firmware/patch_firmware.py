"""Apply the small diagnostic extension only to the pinned 6.1 source tree."""
from pathlib import Path
import argparse
import shutil
import subprocess

BASE = 'ca30987ab07dfdf980f258683648469a023de0b0'


def patch(source):
    actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != BASE:
        raise ValueError(f'Requires OpenFIRE {BASE}; got {actual}')
    sketch = source / 'OpenFIREmain'
    edits = {
        'OpenFIREDefines.h': [('#define _OPENFIREDEFINES_H_', '#define _OPENFIREDEFINES_H_\n\n#define GIT_HASH "ca30987-obir2"')],
        'OpenFIREcommon.cpp': [
            ('#include "OpenFIREserial.h"', '#include "OpenFIREserial.h"\n#include "OBIRTelemetry.h"'),
            ('int mouseXscaled = mouseX / 4;', 'OBIRTelemetry::send(dfrIRPos->seen(), dfrIRPos->xPositions(), dfrIRPos->yPositions(),\n'
             '                                              OF_Prefs::profiles[OF_Prefs::currentProfile].irLayout);\n'
             '                        int mouseXscaled = mouseX / 4;')],
        'OpenFIREserial.cpp': [
            ('#include "OpenFIREserial.h"', '#include "OpenFIREserial.h"\n#include "OBIRTelemetry.h"'),
            ('case OF_Const::serialTerminator:\n        if(!FW_Common::justBooted)',
             'case 0x0A: // OBIR opt-in, no argument, custom builds only\n'
             '        OBIRTelemetry::enabled = true;\n        OBIRTelemetry::sequence = 0;\n        break;\n'
             '    case OF_Const::serialTerminator:\n        OBIRTelemetry::enabled = false;\n        if(!FW_Common::justBooted)')],
    }
    pending = {}
    for name, replacements in edits.items():
        path = sketch / name
        text = path.read_text(encoding='utf-8')
        text=text.replace('ca30987-obir1','ca30987-obir2').replace('ca30987-obir3','ca30987-obir2')
        for old, new in replacements:
            if new in text:
                continue
            if text.count(old) != 1:
                raise ValueError(f'Unexpected source around {name}: {old[:70]}')
            text = text.replace(old, new, 1)
        pending[path] = text
    for path, text in pending.items():
        text=text.replace('ca30987-obir1','ca30987-obir2').replace('ca30987-obir3','ca30987-obir2')
        path.write_text(text, encoding='utf-8', newline='\n')
    shutil.copyfile(Path(__file__).with_name('OBIRTelemetry.h'), sketch / 'OBIRTelemetry.h')
    for name in ('OBIREncoder.h','OBIRQuadrature.h'):
        shutil.copyfile(Path(__file__).with_name(name),sketch/name)
    additions={
        'OpenFIREmain.ino':[
            ('#include "OpenFIREconstant.h"','#include "OpenFIREconstant.h"\n#include "OBIREncoder.h"'),
            ('FW_Common::FeedbackSet();','FW_Common::FeedbackSet();\n    OBIREncoder::begin();')],
        'OpenFIREcommon.cpp':[
            ('#include "OBIRTelemetry.h"','#include "OBIRTelemetry.h"\n#include "OBIREncoder.h"'),
            ('int error = dfrIRPos->basicAtomic','OBIREncoder::poll();\n        int error = dfrIRPos->basicAtomic'),
            ('Serial.write(buf, sizeof(buf));\n                        }\n                    }\n',
             'Serial.write(buf, sizeof(buf));\n                        }\n                        OBIREncoder::send(); // After the adjacent raw+legacy pair.\n                    }\n')]
    }
    additions['OpenFIREserial.cpp']=[
        ('#include "OBIRTelemetry.h"','#include "OBIRTelemetry.h"\n#include "OBIREncoder.h"'),
        ('FW_Common::UpdateBindings(true);\n\n', 'FW_Common::UpdateBindings(true);\n                            OBIREncoder::begin(); // PinsReset also resets encoder power and pulls.\n\n')]
    for name,replacements in additions.items():
        path=sketch/name; text=path.read_text(encoding='utf-8')
        for old,new in replacements:
            if new in text: continue
            if text.count(old)!=1: raise ValueError(f'Unexpected encoder anchor in {name}: {old[:60]}')
            text=text.replace(old,new,1)
        path.write_text(text,encoding='utf-8',newline='\n')

    path=sketch/'OpenFIREmain.ino'
    text=path.read_text(encoding='utf-8')
    anchor='''void ExecRunModeProcessing()
{'''
    start=text.index(anchor)
    end=text.index('// For use with the OpenFIRE app',start)
    section=text[start:end]
    poll='        FW_Common::buttons.Poll(1);'
    addition='''
        // The stock IR-test loop polls buttons but never forwards them.
        // Emit native button indexes only for our explicit telemetry session.
        if(OBIRTelemetry::enabled) {
            for(uint i=0;i<ButtonCount;++i) {
                if(bitRead(FW_Common::buttons.pressed,i)) {
                    const uint8_t event[2]={OF_Const::sBtnPressed,uint8_t(i)};
                    Serial.write(event,sizeof(event));
                }
                if(bitRead(FW_Common::buttons.released,i)) {
                    const uint8_t event[2]={OF_Const::sBtnReleased,uint8_t(i)};
                    Serial.write(event,sizeof(event));
                }
            }
        }'''
    if(addition not in section):
        if section.count(poll)!=1: raise ValueError('Unexpected IR-test button poll anchor')
        section=section.replace(poll,poll+addition,1)
        text=text[:start]+section+text[end:]
        path.write_text(text,encoding='utf-8',newline='\n')


    # OB uses the measured serial/OSC bridge, never desktop HID injection.
    # Suppress at the final report boundary, including reconnect/exit/crash cases.
    hid = source/'libraries/TinyUSB_Devices/TinyUSB_Devices.cpp'
    text = hid.read_text(encoding='utf-8')
    for signature, flag in [('void AbsMouse5_::report(void)\n{', 'reportMouse'),
                            ('  void Keyboard_::report()\n  {', 'reportKeyboard'),
                            ('  void Gamepad16_::report() {', 'reportGamepad')]:
        quiet = signature + '\n    // OB_SERIAL_ONLY: desktop input stays disabled; game uses serial OSC.\n    TinyUSBDevices.newReport[TinyUSBDevices_::' + flag + '] = false;\n    return;\n'
        if quiet not in text:
            if text.count(signature) != 1: raise ValueError('Unexpected HID report boundary: '+signature)
            text = text.replace(signature, quiet, 1)
    hid.write_text(text, encoding='utf-8', newline='\n')
    defines=sketch/'OpenFIREDefines.h'
    defines.write_text(defines.read_text(encoding='utf-8').replace('ca30987-obir2','ca30987-obir3'), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    patch(parser.parse_args().source.resolve())
