"""Real speech recognition end to end (runs in CI where models can be downloaded).

Speech is synthesised with espeak-ng, resampled to 16 kHz and sent through the companion's real
faster-whisper backend. Enabled with NOVA_REAL_STT=1.
"""

import os
import shutil
import subprocess
import wave

import numpy as np
import pytest

pytestmark = pytest.mark.skipif(os.environ.get("NOVA_REAL_STT") != "1" or not shutil.which("espeak-ng"),
                                reason="set NOVA_REAL_STT=1 with espeak-ng + faster-whisper installed")


def synth(text, voice, tmp_path):
    path = tmp_path / f"{voice}.wav"
    subprocess.run(["espeak-ng", "-v", voice, "-s", "140", "-w", str(path), text], check=True)
    with wave.open(str(path)) as w:
        sr = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    t_out = np.arange(int(len(x) * 16000 / sr)) / 16000
    return np.interp(t_out, np.arange(len(x)) / sr, x).astype(np.float32)


@pytest.fixture(scope="module")
def stt():
    from nova_companion.stt import FasterWhisperSTT
    return FasterWhisperSTT(model_size=os.environ.get("NOVA_STT_MODEL", "base"))


def test_english_request(stt, tmp_path):
    audio = synth("Make the vocal brighter and remove the harshness.", "en-us", tmp_path)
    t = stt.transcribe(audio, 16000, "auto")
    print("EN:", t)
    assert t.language == "en"
    words = t.text.lower()
    assert "vocal" in words and ("bright" in words or "harsh" in words)


def test_arabic_request_detected_as_arabic(stt, tmp_path):
    audio = synth("الصوت حاد جدا، خليه أدفى شوية", "ar", tmp_path)
    t = stt.transcribe(audio, 16000, "auto")
    print("AR:", t)
    assert t.language == "ar"
    assert any("؀" <= ch <= "ۿ" for ch in t.text)
