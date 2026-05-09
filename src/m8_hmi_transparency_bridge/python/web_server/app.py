"""FastAPI main app — M8 web backend."""
from __future__ import annotations

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

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
    return app
