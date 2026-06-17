"""Simulated Wii Remote for UI development without hardware."""

from __future__ import annotations

import math
import threading
import time
from typing import Callable

from .protocol import WiimoteState, apply_report


class MockWiimote:
    def __init__(self) -> None:
        self._thread: threading.Thread | None = None
        self._running = False
        self._on_state: Callable[[WiimoteState], None] | None = None
        self._state = WiimoteState(
            connected=False,
            backend="mock",
            address="mock://local",
            name="Mock Wii Remote",
        )
        self._t0 = time.time()

    def set_callback(self, cb: Callable[[WiimoteState], None]) -> None:
        self._on_state = cb

    def connect(self) -> None:
        self._state.connected = True
        self._state.error = ""
        self._emit()
        self._running = True
        self._t0 = time.time()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self) -> None:
        while self._running:
            t = time.time() - self._t0
            ax = int(512 + 120 * math.sin(t * 0.7))
            ay = int(512 + 90 * math.sin(t * 1.1))
            az = int(512 + 200 * math.cos(t * 0.5))
            btn_hi = 0x08 if int(t) % 4 == 0 else 0x00
            packet = bytes([
                0xA1,
                0x31,
                btn_hi | ((ax & 0x03) << 5),
                ((ay & 0x02) << 4) | ((az & 0x02) << 5),
                (ax >> 2) & 0xFF,
                (ay >> 2) & 0xFF,
                (az >> 2) & 0xFF,
            ])
            self._state = apply_report(self._state, packet)
            self._state.packets_per_second = 50.0
            self._emit()
            time.sleep(0.02)

    def _emit(self) -> None:
        if self._on_state:
            self._on_state(self._state)

    def disconnect(self) -> None:
        self._running = False
        self._state.connected = False
        self._emit()
