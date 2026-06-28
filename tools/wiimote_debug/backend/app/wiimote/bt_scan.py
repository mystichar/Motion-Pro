"""Discover Bluetooth devices whose names start with 'Nintendo'."""

from __future__ import annotations

import asyncio
import json
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from typing import Any

NINTENDO_PREFIX = "Nintendo"
DEFAULT_SCAN_TIMEOUT_S = 20.0

_MAC_COLON_RE = re.compile(r"([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})")
_MAC_DEV12_RE = re.compile(r"DEV_([0-9A-Fa-f]{12})")

# Classic Bluetooth protocol (not BLE) — finds in-range paired and unpaired devices.
_WINRT_CLASSIC_BT_AQS = 'System.Devices.Aep.ProtocolId:="{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}"'
_WINRT_REQUESTED_PROPERTIES = [
    "System.Devices.Aep.DeviceAddress",
    "System.Devices.Aep.IsPaired",
    "System.ItemNameDisplay",
]


def _format_mac_dev12(raw: str) -> str:
    pairs = [raw[i : i + 2] for i in range(0, 12, 2)]
    return ":".join(p.upper() for p in pairs)


def _mac_from_uint64(value: int) -> str:
    hex_digits = f"{value:012X}"
    return ":".join(hex_digits[i : i + 2] for i in range(0, 12, 2))


def _mac_from_instance_id(instance_id: str) -> str:
    if not instance_id:
        return ""
    match = _MAC_COLON_RE.search(instance_id)
    if match:
        return match.group(1).upper()
    match = _MAC_DEV12_RE.search(instance_id)
    if match:
        return _format_mac_dev12(match.group(1))
    return ""


def _read_winrt_prop(props: Any, key: str) -> Any:
    if not props:
        return None
    try:
        return props[key]
    except Exception:
        return None


def _format_bt_address(raw: Any) -> str:
    if raw is None:
        return ""
    if isinstance(raw, str):
        text = raw.strip().upper()
        if _MAC_COLON_RE.fullmatch(text):
            return text
        if len(text) == 12 and re.fullmatch(r"[0-9A-Fa-f]{12}", text):
            return _format_mac_dev12(text)
        if text.startswith("0X"):
            return _mac_from_uint64(int(text, 16))
        return text
    if isinstance(raw, int):
        return _mac_from_uint64(raw)
    try:
        return _mac_from_uint64(int(raw))
    except (TypeError, ValueError):
        return str(raw).strip().upper()


def _normalize(entry: dict[str, Any]) -> dict[str, str] | None:
    name = (entry.get("name") or "").strip()
    if not name.startswith(NINTENDO_PREFIX):
        return None
    address = (entry.get("address") or "").strip().upper()
    if not address:
        return None
    via = entry.get("via") or "bluetooth"
    paired = entry.get("paired")
    if paired is False:
        via = "bluetooth-unpaired"
    elif paired is True:
        via = "bluetooth-paired"
    return {
        "name": name,
        "address": address,
        "via": via,
    }


def _dedupe(devices: list[dict[str, str]]) -> list[dict[str, str]]:
    seen: set[tuple[str, str]] = set()
    out: list[dict[str, str]] = []
    for device in devices:
        key = (device["address"], device["name"])
        if key in seen:
            continue
        seen.add(key)
        out.append(device)
    return out


def dedupe_devices(devices: list[dict[str, str]]) -> list[dict[str, str]]:
    return _dedupe(devices)


def discover_windows_pnp() -> list[dict[str, str]]:
    """Instant snapshot of paired Nintendo devices from PnP."""
    ps = r"""
$script:items = @()
function Add-NintendoDevice($name, $id, $via) {
    if (-not $name -or -not $name.StartsWith('Nintendo')) { return }
    $mac = ''
    if ($id -match 'DEV_([0-9A-Fa-f]{12})') {
        $raw = $Matches[1]
        $mac = ($raw -replace '(.{2})', '$1:').TrimEnd(':').ToUpper()
    } elseif ($id -match '(([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})') {
        $mac = $Matches[1].ToUpper()
    }
    $script:items += [PSCustomObject]@{
        name = $name
        address = $mac
        via = 'bluetooth-paired'
        instance_id = $id
        paired = $true
    }
}
foreach ($dev in (Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue)) {
    Add-NintendoDevice $dev.FriendlyName $dev.InstanceId 'bluetooth-paired'
}
foreach ($dev in (Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue)) {
    Add-NintendoDevice $dev.Name $dev.PNPDeviceID 'bluetooth-paired'
}
$script:items | ConvertTo-Json -Compress
"""
    try:
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-Command", ps],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return []

    if proc.returncode != 0 or not proc.stdout.strip():
        return []

    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return []

    if isinstance(payload, dict):
        payload = [payload]

    found: list[dict[str, str]] = []
    for item in payload:
        if not isinstance(item, dict):
            continue
        name = str(item.get("name") or "")
        address = str(item.get("address") or "")
        if not address:
            address = _mac_from_instance_id(str(item.get("instance_id") or ""))
        normalized = _normalize(
            {
                "name": name,
                "address": address,
                "via": item.get("via"),
                "paired": item.get("paired", True),
            }
        )
        if normalized:
            found.append(normalized)
    return found


def _winrt_available() -> bool:
    if sys.platform != "win32":
        return False
    try:
        import winsdk.windows.devices.enumeration  # noqa: F401

        return True
    except ImportError:
        return False


async def _discover_windows_active_async(timeout_s: float) -> list[dict[str, str]]:
    from winsdk.windows.devices.enumeration import DeviceInformation, DeviceInformationKind

    pending: dict[str, dict[str, Any]] = {}
    found: dict[str, dict[str, str]] = {}

    def record(device_id: str, name: str, address: str, paired: Any) -> None:
        if not name.startswith(NINTENDO_PREFIX):
            return
        if not address:
            address = _mac_from_instance_id(device_id)
        if not address:
            return
        normalized = _normalize(
            {"name": name, "address": address, "paired": paired, "via": "bluetooth"}
        )
        if normalized:
            found[normalized["address"]] = normalized

    def on_added(_sender: Any, info: Any) -> None:
        props = info.properties
        entry = {
            "name": (
                info.name or _read_winrt_prop(props, "System.ItemNameDisplay") or ""
            ).strip(),
            "address": _format_bt_address(
                _read_winrt_prop(props, "System.Devices.Aep.DeviceAddress")
            ),
            "paired": _read_winrt_prop(props, "System.Devices.Aep.IsPaired"),
        }
        pending[info.id] = entry
        record(info.id, entry["name"], entry["address"], entry["paired"])

    def on_updated(_sender: Any, update: Any) -> None:
        entry = pending.setdefault(update.id, {"name": "", "address": "", "paired": None})
        props = update.properties
        name = _read_winrt_prop(props, "System.ItemNameDisplay")
        if name:
            entry["name"] = str(name).strip()
        address = _read_winrt_prop(props, "System.Devices.Aep.DeviceAddress")
        if address is not None:
            entry["address"] = _format_bt_address(address)
        paired = _read_winrt_prop(props, "System.Devices.Aep.IsPaired")
        if paired is not None:
            entry["paired"] = paired
        record(update.id, entry["name"], entry["address"], entry["paired"])

    watcher = DeviceInformation.create_watcher(
        _WINRT_CLASSIC_BT_AQS,
        _WINRT_REQUESTED_PROPERTIES,
        DeviceInformationKind.ASSOCIATION_ENDPOINT,
    )
    watcher.add_added(on_added)
    watcher.add_updated(on_updated)
    watcher.start()
    try:
        print(
            f"Scanning Bluetooth for {timeout_s:.0f}s (hold 1+2 on Wii Remote)...",
            flush=True,
        )
        await asyncio.sleep(timeout_s)
    finally:
        watcher.stop()

    return list(found.values())


def _run_async_blocking(coro: Any, *, wait_timeout_s: float) -> Any:
    """Run a coroutine even when an event loop is already active (e.g. FastAPI)."""
    try:
        asyncio.get_running_loop()
    except RuntimeError:
        return asyncio.run(coro)

    with ThreadPoolExecutor(max_workers=1) as pool:
        future = pool.submit(asyncio.run, coro)
        return future.result(timeout=wait_timeout_s + 15.0)


def discover_windows_active(timeout_s: float) -> list[dict[str, str]]:
    if not _winrt_available():
        print(
            "Active Bluetooth scan unavailable (install winsdk). "
            "Only paired devices will be listed.",
            flush=True,
        )
        return []
    try:
        return _run_async_blocking(
            _discover_windows_active_async(timeout_s),
            wait_timeout_s=timeout_s,
        )
    except Exception as exc:
        print(f"Active Bluetooth scan failed: {exc}", flush=True)
        return []


def discover_windows(timeout_s: float = DEFAULT_SCAN_TIMEOUT_S) -> list[dict[str, str]]:
    """Active inquiry for in-range devices, plus paired PnP snapshot."""
    devices = discover_windows_active(timeout_s)
    devices.extend(discover_windows_pnp())
    return _dedupe(devices)


def discover_linux(timeout_s: float = DEFAULT_SCAN_TIMEOUT_S) -> list[dict[str, str]]:
    """Inquiry scan via PyBluez; returns in-range devices named Nintendo*."""
    try:
        import bluetooth  # type: ignore
    except ImportError:
        return []

    deadline = time.time() + timeout_s
    found: dict[str, dict[str, str]] = {}
    print(
        f"Scanning Bluetooth for {timeout_s:.0f}s (hold 1+2 on Wii Remote)...",
        flush=True,
    )

    while time.time() < deadline:
        remaining = max(1.0, deadline - time.time())
        chunk = min(8.0, remaining)
        try:
            nearby = bluetooth.discover_devices(
                duration=int(chunk),
                lookup_names=True,
                flush_cache=True,
                lookup_class=False,
            )
        except OSError:
            break

        for addr in nearby:
            mac = addr.upper()
            name = ""
            try:
                name = bluetooth.lookup_name(mac, timeout=5) or ""
            except OSError:
                pass
            if not name.startswith(NINTENDO_PREFIX):
                continue
            normalized = _normalize(
                {"name": name, "address": mac, "via": "bluetooth", "paired": None}
            )
            if normalized:
                found[mac] = normalized

        time.sleep(0.25)

    return list(found.values())


def discover_nintendo_bluetooth(
    timeout_s: float = DEFAULT_SCAN_TIMEOUT_S,
) -> list[dict[str, str]]:
    """Platform scan for Bluetooth devices with names starting with 'Nintendo'."""
    if sys.platform == "win32":
        devices = discover_windows(timeout_s=timeout_s)
    elif sys.platform == "linux":
        devices = discover_linux(timeout_s=timeout_s)
    else:
        devices = discover_linux(timeout_s=timeout_s)
        if not devices:
            devices = discover_windows(timeout_s=timeout_s)

    return _dedupe(devices)


def print_nintendo_devices(
    devices: list[dict[str, str]] | None = None,
    timeout_s: float = DEFAULT_SCAN_TIMEOUT_S,
) -> list[dict[str, str]]:
    """Scan (if needed) and print name + MAC to stdout."""
    if devices is None:
        devices = discover_nintendo_bluetooth(timeout_s=timeout_s)
    if not devices:
        print("No Nintendo Bluetooth devices found.", flush=True)
        return []
    for device in devices:
        print(f"{device['name']}\t{device['address']}\t{device.get('via', '')}", flush=True)
    return devices


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Scan for Nintendo Bluetooth devices.")
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_SCAN_TIMEOUT_S,
        help="Scan duration in seconds (default: 20)",
    )
    args = parser.parse_args()
    print_nintendo_devices(timeout_s=args.timeout)
