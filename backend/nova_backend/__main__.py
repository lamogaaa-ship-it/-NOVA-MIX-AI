import argparse
import logging

import uvicorn


def main() -> None:
    p = argparse.ArgumentParser(description="NOVA Cloud backend")
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=8080)
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s %(message)s")
    uvicorn.run("nova_backend.app:app", host=a.host, port=a.port, proxy_headers=True)


if __name__ == "__main__":
    main()
