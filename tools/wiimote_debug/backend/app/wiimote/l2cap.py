"""Linux BlueZ L2CAP connection to a Wii Remote (direct protocol, no kernel HID)."""

from __future__ import annotations

import socket
import sys
import threading
import time
from typing import Callable

from .protocol import KNOWN_NAMES, WiimoteState, apply_report

CONTROL_PSM = 17
DATA_PSM = 19

MODE_ACCEL = 0x31


def _platform_supports_l2cap() -> bool:
    return sys.platform == "linux" and hasattr(socket, "AF_BLUETOOTH")


def discover_devices(timeout_s: float = 6.0) -> list[dict[str, str]]:
    if not _platform_supports_l2cap():
        return []
    try:
        import bluetooth  # type: ignore
    except ImportError:
        return []

    devices: list[dict[str, str]] = []
    seen: set[str] = set()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for entry in bluetooth.find_service():
            name = entry.get("name") or ""
            host = entry.get("host") or ""
            if name in KNOWN_NAMES and host and host not in seen:
                seen.add(host)
                devices.append({"address": host, "name": name})
        if devices:
            break
        time.sleep(0.4)
    return devices


class L2capWiimote:
    def __init__(self, address: str, name: str = "") -> None:
        self.address = address
        self.name = name or "Nintendo Wii Remote"
        self._control: socket.socket | None = None
        self._data: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False
        self._is_tr = "-TR" in self.name
        self._on_state: Callable[[WiimoteState], None] | None = None
        self._state = WiimoteState(
            connected=False, backend="l2cap", address=address, name=self.name
        )
        self._packet_times: list[float] = []

    def set_callback(self, cb: Callable[[WiimoteState], None]) -> None:
        self._on_state = cb

    def connect(self) -> None:
        if not _platform_supports_l2cap():
            raise RuntimeError("L2CAP requires Linux with BlueZ (AF_BLUETOOTH).")

        control = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP)
        data = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP)
        control.connect((self.address, CONTROL_PSM))
        data.connect((self.address, DATA_PSM))
        data.settimeout(1.0)
        self._control = control
        self._data = data
        self._init_reporting()
        self._state.connected = True
        self._emit()
        self._running = True
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def _init_reporting(self) -> None:
        assert self._control is not None and self._data is not None
        cmd_set = 0xA2 if self._is_tr else 0x52
        send_sock = self._data if self._is_tr else self._control
        # Data reporting mode: accelerometer (0x31), continuous.
        send_sock.send(bytes([cmd_set, 0x12, 0x00, MODE_ACCEL]))
        # Request status report once.
        send_sock.send(bytes([cmd_set, 0x15, 0x00]))
        # LED 1 on — connection indicator.
        send_sock.send(bytes([cmd_set, 0x11, 0x10]))

    def _read_loop(self) -> None:
        assert self._data is not None
        while self._running:
            try:
                packet = self._data.recv(128)
            except (TimeoutError, socket.timeout, OSError):
                continue
            if not packet:
                self._state.error = "Remote disconnected"
                self._state.connected = False
                self._emit()
                break
            self._state = apply_report(self._state, packet)
            now = time.time()
            self._packet_times = [t for t in self._packet_times if now - t < 1.0]
            self._packet_times.append(now)
            self._state.packets_per_second = float(len(self._packet_times))
            self._emit()

    def _emit(self) -> None:
        if self._on_state:
            self._on_state(self._state)

    def disconnect(self) -> None:
        self._running = False
        for sock in (self._data, self._control):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        self._data = None
        self._control = None
        self._state.connected = False
        self._emit()
