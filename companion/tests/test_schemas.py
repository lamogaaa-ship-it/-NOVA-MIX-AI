"""The companion's responses conform to the shared JSON schemas the plug-in is written against."""

import base64
import json
import pathlib

import jsonschema
import numpy as np
import pytest

from test_companion import client

SCHEMAS = pathlib.Path(__file__).resolve().parents[2] / "shared" / "schemas"


def schema(name, ref=None):
    s = json.loads((SCHEMAS / name).read_text())
    return {**s["$defs"][ref], "$defs": s["$defs"]} if ref else s


def test_health_schema():
    c, _ = client()
    jsonschema.validate(c.get("/v1/health").json(), schema("companion-health.schema.json"))


def test_stt_schema():
    c, _ = client()
    jsonschema.validate(c.post("/v1/stt/start", json={"language": "ar"}).json(), schema("companion-stt.schema.json", "startResponse"))
    jsonschema.validate(c.post("/v1/stt/stop", json={}).json(), schema("companion-stt.schema.json", "stopResponse"))


def test_embed_schema():
    c, _ = client()
    sr = 48000
    pcm = (0.2 * np.sin(2 * np.pi * 440 * np.arange(sr) / sr)).astype("<f4")
    req = {"sample_rate": sr, "pcm_f32_b64": base64.b64encode(pcm.tobytes()).decode()}
    jsonschema.validate(req, schema("companion-embed.schema.json", "request"))
    jsonschema.validate(c.post("/v1/embed", json=req).json(), schema("companion-embed.schema.json", "response"))


def test_schemas_are_valid():
    for p in SCHEMAS.glob("*.schema.json"):
        jsonschema.Draft202012Validator.check_schema(json.loads(p.read_text()))
