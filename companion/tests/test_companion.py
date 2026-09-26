import base64

import numpy as np
from fastapi.testclient import TestClient

from nova_companion.app import Services, create_app
from nova_companion.audio_input import SAMPLE_RATE, NullRecorder
from nova_companion.embed import DspEmbedder
from nova_companion.stt import NullSTT, Transcript
from nova_companion.tts import NullTTS, mac_arabic_voice


class FakeRecorder:
    available = True
    name = "fake-mic"

    def __init__(self, seconds=1.5):
        self.seconds = seconds
        self.started = 0

    def start(self):
        self.started += 1

    def stop(self):
        t = np.arange(int(self.seconds * SAMPLE_RATE)) / SAMPLE_RATE
        return (0.1 * np.sin(2 * np.pi * 220 * t)).astype(np.float32)


class FakeSTT:
    available = True
    name = "fake-stt"

    def __init__(self):
        self.calls = []

    def transcribe(self, audio, sample_rate, language_hint="auto"):
        self.calls.append((audio.size, sample_rate, language_hint))
        return Transcript(text="الصوت حاد شوية", language="ar", confidence=0.93)


class FakeTTS:
    available = True
    name = "fake-tts"

    def __init__(self):
        self.said = []

    def speak(self, text, language="en"):
        self.said.append((text, language))


def client(recorder=None, stt=None, tts=None):
    s = Services(recorder=recorder or FakeRecorder(), stt=stt or FakeSTT(), tts=tts or FakeTTS(), embedder=DspEmbedder())
    return TestClient(create_app(s)), s


def test_health_reports_real_capabilities():
    c, _ = client()
    caps = c.get("/v1/health").json()["capabilities"]
    assert caps["stt"] and caps["tts"] and caps["embeddings"]
    assert caps["stt_engine"] == "fake-stt"
    assert "non-neural" in caps["embedding_model"]      # honest about the embedding backend
    c2, _ = client(recorder=NullRecorder(), stt=NullSTT(), tts=NullTTS())
    caps2 = c2.get("/v1/health").json()["capabilities"]
    assert caps2["stt"] is False and caps2["tts"] is False and caps2["stt_engine"] == ""


def test_push_to_talk_round_trip_arabic():
    c, s = client()
    assert c.post("/v1/stt/start", json={"language": "auto"}).json() == {"listening": True}
    r = c.post("/v1/stt/stop", json={}).json()
    assert r["text"] == "الصوت حاد شوية"
    assert r["language"] == "ar"
    assert s.stt.calls[0][1] == SAMPLE_RATE and s.stt.calls[0][2] == "auto"


def test_stop_without_start_and_too_short():
    c, _ = client(recorder=FakeRecorder(seconds=0.1))
    assert c.post("/v1/stt/stop", json={}).json()["error"] == "not listening"
    c.post("/v1/stt/start", json={})
    assert "too short" in c.post("/v1/stt/stop", json={}).json()["error"]


def test_stt_unavailable_is_503():
    c, _ = client(recorder=NullRecorder(), stt=NullSTT())
    assert c.post("/v1/stt/start", json={}).status_code == 503


def test_tts():
    c, s = client()
    assert c.post("/v1/tts", json={"text": "تمام، خففت الحدة", "language": "ar"}).json() == {"ok": True}
    assert s.tts.said == [("تمام، خففت الحدة", "ar")]
    c2, _ = client(tts=NullTTS())
    assert c2.post("/v1/tts", json={"text": "x"}).json()["ok"] is False


def test_browser_origins_are_rejected():
    c, _ = client()
    assert c.get("/v1/health", headers={"origin": "https://evil.example"}).status_code == 403


def _pcm(x):
    return base64.b64encode(np.asarray(x, dtype="<f4").tobytes()).decode()


def test_embedding_is_deterministic_normalised_and_discriminative():
    c, _ = client()
    sr = 48000
    t = np.arange(sr * 2) / sr
    dark = 0.3 * np.sin(2 * np.pi * 150 * t) + 0.05 * np.sin(2 * np.pi * 300 * t)
    rng = np.random.default_rng(1)
    bright = 0.3 * np.sin(2 * np.pi * 150 * t) + 0.2 * rng.standard_normal(t.size) * (t % 0.5 < 0.25)
    e1 = np.array(c.post("/v1/embed", json={"sample_rate": sr, "pcm_f32_b64": _pcm(dark)}).json()["embedding"])
    e1b = np.array(c.post("/v1/embed", json={"sample_rate": sr, "pcm_f32_b64": _pcm(dark * 0.25)}).json()["embedding"])
    e2 = np.array(c.post("/v1/embed", json={"sample_rate": sr, "pcm_f32_b64": _pcm(bright)}).json()["embedding"])
    assert abs(np.linalg.norm(e1) - 1.0) < 1e-3
    assert float(e1 @ e1b) > 0.98            # level independent
    assert float(e1 @ e2) < float(e1 @ e1b)  # different sounds are further apart


def test_embed_rejects_bad_input():
    c, _ = client()
    assert c.post("/v1/embed", json={"sample_rate": 48000, "pcm_f32_b64": "%%%"}).status_code == 400
    assert c.post("/v1/embed", json={"sample_rate": 48000, "pcm_f32_b64": _pcm(np.zeros(100))}).status_code == 400


def test_mac_arabic_voice_detection():
    listing = "Alex                en_US    # Most people recognize me by my voice.\nMajed               ar_001   # مرحبًا! اسمي ماجد.\n"
    assert mac_arabic_voice(listing) == "Majed"
    assert mac_arabic_voice("Alex en_US # hi\n") is None


def test_whisper_language_detection_is_limited_to_arabic_and_english():
    """A short command that Whisper would call German / Persian is decided between ar and en only."""
    from nova_companion.stt import FasterWhisperSTT

    class Seg:
        text = " نص"

    class Info:
        language = "de"
        language_probability = 0.4

    class FakeModel:
        def __init__(self):
            self.forced = None

        def detect_language(self, audio):
            return "de", 0.4, [("de", 0.4), ("fa", 0.3), ("ar", 0.2), ("en", 0.05)]

        def transcribe(self, audio, language=None, **kw):
            self.forced = language
            return [Seg()], Info()

    stt = object.__new__(FasterWhisperSTT)
    stt.name, stt.available, stt.languages = "fake", True, ("ar", "en")
    stt._model = FakeModel()
    t = stt.transcribe(np.zeros(16000, dtype=np.float32), 16000, "auto")
    assert t.language == "ar" and stt._model.forced == "ar"
    assert abs(t.confidence - 0.2 / 0.25) < 1e-6
    t2 = stt.transcribe(np.zeros(16000, dtype=np.float32), 16000, "en")   # explicit hint wins
    assert t2.language == "en" and stt._model.forced == "en"
