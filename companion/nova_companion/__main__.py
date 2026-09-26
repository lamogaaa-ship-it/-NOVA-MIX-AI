import argparse
import os

import uvicorn

from .app import Services, create_app
from .audio_input import NullRecorder, default_recorder
from .embed import default_embedder
from .stt import NullSTT, default_stt
from .tts import NullTTS, default_tts


def main() -> None:
    p = argparse.ArgumentParser(description="NOVA Companion (local voice + analysis helper for NOVA MIX AI)")
    p.add_argument("--port", type=int, default=47800)
    p.add_argument("--stt-model", default=os.environ.get("NOVA_STT_MODEL", "small"),
                   help="faster-whisper model size: tiny, base, small (default), medium, large-v3")
    p.add_argument("--model-dir", default=os.environ.get("NOVA_MODEL_DIR"))
    p.add_argument("--no-voice", action="store_true", help="disable microphone + speech-to-text")
    p.add_argument("--no-tts", action="store_true")
    a = p.parse_args()
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
