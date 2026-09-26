"""Speech-to-text backends (English + Arabic, including Egyptian Arabic).

Provider-independent: the service only uses the SpeechToText interface. The default local
backend is faster-whisper (MIT) with OpenAI Whisper weights (MIT) - both commercially usable.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

import numpy as np


@dataclass
class Transcript:
    text: str
    language: str          # ISO 639-1, e.g. "en", "ar"
    confidence: float      # 0..1 (language probability for whisper)


class SpeechToText(Protocol):
    available: bool
    name: str

    def transcribe(self, audio: np.ndarray, sample_rate: int, language_hint: str = "auto") -> Transcript: ...


class NullSTT:
    available = False
    name = "none"

    def transcribe(self, audio, sample_rate, language_hint="auto") -> Transcript:
        raise RuntimeError("no speech-to-text backend installed (pip install faster-whisper)")


class FasterWhisperSTT:
    """Local Whisper via CTranslate2. Model weights download once to the companion's model folder."""

    def __init__(self, model_size: str = "small", model_dir: str | None = None, device: str = "auto", compute_type: str = "int8"):
        from faster_whisper import WhisperModel   # optional dependency
        self.name = f"faster-whisper-{model_size}"
        self.available = True
        self._model = WhisperModel(model_size, device=device, compute_type=compute_type, download_root=model_dir)

    def transcribe(self, audio: np.ndarray, sample_rate: int, language_hint: str = "auto") -> Transcript:
        if sample_rate != 16000:
            raise ValueError("audio must be 16 kHz mono")
        language = None if language_hint in ("", "auto") else language_hint
        # Mixing vocabulary helps recognition of engineering words in both languages.
        prompt = "Mixing and mastering: vocal, harsh, sibilance, de-esser, compression, reverb, EQ, loudness, LUFS. " \
                 "الصوت، حاد، الريفيرب، الكومبريسور، أوضح، أدفى"
        segments, info = self._model.transcribe(audio, language=language, vad_filter=True, beam_size=5, initial_prompt=prompt)
        text = " ".join(s.text.strip() for s in segments).strip()
        return Transcript(text=text, language=info.language or (language or ""), confidence=float(info.language_probability or 0.0))


def default_stt(model_size: str = "small", model_dir: str | None = None) -> SpeechToText:
    try:
        return FasterWhisperSTT(model_size=model_size, model_dir=model_dir)
    except Exception:
        return NullSTT()
