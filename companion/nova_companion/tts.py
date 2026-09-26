"""Text-to-speech backends. The default on macOS is the system `say` command (Apple system voices,
including Arabic voices such as "Majed"); elsewhere espeak-ng if installed. Replaceable."""

from __future__ import annotations

import platform
import shutil
import subprocess
from typing import Protocol


class TextToSpeech(Protocol):
    available: bool
    name: str

    def speak(self, text: str, language: str = "en") -> None: ...


class NullTTS:
    available = False
    name = "none"

    def speak(self, text, language="en") -> None:
        raise RuntimeError("no text-to-speech backend available")


class CommandTTS:
    """Speaks through a system command without blocking the request (the previous utterance is
    interrupted, like a person starting a new sentence)."""

    def __init__(self, name: str, build_cmd):
        self.name = name
        self.available = True
        self._build = build_cmd
        self._proc: subprocess.Popen | None = None

    def speak(self, text: str, language: str = "en") -> None:
        text = text.strip()
        if not text:
            return
        if self._proc is not None and self._proc.poll() is None:
            self._proc.terminate()
        self._proc = subprocess.Popen(self._build(text[:2000], language), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def mac_arabic_voice(voice_list: str) -> str | None:
    """First installed Arabic voice from `say -v '?'` output (e.g. "Majed  ar_SA  # ...")."""
    for line in voice_list.splitlines():
        parts = line.split("#")[0].split()
        if len(parts) >= 2 and parts[-1].lower().startswith("ar"):
            return " ".join(parts[:-1])
    return None


def _mac_say_factory():
    try:
        voices = subprocess.run(["say", "-v", "?"], capture_output=True, text=True, timeout=10).stdout
    except Exception:
        voices = ""
    arabic = mac_arabic_voice(voices)

    def build(text: str, language: str) -> list[str]:
        cmd = ["say"]
        if language.startswith("ar") and arabic:
            cmd += ["-v", arabic]       # e.g. Majed; install more in System Settings > Accessibility > Spoken Content
        return cmd + [text]
    return build


def _espeak(text: str, language: str) -> list[str]:
    return ["espeak-ng", "-v", "ar" if language.startswith("ar") else "en", text]


def default_tts() -> TextToSpeech:
    if platform.system() == "Darwin" and shutil.which("say"):
        return CommandTTS("macos-say", _mac_say_factory())
    if shutil.which("espeak-ng"):
        return CommandTTS("espeak-ng", _espeak)
    return NullTTS()
