# Wii Remote debug web tool

Pair a Nintendo Wii Remote over Bluetooth and inspect accelerometer orientation, buttons, and raw HID reports in the browser.

## Stack

- **Backend:** FastAPI + WebSocket (`tools/wiimote_debug/backend`)
- **Frontend:** Vue 3 + Vite (`tools/wiimote_debug/frontend`)

## Quick start (Windows)

```powershell
cd tools/wiimote_debug

# Backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r backend/requirements.txt
cd backend
uvicorn app.main:app --reload --host 127.0.0.1 --port 8000

# Frontend (new terminal)
cd tools/wiimote_debug/frontend
npm install
npm run dev
```

Open http://localhost:5173

Use **Mock** to verify the UI without hardware.

## Connecting a real remote

### Linux (recommended — full raw reports)

1. Pair the remote: hold **1+2**, add device in Bluetooth settings (PIN empty if prompted).
2. Click **Scan** — you should see `Nintendo RVL-CNT-01` or `-TR`.
3. Click **Connect**.

Uses direct **L2CAP** (control port 17, data port 19) and report mode `0x31` (accelerometer).

Requires: BlueZ, `python3-dev`, and PyBluez (listed in `backend/requirements.txt`).

### Windows

Windows does not expose Wii Remote **L2CAP** to user-space Python. Options:

1. **[HID Wiimote driver](https://www.julianloehr.de/educational-work/hid-wiimote/)** — pair the remote, then **Scan** for a Nintendo HID device and connect.
2. **Mayflash DolphinBar** — remote appears as USB HID; scan and connect via HID path.
3. **Mock mode** — UI development only.

## API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Connection state + platform capabilities |
| GET | `/api/devices` | Bluetooth / HID scan |
| POST | `/api/connect` | Body: `{ "address": "...", "mock": false }` |
| POST | `/api/disconnect` | Disconnect |
| WS | `/ws/stream` | Live JSON state (~50 Hz) |

## Production build

```powershell
cd frontend
npm run build
cd ../backend
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

The API serves `frontend/dist` when present.

## Orientation notes

Pitch and roll are estimated from the built-in accelerometer (no gyro yaw). Values match the Wiibrew 10-bit accelerometer encoding used by the Wii Remote firmware pipeline in `firmware/imu_demo`.
