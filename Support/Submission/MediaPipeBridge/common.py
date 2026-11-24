from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

# Source tools and frozen helpers import the same calibration implementation.
if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'DeviceSettings'))
from device_settings import load_section, save_section, migrate_section, DeviceChannel


BASE_DIR = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
CONFIG_PATH = BASE_DIR / "config.json"


def load_config() -> dict[str, Any]:
    defaults = json.loads(CONFIG_PATH.read_text(encoding='utf-8-sig')) if CONFIG_PATH.exists() else {}
    return migrate_section('Camera', defaults)


def save_config(config: dict[str, Any]) -> None:
    save_section('Camera', config)


def resolve_osc_ip(config: dict[str, Any]) -> str:
    configured_ip = str(config.get("osc_ip", "AUTO")).strip()

    if not configured_ip or configured_ip.upper() == "AUTO":
        return "127.0.0.1"

    return configured_ip
