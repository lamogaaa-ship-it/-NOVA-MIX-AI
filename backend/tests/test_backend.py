import hashlib
import json

import pytest
from fastapi.testclient import TestClient

from nova_backend.app import create_app
from nova_backend.config import Settings
from nova_backend.engine import UpstreamError

TOKEN = "nova-test-token"


class FakeEngine:
    def __init__(self, reply=None, error=None):
        self.calls = []
        self.reply = reply or {"id": "msg_1", "type": "message", "role": "assistant", "model": "claude-fable-5-1",
                               "stop_reason": "end_turn", "content": [{"type": "text", "text": "<simple>ok</simple>"}],
                               "usage": {"input_tokens": 10, "output_tokens": 3}}
        self.error = error

    async def create(self, params, use_fallbacks):
        self.calls.append((params, use_fallbacks))
        if self.error:
            raise self.error
        return json.dumps(self.reply)


def make(tmp_path, engine=None, **kw):
    s = Settings(anthropic_api_key=None, client_token_hashes={hashlib.sha256(TOKEN.encode()).hexdigest()},
                 data_dir=str(tmp_path), **kw)
    engine = engine or FakeEngine()
    return TestClient(create_app(s, engine)), engine


def body(**over):
    b = {"model": "claude-fable-5-1", "max_tokens": 8000, "system": [{"type": "text", "text": "sys", "cache_control": {"type": "ephemeral"}}],
         "messages": [{"role": "user", "content": "hi"}], "tools": [], "output_config": {"effort": "medium"}}
    b.update(over)
    return b


def auth(token=TOKEN):
    return {"authorization": f"Bearer {token}"}


def test_health_reveals_no_secrets(tmp_path):
    c, _ = make(tmp_path)
    r = c.get("/v1/health")
    assert r.status_code == 200
    assert "key" not in json.dumps(r.json()).lower().replace("provider_configured", "")


def test_rejects_missing_and_wrong_tokens(tmp_path):
    c, eng = make(tmp_path)
    assert c.post("/v1/engineer/messages", json=body()).status_code == 401
    r = c.post("/v1/engineer/messages", json=body(), headers=auth("wrong"))
    assert r.status_code == 401
    assert r.json()["type"] == "error" and r.json()["error"]["type"] == "authentication_error"
    assert eng.calls == []


def test_forwards_valid_request_with_fallbacks(tmp_path):
    c, eng = make(tmp_path)
    r = c.post("/v1/engineer/messages", json=body(), headers={**auth(), "x-nova-fallbacks": "default", "x-nova-client": "nova-mix-ai-plugin/0.3.0"})
    assert r.status_code == 200
    assert r.json()["content"][0]["text"] == "<simple>ok</simple>"
    params, fallbacks = eng.calls[0]
    assert fallbacks is True
    assert params["output_config"] == {"effort": "medium"}
    assert params["system"][0]["cache_control"] == {"type": "ephemeral"}


def test_validation_rejects_unknown_fields_models_and_caps_tokens(tmp_path):
    c, eng = make(tmp_path)
    assert c.post("/v1/engineer/messages", json=body(model="some-other-model"), headers=auth()).status_code == 400
    assert c.post("/v1/engineer/messages", json=body(betas=["x"]), headers=auth()).status_code == 400
    assert c.post("/v1/engineer/messages", json=body(messages=[]), headers=auth()).status_code == 400
    assert c.post("/v1/engineer/messages", content=b"{not json", headers=auth()).status_code == 400
    assert c.post("/v1/engineer/messages", json=body(max_tokens=10**6), headers=auth()).status_code == 200
    assert eng.calls[-1][0]["max_tokens"] == 16000
    assert eng.calls[-1][1] is False   # no fallback header -> no fallbacks


def test_upstream_errors_keep_messages_api_shape(tmp_path):
    c, _ = make(tmp_path, engine=FakeEngine(error=UpstreamError(529, "overloaded_error", "Overloaded")))
    r = c.post("/v1/engineer/messages", json=body(), headers=auth())
    assert r.status_code == 529
    assert r.json() == {"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}}


def test_rate_limit_per_token(tmp_path):
    c, _ = make(tmp_path, requests_per_minute=2)
    codes = [c.post("/v1/engineer/messages", json=body(), headers=auth()).status_code for _ in range(3)]
    assert codes == [200, 200, 429]


def test_request_size_limit(tmp_path):
    c, _ = make(tmp_path, max_request_bytes=100)
    assert c.post("/v1/engineer/messages", json=body(), headers=auth()).status_code == 413


def test_no_provider_key_is_reported_not_crashed(tmp_path):
    s = Settings(anthropic_api_key=None, client_token_hashes={hashlib.sha256(TOKEN.encode()).hexdigest()}, data_dir=str(tmp_path))
    c = TestClient(create_app(s, None))
    r = c.post("/v1/engineer/messages", json=body(), headers=auth())
    assert r.status_code == 503


def test_prefs_sync_is_per_token_and_deletable(tmp_path):
    c, _ = make(tmp_path)
    assert c.get("/v1/prefs", headers=auth()).json() == {"prefs": None}
    assert c.put("/v1/prefs", json={"contexts": {"vocal": {"brightness": 0.2}}}, headers=auth()).status_code == 200
    assert c.get("/v1/prefs", headers=auth()).json()["prefs"]["contexts"]["vocal"]["brightness"] == 0.2
    assert c.put("/v1/prefs", json=[1, 2], headers=auth()).status_code == 400
    assert c.delete("/v1/prefs", headers=auth()).status_code == 200
    assert c.get("/v1/prefs", headers=auth()).json() == {"prefs": None}
    files = list(tmp_path.iterdir())
    assert all(TOKEN not in f.name for f in files)


def test_real_engine_uses_sdk_beta_stream(monkeypatch):
    """The Anthropic engine forwards params + the scalar fallbacks beta through the SDK stream helper."""
    from nova_backend import engine as eng_mod

    captured = {}

    class FakeMessage:
        def to_json(self, indent=None):
            return '{"type":"message"}'

    class FakeStream:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *a):
            return False

        async def get_final_message(self):
            return FakeMessage()

    e = eng_mod.AnthropicEngine("test-key")

    def fake_stream(**kwargs):
        captured.update(kwargs)
        return FakeStream()

    monkeypatch.setattr(e.client.beta.messages, "stream", fake_stream)
    import asyncio
    out = asyncio.run(e.create({"model": "claude-fable-5-1", "max_tokens": 10, "messages": []}, use_fallbacks=True))
    assert out == '{"type":"message"}'
    assert captured["fallbacks"] == "default"
    assert captured["betas"] == [eng_mod.FALLBACK_BETA]


def test_backend_fields_match_shared_schema():
    import pathlib

    import jsonschema

    from nova_backend.app import ALLOWED_FIELDS
    s = json.loads((pathlib.Path(__file__).resolve().parents[2] / "shared" / "schemas" / "engineer-messages-request.schema.json").read_text())
    assert set(s["properties"]) == ALLOWED_FIELDS
    jsonschema.validate(body(), s)
