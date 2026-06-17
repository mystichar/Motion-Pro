"""FastAPI server for Wii Remote debug streaming."""

from __future__ import annotations

import asyncio
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .wiimote.manager import manager


@asynccontextmanager
async def lifespan(_app: FastAPI):
    manager.bind_loop(asyncio.get_running_loop())
    yield


app = FastAPI(title="Wii Remote Debug", version="0.1.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class ConnectRequest(BaseModel):
    address: str | None = None
    mock: bool = Field(default=False, description="Use simulated remote (no Bluetooth)")


@app.get("/api/status")
async def get_status() -> dict:
    return manager.status()


@app.get("/api/devices")
async def list_devices() -> dict:
    return {"devices": manager.discover()}


@app.post("/api/connect")
async def connect_remote(body: ConnectRequest) -> dict:
    try:
        return manager.connect(address=body.address, use_mock=body.mock)
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.post("/api/disconnect")
async def disconnect_remote() -> dict:
    return manager.disconnect()


@app.websocket("/ws/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    queue = await manager.subscribe()
    try:
        while True:
            payload = await queue.get()
            await ws.send_json({"type": "state", **payload})
    except WebSocketDisconnect:
        pass
    finally:
        manager.unsubscribe(queue)


dist = Path(__file__).resolve().parents[2] / "frontend" / "dist"
if dist.is_dir():
    app.mount("/", StaticFiles(directory=str(dist), html=True), name="frontend")
