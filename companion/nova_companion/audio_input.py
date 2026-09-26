"""Microphone capture for push-to-talk. The companion owns the microphone so the DAW's audio
devices are never touched by the plug-in."""

from __future__ import annotations

import threading
from typing import Protocol

import numpy as np

SAMPLE_RATE = 16000   # what speech recognisers expect


class Recorder(Protocol):
    available: bool
    name: str

    def start(self) -> None: ...
    def stop(self) -> np.ndarray: ...   # mono float32 at SAMPLE_RATE


class NullRecorder:
    available = False
    name = "none"

    def start(self) -> None:
        raise RuntimeError("no microphone backend available (install the 'sounddevice' package)")

    def stop(self) -> np.ndarray:
        return np.zeros(0, dtype=np.float32)


class SoundDeviceRecorder:
    """Default input device via PortAudio (python-sounddevice, MIT)."""

    name = "sounddevice"

    def __init__(self, max_seconds: float = 60.0):
        import sounddevice  # noqa: F401  (import check)
        self.available = True
        self.max_frames = int(max_seconds * SAMPLE_RATE)
        self._chunks: list[np.ndarray] = []
        self._frames = 0
        self._lock = threading.Lock()
        self._stream = None

    def _callback(self, indata, frames, time_info, status):  # PortAudio thread
        with self._lock:
            if self._frames < self.max_frames:
                self._chunks.append(indata[:, 0].copy())
                self._frames += frames

    def start(self) -> None:
        import sounddevice as sd
        with self._lock:
            self._chunks, self._frames = [], 0
        if self._stream is not None:
            self._stream.close()
        self._stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="float32", callback=self._callback)
        self._stream.start()

    def stop(self) -> np.ndarray:
        if self._stream is not None:
            self._stream.stop()
            self._stream.close()
            self._stream = None
        with self._lock:
            audio = np.concatenate(self._chunks) if self._chunks else np.zeros(0, dtype=np.float32)
            self._chunks, self._frames = [], 0
        return audio.astype(np.float32)


def default_recorder() -> Recorder:
    try:
        return SoundDeviceRecorder()
    except Exception:
        return NullRecorder()
