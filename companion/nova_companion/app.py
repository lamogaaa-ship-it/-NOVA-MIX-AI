"""HTTP API used by the plug-in (see plugin/src/net/CompanionClient.*). Local only."""

from __future__ import annotations

import base64
import threading
from dataclasses import dataclass

import numpy as np
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

from . import __version__
from .audio_input import SAMPLE_RATE, Recorder
from .embed import Embedder
from .stt import SpeechToText
from .tts import TextToSpeech

LOCAL_HOSTS = {"127.0.0.1", "::1", "localhost", "testclient"}


@dataclass
class Services:
    recorder: Recorder
    stt: SpeechToText
    tts: TextToSpeech
    embedder: Embedder


def create_app(services: Services) -> FastAPI:
    app = FastAPI(title="NOVA Companion", version=__version__)
    state = {"listening": False, "language": "auto"}
    lock = threading.Lock()

    @app.middleware("http")
    async def local_only(request: Request, call_next):
        # Loopback clients only, and never from a web page (browsers always send Origin).
        host = request.client.host if request.client else ""
        if host not in LOCAL_HOSTS or request.headers.get("origin"):
            return JSONResponse(status_code=403, content={"error": "local plug-in clients only"})
        return await call_next(request)

    @app.get("/v1/health")
    def health():
        voice = services.recorder.available and services.stt.available
        return {"ok": True, "version": __version__,
                "capabilities": {"stt": voice, "tts": services.tts.available, "embeddings": services.embedder.available,
                                 "separation": False,
                                 "stt_engine": services.stt.name if voice else "",
                                 "tts_engine": services.tts.name if services.tts.available else "",
                                 "embedding_model": services.embedder.name + ("" if getattr(services.embedder, "neural", False) else " (DSP, non-neural)"),
                                 "microphone": services.recorder.name}}

    @app.post("/v1/stt/start")
    async def stt_start(request: Request):
        body = await _json(request)
        if not (services.recorder.available and services.stt.available):
            raise HTTPException(503, "speech-to-text is not available on this computer")
        with lock:
            if state["listening"]:
                services.recorder.stop()
            services.recorder.start()
            state["listening"] = True
            state["language"] = str(body.get("language", "auto") or "auto")
        return {"listening": True}

    @app.post("/v1/stt/stop")
    def stt_stop():
        with lock:
            if not state["listening"]:
                return {"text": "", "language": "", "error": "not listening"}
            audio = services.recorder.stop()
            state["listening"] = False
            hint = state["language"]
        if audio.size < SAMPLE_RATE // 4:
            return {"text": "", "language": "", "error": "too short - hold the button while you speak"}
        t = services.stt.transcribe(audio, SAMPLE_RATE, hint)
        return {"text": t.text, "language": t.language, "confidence": t.confidence, "seconds": round(audio.size / SAMPLE_RATE, 2)}

    @app.post("/v1/tts")
    async def tts(request: Request):
        body = await _json(request)
        text = str(body.get("text", ""))
        if not services.tts.available:
            return {"ok": False, "error": "text-to-speech is not available"}
        services.tts.speak(text, str(body.get("language", "en") or "en"))
        return {"ok": True}

    @app.post("/v1/embed")
    async def embed(request: Request):
        body = await _json(request)
        try:
            sr = int(body["sample_rate"])
            pcm = np.frombuffer(base64.b64decode(body["pcm_f32_b64"], validate=True), dtype="<f4")
        except Exception:
            raise HTTPException(400, "expected sample_rate and base64 little-endian float32 mono pcm_f32_b64")
        if not 8000 <= sr <= 384000:
            raise HTTPException(400, "unsupported sample rate")
        if pcm.size > sr * 600:
            raise HTTPException(413, "excerpt too long (max 10 minutes)")
        try:
            vec = services.embedder.embed(pcm, sr)
        except ValueError as e:
            raise HTTPException(400, str(e))
        return {"embedding": [round(float(v), 6) for v in vec], "model": services.embedder.name,
                "neural": bool(getattr(services.embedder, "neural", False))}

    return app


async def _json(request: Request) -> dict:
    try:
        body = await request.json()
    except Exception:
        body = {}
    return body if isinstance(body, dict) else {}
