import argparse
import os
import pathlib
import platform

import uvicorn

from .app import Services, create_app
from .audio_input import NullRecorder, default_recorder
from .embed import default_embedder
from .stt import NullSTT, default_stt
from .tts import NullTTS, default_tts


def default_model_dir() -> str:
    """Speech models live next to NOVA's other user data (downloaded once on first use)."""
    home = pathlib.Path.home()
    base = home / "Library" / "Application Support" if platform.system() == "Darwin" else home / ".config"
    d = base / "NOVA MIX AI" / "models"
    d.mkdir(parents=True, exist_ok=True)
    return str(d)


def main() -> None:
    p = argparse.ArgumentParser(description="NOVA Companion (local voice + analysis helper for NOVA MIX AI)")
    p.add_argument("--port", type=int, default=47800)
    p.add_argument("--stt-model", default=os.environ.get("NOVA_STT_MODEL", "small"),
                   help="faster-whisper model size: tiny, base, small (default), medium, large-v3")
    p.add_argument("--model-dir", default=os.environ.get("NOVA_MODEL_DIR") or default_model_dir())
    p.add_argument("--no-voice", action="store_true", help="disable microphone + speech-to-text")
    p.add_argument("--no-tts", action="store_true")
    p.add_argument("--self-test", action="store_true", help="check that the bundled speech stack imports, then exit")
    a = p.parse_args()
    if a.self_test:
        import ctranslate2
        import faster_whisper
        import sounddevice
        print(f"ok faster-whisper {faster_whisper.__version__} ctranslate2 {ctranslate2.__version__} portaudio {sounddevice.get_portaudio_version()[1]}")
        return
    services = Services(
        recorder=NullRecorder() if a.no_voice else default_recorder(),
        stt=NullSTT() if a.no_voice else default_stt(a.stt_model, a.model_dir),
        tts=NullTTS() if a.no_tts else default_tts(),
        embedder=default_embedder(),
    )
    print(f"NOVA Companion: stt={services.stt.name} mic={services.recorder.name} tts={services.tts.name} embed={services.embedder.name}")
    # 127.0.0.1 only: the companion is never reachable from the network.
    uvicorn.run(create_app(services), host="127.0.0.1", port=a.port, log_level="warning")


if __name__ == "__main__":
    main()
