"""uvicorn entry point."""
from __future__ import annotations

import uvicorn

from web_server.app import create_app

app = create_app(cors_origins=["http://localhost:3000", "http://hmi.roc.local"])

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8080)
