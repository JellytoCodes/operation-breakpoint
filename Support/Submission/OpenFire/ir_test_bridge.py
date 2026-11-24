"""Measured OpenFIRE IR -> OB loopback OSC, shared by game and aim test.

The physical trigger is forwarded for the isolated wall-impact test.
No HID pointer injection, synthetic button input, flash or calibration writes.
The existing diagnostics worker owns Dock/Undock for the lifetime of this test.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import queue
import signal
import threading
import time

from aim_analysis import aim_for_frame
from device_port import PortSelectionError, list_ports, reconnect_device
from diagnostics import run_diagnostics
from protocol import InputState, Sender
from aim_adjustment import load_adjustment
from device_settings import DeviceChannel, save_section

TIMEOUT = .25


def maintain_connection(preferred, stop, wake, emit, log_handshake, log_event):
    serial_number = None
    cycle = 0
    previous_wait_error = None
    ever_streamed = False
    while not stop.is_set():
        if wake.is_set():
            previous_wait_error = None
        wake.clear()
        if stop.is_set():
            break
        try:
            device = reconnect_device(list_ports.comports(), preferred, serial_number)
        except (PortSelectionError, OSError) as exc:
            if str(exc) != previous_wait_error:
                previous_wait_error = str(exc)
                log_event('port_wait', port=preferred, error=previous_wait_error)
                emit('link_lost', (preferred, previous_wait_error))
            wake.wait(1)
            continue
        previous_wait_error = None
        preferred = device.device
        serial_number = device.serial_number or serial_number
        cycle += 1
        streaming = False
        log_event('connecting', port=preferred, cycle=cycle)

        def forward(kind, value):
            nonlocal streaming, ever_streamed
            if kind == 'identity':
                if not value[0].endswith(('-obir1', '-obir2', '-obir3')):
                    raise ValueError('Measured four-LED IR test requires the obir1 firmware')
                emit('link_identified', (preferred, value))
            else:
                if kind == 'detection' and not streaming:
                    log_event('reconnected' if ever_streamed else 'connected', port=preferred, cycle=cycle)
                    streaming = ever_streamed = True
                emit(kind, value)

        try:
            run_diagnostics(preferred, wake, forward, attempts=2, stream_timeout=2,
                            on_handshake=lambda result: log_handshake(dict(cycle=cycle, **result)))
            message = 'F6에서 다시 연결 요청'
            event = 'manual_restart'
        except (OSError, ValueError, RuntimeError) as exc:
            message = str(exc)
            event = 'disconnected' if streaming else 'connection_failed'
        if stop.is_set():
            break
        log_event(event, port=preferred, cycle=cycle, error=message)
        emit('link_lost', (preferred, message))
        wake.wait(1)


class IRTestInput:
    def __init__(self, port, adjustment=None):
        self.input = InputState(device='OpenFIRE-IR:'+port, enabled=True)
        self.input.interaction_trigger = False
        self.input.interaction_trigger_count = 0
        self.detection = self.sample = None
        self.detection_at = self.sample_at = 0.
        self.identity = None
        self.error = ''
        self.trigger_down = False
        self.aux_down = {1: False, 2: False}
        self.adjustment=adjustment
        self.encoder_events=[]
        self.button_events=[]

    def accept(self, kind, value, received):
        if kind == 'identity':
            if not value[0].endswith(('-obir1','-obir2','-obir3')):
                raise ValueError('Measured four-LED IR test requires the obir1 firmware')
            self.identity = value
            if self.adjustment: self.adjustment.reset_session()
        elif kind=='encoder' and self.adjustment:
            previous=self.adjustment.last
            changed=self.adjustment.accept_encoder(value,received)
            if previous!=value:
                self.encoder_events.append(dict(host_s=received,position=value.position,presses=value.presses,
                                                trim_mm=self.adjustment.trim_mm,changed=changed))
                self.encoder_events=self.encoder_events[-64:]
        elif kind == 'detection':
            self.error = ''
            self.detection, self.detection_at = value, received
            self.sample = None
        elif kind == 'camera':
            self.sample = None
        elif kind == 'sample' and value.detection == self.detection:
            self.sample, self.sample_at = value, received
        elif kind in ('error', 'detection_error'):
            self.error = str(value)
            self.detection = self.sample = None
        elif kind == 'button':
            point=self.update(received).point
            self.button_events.append(dict(host_s=received,index=value[0],down=bool(value[1]),
                aim_valid=point is not None and all(0<=c<=1 for c in point)))
            self.button_events=self.button_events[-64:]
            if value[0] in (1,2):  # Native Button A: reload; Button B: calibration complete.
                index, down = value[0], bool(value[1])
                previous = self.aux_down[index]
                self.aux_down[index] = down
                # Physical latch survives aim loss/output resets. Only release
                # rearms it; an offline press is never queued for recovery.
                if not down or (not previous and self.input.connected):
                    flag=(1 if value[1] else 2) << (2*value[0])
                    self.input.raw_mouse(False,0,0,flag)
                return
            if value[0]!=0: return  # Firmware BtnIdx_Trigger.
            down = bool(value[1])
            if down and not self.trigger_down:
                # UI taps/holds do not need LED coordinates. Combat keeps its
                # separate aim-qualified counter, so reacquiring LEDs can never
                # turn an offscreen press into a delayed shot.
                if self.input.connected:
                    self.input.interaction_trigger = True
                    self.input.interaction_trigger_count += 1
                # Do not queue a shot while tracking is missing. A new press is
                # required after recovery; holding the switch cannot fire later.
                point = self.update(received).point
                if point is not None and all(0 <= coordinate <= 1 for coordinate in point):
                    self.input.raw_mouse(False, 0, 0, 1)
            elif not down:
                self.input.interaction_trigger = False
                self.input.raw_mouse(False, 0, 0, 2)
            self.trigger_down = down

    def update(self, now):
        self.input.applied_y_shift=self.adjustment.total_mm/self.adjustment.height_mm if self.adjustment else 0.
        fresh = self.detection is not None and 0 <= now-self.detection_at <= TIMEOUT
        self.input.connected = bool(self.identity and fresh and not self.error)
        self.input.point = None
        if fresh and self.input.connected:
            self.input.point, _ = aim_for_frame(self.detection, self.sample, fresh=True,
                sample_fresh=self.sample is not None and 0 <= now-self.sample_at <= TIMEOUT,adjustment=self.adjustment)
        if not self.input.connected:
            self.input.buttons = [False]*5
            self.input.interaction_trigger = False
        elif self.input.point is None or not all(0 <= coordinate <= 1 for coordinate in self.input.point):
            self.input.buttons[0] = False
        return self.input


def _run(args, channel):
    args.log_dir.mkdir(parents=True, exist_ok=True)
    stop = threading.Event()
    wake = threading.Event()
    messages = queue.SimpleQueue()
    adjustment=load_adjustment(args.display,args.offset_settings,args.screen_height_mm,args.y_offset_mm)
    state = IRTestInput(args.port,adjustment)
    port = args.port
    sender = Sender(args.udp_port)
    started = time.monotonic()
    counters = dict(samples=0, four_point_samples=0, valid_aim_samples=0)
    bounds = [float('inf'), float('-inf'), float('inf'), float('-inf')]
    finished = False
    handshake_started = datetime.now(timezone.utc).isoformat(timespec='milliseconds')
    log_lock = threading.Lock()

    def append_log(name, result):
        entry = dict(bridge_started_utc=handshake_started,
                     recorded_utc=datetime.now(timezone.utc).isoformat(timespec='milliseconds'),
                     **result)
        with log_lock:
            with (args.log_dir / name).open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(entry, ensure_ascii=False) + '\n')

    def log_handshake(result):
        append_log('handshake-attempts.jsonl', result)

    def log_event(event, **fields):
        append_log('connection-events.jsonl', dict(event=event, **fields))

    def emit(kind, value):
        messages.put((kind, value, time.monotonic()))

    def worker():
        try:
            maintain_connection(args.port, stop, wake, emit, log_handshake, log_event)
        except Exception as exc:
            emit('worker_error', str(exc))
        finally:
            emit('finished', None)

    def summary():
        latest = state.sample.normalized if state.sample is not None else None
        return dict(kind='physical OpenFIRE IR input bridge', port=port,
            identity=state.identity, error=state.error, **counters,
            received_leds=len(state.detection.points) if state.detection else None,
            latest_board_aim=latest, valid_now=state.input.point is not None,
            latest_adjusted_aim=state.input.point,y_up_mm=adjustment.total_mm,y_trim_mm=adjustment.trim_mm,
            screen_height_mm=adjustment.height_mm,encoder_received=bool(adjustment.received),
            x_shift=adjustment.x_shift,
            encoder_events=state.encoder_events,
            button_events=state.button_events,
            aim_bounds=bounds if counters['valid_aim_samples'] else None,
            trigger_presses=state.input.counts[0], trigger_held=state.input.buttons[0],
            interaction_trigger_presses=state.input.interaction_trigger_count,
            interaction_trigger_held=state.input.interaction_trigger,
            reload_presses=state.input.counts[1], magazine_inserted=state.aux_down[1],
            stopped=stop.is_set(), persisted_settings_changed=False)

    def save_summary():
        target = args.log_dir/'bridge-status.json'
        temp = target.with_suffix('.tmp')
        temp.write_text(json.dumps(summary(), ensure_ascii=False, indent=2), encoding='utf-8')
        temp.replace(target)

    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    last_saved = 0.
    next_commands = 0.
    try:
        with (args.log_dir/'ir-samples.csv').open('w', newline='', encoding='utf-8') as file:
            writer = csv.writer(file)
            writer.writerow(('host_s', 'sequence', 'seen', 'layout', 'count', 'aim_x', 'aim_y', 'valid'))
            while not stop.is_set() and not finished:
                now = time.monotonic()
                if now >= next_commands:
                    next_commands = now + .1
                    if 'reconnect' in channel.commands():
                        log_event('manual_request', port=port)
                        wake.set()
                while not messages.empty():
                    kind, value, received = messages.get_nowait()
                    if kind == 'finished':
                        finished = True
                        break
                    if kind == 'worker_error':
                        raise RuntimeError(value)
                    if kind == 'link_lost':
                        port, state.error = value
                        state.identity = state.detection = state.sample = None
                        sender.send(state.update(received))
                        continue
                    if kind == 'link_identified':
                        port, identity = value
                        state.error = ''
                        state.accept('identity', identity, received)
                        save_section('Gun',dict(port=port,display=args.display,udp_port=args.udp_port))
                        continue
                    state.accept(kind, value, received)
                    if kind == 'sample':
                        counters['samples'] += 1
                        if len(value.detection.points) == 4: counters['four_point_samples'] += 1
                        current = state.update(time.monotonic())
                        if current.point is not None:
                            counters['valid_aim_samples'] += 1
                            x, y = current.point
                            bounds[:] = min(bounds[0], x), max(bounds[1], x), min(bounds[2], y), max(bounds[3], y)
                        writer.writerow((received, value.detection.sequence, value.detection.seen,
                            value.detection.layout, len(value.detection.points), *value.normalized, current.point is not None))
                adjustment.reload_shared()
                current=state.update(time.monotonic())
                sender.send(current)
                channel.publish(dict(connected=int(current.connected),aim_valid=int(current.point is not None),
                    bridge_running=1, port=port, ammo_input='IR',x_shift=adjustment.x_shift,y_up_mm=adjustment.total_mm))
                adjustment.flush()
                if now-last_saved >= 1:
                    save_summary(); file.flush(); last_saved = now
                if args.stop_file and args.stop_file.exists(): break
                if args.duration and now-started >= args.duration: break
                stop.wait(1/60)
    except Exception as exc:
        if not state.error:
            state.error = str(exc)
        raise
    finally:
        stop.set()
        wake.set()
        thread.join(timeout=2.)  # Worker finally sends FE / Undock and closes COM.
        state.input.disconnect(); state.input.enabled = False
        for _ in range(3): sender.send(state.input)
        sender.close()
        channel.publish(dict(connected=0, aim_valid=0, bridge_running=0, port=port), force=True)
        channel.close()
        adjustment.flush(force=True)
        save_summary()
    return 1 if state.error else 0


def run(args):
    channel=DeviceChannel('gun')
    try:
        return _run(args,channel)
    finally:
        channel.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--udp-port', type=int)
    parser.add_argument('--log-dir', type=Path, required=True)
    parser.add_argument('--stop-file', type=Path)
    parser.add_argument('--duration', type=float, default=0)
    parser.add_argument('--display')
    parser.add_argument('--screen-height-mm',type=float)
    parser.add_argument('--y-offset-mm',type=float,default=50.)
    parser.add_argument('--offset-settings',type=Path)
    options = parser.parse_args(argv)
    if not 0 <= options.duration <= 7200: parser.error('Duration must be 0..7200 seconds')
    from device_settings import load_section
    shared=load_section('Gun')
    options.udp_port=options.udp_port or shared.get('udp_port',9011)
    options.display=options.display or shared.get('display','PRIMARY')
    options.port=options.port or shared.get('port','AUTO')
    return run(options)


if __name__ == '__main__':
    raise SystemExit(main())
