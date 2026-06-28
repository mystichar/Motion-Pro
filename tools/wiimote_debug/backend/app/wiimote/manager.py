"""Connection lifecycle shared by HTTP and WebSocket endpoints."""

from __future__ import annotations

import asyncio
import sys
import threading
from typing import Any, Callable

from . import bt_scan, hid_backend, l2cap, mock
from .protocol import WiimoteState

Connection = l2cap.L2capWiimote | hid_backend.HidWiimote | mock.MockWiimote


class WiimoteManager:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._connection: Connection | None = None
        self._state = WiimoteState()
        self._subscribers: list[asyncio.Queue[dict[str, Any]]] = []
        self._loop: asyncio.AbstractEventLoop | None = None

    def bind_loop(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop

    @property
    def state(self) -> WiimoteState:
        return self._state

    def status(self) -> dict[str, Any]:
        with self._lock:
            platform = {
                "os": sys.platform,
                "l2cap": l2cap._platform_supports_l2cap(),
                "hid": hid_backend.hid_available(),
            }
            return {"state": self._state.to_dict(), "platform": platform}

    def discover(self, timeout_s: float | None = None) -> list[dict[str, str]]:
        scan_seconds = timeout_s if timeout_s is not None else bt_scan.DEFAULT_SCAN_TIMEOUT_S
        devices: list[dict[str, str]] = []
        nintendo = bt_scan.discover_nintendo_bluetooth(timeout_s=scan_seconds)
        bt_scan.print_nintendo_devices(nintendo, timeout_s=scan_seconds)
        devices.extend(nintendo)
        if l2cap._platform_supports_l2cap():
            for entry in l2cap.discover_devices():
                if entry.get("name", "").startswith(bt_scan.NINTENDO_PREFIX):
                    devices.append({**entry, "via": entry.get("via", "l2cap")})
        if hid_backend.hid_available():
            for entry in hid_backend.discover_devices():
                name = entry.get("name", "")
                if not name.startswith(bt_scan.NINTENDO_PREFIX):
                    continue
                devices.append({"address": entry["address"], "name": name, "via": "hid"})
        return bt_scan.dedupe_devices(devices)

    def connect(self, address: str | None = None, use_mock: bool = False) -> dict[str, Any]:
        with self._lock:
            self.disconnect_locked()
            if use_mock:
                conn: Connection = mock.MockWiimote()
            elif address and sys.platform == "win32" and hid_backend.hid_available():
                conn = hid_backend.HidWiimote(address)
            elif address and l2cap._platform_supports_l2cap():
                conn = l2cap.L2capWiimote(address)
            elif address and sys.platform == "win32":
                raise RuntimeError(
                    "Windows cannot use L2CAP directly. Install the HID Wiimote driver, "
                    "connect the remote, then connect via HID path — or use Mock mode."
                )
            else:
                devices = self.discover()
                if not devices:
                    raise RuntimeError(
                        "No Wii Remote found. Pair with 1+2 held, or pass address / use mock mode."
                    )
                first = devices[0]
                if first.get("via") == "hid":
                    conn = hid_backend.HidWiimote(first["address"], first.get("name", ""))
                else:
                    conn = l2cap.L2capWiimote(first["address"], first.get("name", ""))

            conn.set_callback(self._on_state)
            try:
                conn.connect()
            except Exception as exc:
                self._state = WiimoteState(error=str(exc))
                raise
            self._connection = conn
            return self._state.to_dict()

    def disconnect(self) -> dict[str, Any]:
        with self._lock:
            self.disconnect_locked()
            return self._state.to_dict()

    def disconnect_locked(self) -> None:
        if self._connection is not None:
            self._connection.disconnect()
            self._connection = None
        self._state = WiimoteState()

    def _on_state(self, state: WiimoteState) -> None:
        with self._lock:
            self._state = state
            payload = state.to_dict()
        if self._loop is not None and self._loop.is_running():
            for queue in list(self._subscribers):
                self._loop.call_soon_threadsafe(queue.put_nowait, payload)

    async def subscribe(self) -> asyncio.Queue[dict[str, Any]]:
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=32)
        self._subscribers.append(queue)
        await queue.put(self._state.to_dict())
        return queue

    def unsubscribe(self, queue: asyncio.Queue[dict[str, Any]]) -> None:
        if queue in self._subscribers:
            self._subscribers.remove(queue)


manager = WiimoteManager()
