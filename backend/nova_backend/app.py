"""FastAPI application.

Endpoints
  GET  /v1/health                 liveness + configuration sanity (no secrets)
  POST /v1/engineer/messages      Messages API request from the plug-in's cloud engineer
  GET  /v1/prefs, PUT /v1/prefs   optional, opt-in sync of the learned taste profile

Privacy: the plug-in sends analysis numbers and the user's text, never audio. Message content is
never logged; only token ids, model, sizes, status and usage are.
"""

from __future__ import annotations

import json
import logging
import os
import time
from collections import defaultdict, deque
from typing import Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import JSONResponse, Response

from . import __version__
from .config import Settings, token_id
from .engine import AnthropicEngine, Engine, UpstreamError

log = logging.getLogger("nova_backend")

# Request fields the plug-in may send; anything else is rejected rather than forwarded.
ALLOWED_FIELDS = {"model", "max_tokens", "system", "messages", "tools", "output_config", "tool_choice", "stop_sequences", "metadata"}


def api_error(status: int, error_type: str, message: str) -> JSONResponse:
    # Same shape as the Messages API so the plug-in's error handling is identical.
    return JSONResponse(status_code=status, content={"type": "error", "error": {"type": error_type, "message": message}})


class RateLimiter:
    def __init__(self, per_minute: int):
        self.per_minute = per_minute
        self.hits: dict[str, deque[float]] = defaultdict(deque)

    def allow(self, key: str) -> bool:
        now = time.monotonic()
        q = self.hits[key]
        while q and now - q[0] > 60.0:
            q.popleft()
        if len(q) >= self.per_minute:
            return False
        q.append(now)
        return True


def validate_request(body: Any, settings: Settings) -> dict[str, Any]:
    if not isinstance(body, dict):
        raise HTTPException(400, "request body must be a JSON object")
    unknown = set(body) - ALLOWED_FIELDS
    if unknown:
        raise HTTPException(400, f"unsupported fields: {', '.join(sorted(unknown))}")
    model = body.get("model")
    if model not in settings.allowed_models:
        raise HTTPException(400, f"model not allowed on this backend: {model}")
    messages = body.get("messages")
    if not isinstance(messages, list) or not messages:
        raise HTTPException(400, "messages must be a non-empty array")
    max_tokens = body.get("max_tokens")
    if not isinstance(max_tokens, int) or max_tokens <= 0:
        raise HTTPException(400, "max_tokens must be a positive integer")
    params = dict(body)
    params["max_tokens"] = min(max_tokens, settings.max_tokens_cap)
    return params


def create_app(settings: Settings | None = None, engine: Engine | None = None) -> FastAPI:
    settings = settings or Settings()
    app = FastAPI(title="NOVA Cloud", version=__version__)
    app.state.settings = settings
    app.state.engine = engine
    app.state.limiter = RateLimiter(settings.requests_per_minute)

    def get_engine() -> Engine:
        if app.state.engine is None:
            if not settings.anthropic_api_key:
                raise HTTPException(503, "backend has no model provider key configured")
            app.state.engine = AnthropicEngine(settings.anthropic_api_key)
        return app.state.engine

    def client_token(authorization: str | None = Header(default=None)) -> str:
        if not authorization or not authorization.lower().startswith("bearer "):
            raise HTTPException(401, "missing bearer token")
        token = authorization[7:].strip()
        if not settings.token_allowed(token):
            raise HTTPException(401, "invalid token")
        return token

    @app.exception_handler(HTTPException)
    async def http_error(_: Request, exc: HTTPException):
        kind = {400: "invalid_request_error", 401: "authentication_error", 413: "request_too_large",
                429: "rate_limit_error", 503: "overloaded_error"}.get(exc.status_code, "api_error")
        return api_error(exc.status_code, kind, str(exc.detail))

    @app.get("/v1/health")
    async def health():
        return {"ok": True, "version": __version__, "provider_configured": bool(settings.anthropic_api_key or app.state.engine),
                "models": settings.allowed_models}

    @app.post("/v1/engineer/messages")
    async def engineer_messages(request: Request, token: str = Depends(client_token),
                                x_nova_fallbacks: str | None = Header(default=None),
                                x_nova_client: str | None = Header(default=None)):
        raw = await request.body()
        if len(raw) > settings.max_request_bytes:
            raise HTTPException(413, "request too large")
        tid = token_id(token)
        if not app.state.limiter.allow(tid):
            raise HTTPException(429, "too many requests - try again in a minute")
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            raise HTTPException(400, "body is not valid JSON")
        params = validate_request(body, settings)
        engine = get_engine()
        started = time.monotonic()
        try:
            result = await engine.create(params, use_fallbacks=(x_nova_fallbacks == "default"))
        except UpstreamError as e:
            log.warning("upstream error token=%s model=%s status=%s type=%s", tid, params["model"], e.status, e.error_type)
            return api_error(e.status, e.error_type, e.message)
        log.info("ok token=%s client=%s model=%s bytes_in=%d ms=%d", tid, x_nova_client or "?", params["model"], len(raw),
                 int((time.monotonic() - started) * 1000))
        return Response(content=result, media_type="application/json")

    # ---- optional taste-profile sync (the plug-in keeps working without it)
    def prefs_path(token: str) -> str:
        os.makedirs(settings.data_dir, exist_ok=True)
        return os.path.join(settings.data_dir, f"prefs-{token_id(token)}.json")

    @app.get("/v1/prefs")
    async def get_prefs(token: str = Depends(client_token)):
        path = prefs_path(token)
        if not os.path.exists(path):
            return {"prefs": None}
        with open(path, encoding="utf-8") as f:
            return {"prefs": json.load(f)}

    @app.put("/v1/prefs")
    async def put_prefs(request: Request, token: str = Depends(client_token)):
        raw = await request.body()
        if len(raw) > 256 * 1024:
            raise HTTPException(413, "preferences too large")
        try:
            prefs = json.loads(raw)
        except json.JSONDecodeError:
            raise HTTPException(400, "body is not valid JSON")
        if not isinstance(prefs, dict):
            raise HTTPException(400, "preferences must be a JSON object")
        with open(prefs_path(token), "w", encoding="utf-8") as f:
            json.dump(prefs, f)
        return {"ok": True}

    @app.delete("/v1/prefs")
    async def delete_prefs(token: str = Depends(client_token)):
        path = prefs_path(token)
        if os.path.exists(path):
            os.remove(path)
        return {"ok": True}

    return app


app = create_app() if os.environ.get("NOVA_BACKEND_AUTOCREATE", "1") == "1" else None
