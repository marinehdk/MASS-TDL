"""FastAPI main app — M8 web backend."""
from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from web_server.ros_bridge import RosBridge
from web_server.sil_router import router as sil_router
from web_server.sil_ws import router as sil_ws_router
from web_server.tor_endpoint import router as tor_router
from web_server.websocket import router as ws_router

logger = logging.getLogger("m8_web_backend")


@asynccontextmanager
async def lifespan(app: FastAPI):  # type: ignore[type-arg]
    bridge = RosBridge()
    await bridge.start()
    app.state.ros_bridge = bridge
    logger.info("M8 web backend started; rclpy bridge online")
    try:
        yield
    finally:
        await bridge.stop()
        logger.info("M8 web backend shut down")


def create_app(cors_origins: list[str]) -> FastAPI:
    app = FastAPI(
        title="MASS L3 M8 HMI Backend",
        version="1.0.0",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=cors_origins,
        allow_credentials=True,
        allow_methods=["GET", "POST"],
        allow_headers=["*"],
    )
    app.include_router(tor_router, prefix="/api")
    app.include_router(ws_router)
    app.include_router(sil_router, prefix="/sil")
    app.include_router(sil_ws_router)
    # D1.3b.3: Serve React production build as static files
    # Path from web_server/app.py -> web/dist (4 levels up)
    _WEB_DIST = Path(__file__).resolve().parents[5] / "web" / "dist"
    if _WEB_DIST.exists():
        # tippecanoe/mb-util output gzip-compressed MVT .pbf; StaticFiles serves
        # them as text/plain with no Content-Encoding, so MapLibre receives raw
        # gzip bytes and fails silently. Register the tile route *before* the
        # catch-all mount so FastAPI matches it first.
        _TILES_DIR = (_WEB_DIST / "tiles").resolve()

        @app.get("/tiles/{file_path:path}")
        async def serve_tile(file_path: str):
            full = (_TILES_DIR / file_path).resolve()
            try:
                full.relative_to(_TILES_DIR)
            except ValueError as exc:
                raise HTTPException(status_code=403) from exc
            if not full.is_file():
                raise HTTPException(status_code=404)
            if full.suffix == ".pbf":
                return FileResponse(
                    full,
                    media_type="application/x-protobuf",
                    headers={"Content-Encoding": "gzip"},
                )
            return FileResponse(full)

        app.mount("/", StaticFiles(directory=str(_WEB_DIST), html=True), name="web")
    return app
