"""Windows/macOS HID fallback when the remote appears as a standard HID device."""

from __future__ import annotations

import sys
import threading
import time
from typing import Callable

from .protocol import WiimoteState, apply_report

NINTENDO_VID = 0x057E
WII_REMOTE_PIDS = {0x0306, 0x0330}


def hid_available() -> bool:
    try:
        import hid  # noqa: F401

        return True
    except ImportError:
        return False


def discover_devices() -> list[dict[str, str]]:
    if not hid_available():
        return []
    import hid

    found: list[dict[str, str]] = []
    seen: set[str] = set()
    for entry in hid.enumerate(NINTENDO_VID, 0):
        pid = entry.get("product_id", 0)
        if pid not in WII_REMOTE_PIDS:
            continue
        path = entry.get("path")
        if not path:
            continue
        key = path.decode() if isinstance(path, bytes) else str(path)
        if key in seen:
            continue
        seen.add(key)
        name = entry.get("product_string") or "Nintendo Wii Remote"
        found.append({"address": key, "name": str(name), "path": key})
    return found


class HidWiimote:
    def __init__(self, path: str, name: str = "Nintendo Wii Remote") -> None:
        self.path = path
        self.name = name
        self._device = None
        self._thread: threading.Thread | None = None
        self._running = False
        self._on_state: Callable[[WiimoteState], None] | None = None
        self._state = WiimoteState(connected=False, backend="hid", address=path, name=name)
        self._packet_times: list[float] = []

    def set_callback(self, cb: Callable[[WiimoteState], None]) -> None:
        self._on_state = cb

    def connect(self) -> None:
        if not hid_available():
            raise RuntimeError("hidapi is not installed (pip install hidapi).")
        import hid

        dev = hid.device()
        dev.open_path(self.path.encode() if isinstance(self.path, str) else self.path)
        dev.set_nonblocking(True)
        self._device = dev
        self._write_report(bytes([0x12, 0x00, 0x31]))
        self._write_report(bytes([0x11, 0x10]))
        self._state.connected = True
        self._emit()
        self._running = True
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def _write_report(self, payload: bytes) -> None:
        if self._device is None:
            return
        # Output reports on Windows often need a leading report ID (0x11 for output).
        report = bytes([0x11]) + payload
        try:
            self._device.write(report)
        except OSError:
            self._device.write(payload)

    def _read_loop(self) -> None:
        assert self._device is not None
        while self._running:
            try:
                packet = self._device.read(64)
            except OSError as exc:
                self._state.error = str(exc)
                self._state.connected = False
                self._emit()
                break
            if not packet:
                time.sleep(0.002)
                continue
            data = bytes(packet)
            self._state = apply_report(self._state, data)
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
        if self._device is not None:
            try:
                self._device.close()
            except OSError:
                pass
            self._device = None
        self._state.connected = False
        self._emit()


def platform_uses_hid() -> bool:
    return sys.platform == "win32"
