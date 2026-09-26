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

    def __init__(self, model_size: str = "small", model_dir: str | None = None, device: str = "auto", compute_type: str = "int8",
                 languages: tuple[str, ...] = ("ar", "en")):
        from faster_whisper import WhisperModel   # optional dependency
        self.name = f"faster-whisper-{model_size}"
        self.available = True
        # NOVA understands English and Arabic: automatic detection chooses only among these, so a
        # short Egyptian command is never transcribed as, say, Persian or German.
        self.languages = languages
        self._model = WhisperModel(model_size, device=device, compute_type=compute_type, download_root=model_dir)

    def detect(self, audio: np.ndarray) -> tuple[str, float]:
        _, _, all_probs = self._model.detect_language(audio)
        allowed = [(code, p) for code, p in all_probs if code in self.languages]
        if not allowed:
            return self.languages[0], 0.0
        code, p = max(allowed, key=lambda cp: cp[1])
        total = sum(pp for _, pp in allowed)
        return code, (p / total if total > 0 else 0.0)

    def transcribe(self, audio: np.ndarray, sample_rate: int, language_hint: str = "auto") -> Transcript:
        if sample_rate != 16000:
            raise ValueError("audio must be 16 kHz mono")
        language = None if language_hint in ("", "auto") else language_hint
        confidence = None
        if language is None and self.languages:
            language, confidence = self.detect(audio)
        # Mixing vocabulary helps recognition of engineering words in both languages.
        prompt = "Mixing and mastering: vocal, harsh, sibilance, de-esser, compression, reverb, EQ, loudness, LUFS. " \
                 "الصوت، حاد، الريفيرب، الكومبريسور، أوضح، أدفى"
        segments, info = self._model.transcribe(audio, language=language, vad_filter=True, beam_size=5, initial_prompt=prompt)
        text = " ".join(s.text.strip() for s in segments).strip()
        conf = confidence if confidence is not None else float(info.language_probability or 0.0)
        return Transcript(text=text, language=language or info.language or "", confidence=float(conf))


def default_stt(model_size: str = "small", model_dir: str | None = None) -> SpeechToText:
    try:
        return FasterWhisperSTT(model_size=model_size, model_dir=model_dir)
    except Exception:
        return NullSTT()
