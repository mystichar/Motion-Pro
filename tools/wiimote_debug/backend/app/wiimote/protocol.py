"""Wii Remote report decoding and orientation helpers (Wiibrew protocol)."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

BUTTON_MASKS: dict[str, int] = {
    "Two": 0x0001,
    "One": 0x0002,
    "B": 0x0004,
    "A": 0x0008,
    "Minus": 0x0010,
    "Home": 0x0080,
    "Left": 0x0100,
    "Right": 0x0200,
    "Down": 0x0400,
    "Up": 0x0800,
    "Plus": 0x1000,
}

# Approximate zero-g bias and scale for built-in accelerometer (Wiibrew).
ACCEL_ZERO = 512.0
ACCEL_SCALE = 730.0

KNOWN_NAMES = ("Nintendo RVL-CNT-01", "Nintendo RVL-CNT-01-TR")


@dataclass
class WiimoteState:
    connected: bool = False
    backend: str = "none"
    address: str = ""
    name: str = ""
    report_type: int = 0
    buttons: dict[str, bool] = field(default_factory=lambda: {k: False for k in BUTTON_MASKS})
    accel_raw: tuple[int, int, int] = (512, 512, 512)
    accel_g: tuple[float, float, float] = (0.0, 0.0, 1.0)
    pitch_deg: float = 0.0
    roll_deg: float = 0.0
    magnitude_g: float = 1.0
    battery_low: bool = False
    extension_connected: bool = False
    report_hex: str = ""
    packets_per_second: float = 0.0
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "connected": self.connected,
            "backend": self.backend,
            "address": self.address,
            "name": self.name,
            "report_type": self.report_type,
            "buttons": dict(self.buttons),
            "accel_raw": list(self.accel_raw),
            "accel_g": [round(v, 4) for v in self.accel_g],
            "orientation_deg": {"pitch": round(self.pitch_deg, 2), "roll": round(self.roll_deg, 2)},
            "magnitude_g": round(self.magnitude_g, 4),
            "battery_low": self.battery_low,
            "extension_connected": self.extension_connected,
            "report_hex": self.report_hex,
            "packets_per_second": round(self.packets_per_second, 1),
            "error": self.error,
        }


def decode_buttons(payload: bytes) -> dict[str, bool]:
    if len(payload) < 2:
        return {k: False for k in BUTTON_MASKS}
    btn_word = (payload[0] << 8) | payload[1]
    return {name: bool(btn_word & mask) for name, mask in BUTTON_MASKS.items()}


def decode_accel(payload: bytes) -> tuple[int, int, int] | None:
    """Decode accelerometer from report payload (after report ID byte)."""
    if len(payload) < 5:
        return None
    report_id = payload[0]
    if report_id not in (0x31, 0x33, 0x35, 0x3e, 0x3f):
        return None
    x_msb, y_msb, z_msb = payload[3], payload[4], payload[5]
    x = (x_msb << 2) + ((payload[1] >> 5) & 0x03)
    y = (y_msb << 2) + ((payload[2] >> 4) & 0x02)
    z = (z_msb << 2) + ((payload[2] >> 5) & 0x02)
    return x, y, z


def raw_accel_to_g(raw: tuple[int, int, int]) -> tuple[float, float, float]:
    return tuple((v - ACCEL_ZERO) / ACCEL_SCALE for v in raw)  # type: ignore[return-value]


def estimate_tilt_deg(accel_g: tuple[float, float, float]) -> tuple[float, float, float]:
    ax, ay, az = accel_g
    pitch = math.degrees(math.atan2(ax, math.sqrt(ay * ay + az * az)))
    roll = math.degrees(math.atan2(ay, math.sqrt(ax * ax + az * az)))
    magnitude = math.sqrt(ax * ax + ay * ay + az * az)
    return pitch, roll, magnitude


def apply_report(state: WiimoteState, packet: bytes) -> WiimoteState:
    if len(packet) < 2:
        return state
    # Input reports are prefixed with 0xA1 on L2CAP; HID may omit it.
    payload = packet[1:] if packet[0] == 0xA1 else packet
    if not payload:
        return state

    state.report_type = payload[0]
    state.report_hex = packet.hex(" ")
    state.buttons = decode_buttons(payload[1:])

    if payload[0] == 0x20:
        status = payload[1] if len(payload) > 1 else 0
        state.battery_low = bool(status & 0x01)
        state.extension_connected = bool(status & 0x02)

    accel = decode_accel(payload)
    if accel is not None:
        state.accel_raw = accel
        state.accel_g = raw_accel_to_g(accel)
        state.pitch_deg, state.roll_deg, state.magnitude_g = estimate_tilt_deg(state.accel_g)

    return state
