"""Optional readiness and graceful-stop protocol for the packaged launcher."""
import json
import os
from pathlib import Path


class LauncherControl:
    def __init__(self, ready_file=None, stop_file=None, token=None):
        if any((ready_file, stop_file, token)) and not all((ready_file, stop_file, token)):
            raise ValueError("--ready-file, --stop-file and --session-token must be provided together")
        self.ready_file = Path(ready_file) if ready_file else None
        self.stop_file = Path(stop_file) if stop_file else None
        self.token = token

    @property
    def managed(self):
        return self.ready_file is not None

    def _publish(self, status, **details):
        if self.managed:
            self.ready_file.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.ready_file.with_suffix(".tmp")
            temporary.write_text(json.dumps(dict(status=status, token=self.token, pid=os.getpid(), **details)), encoding="utf-8")
            temporary.replace(self.ready_file)

    def calibrating(self, progress):
        self._publish("calibrating", calibration_progress=min(max(float(progress), 0.0), 1.0), calibrated=False)

    def ready(self):
        # Camera inference and the player's standing calibration both succeeded.
        self._publish("ready", calibrated=True, calibration_progress=1.0)
        print("[MEDIAPIPE] Standing calibration complete; ready for game", flush=True)

    def should_stop(self):
        return self.managed and self.stop_file.exists()
