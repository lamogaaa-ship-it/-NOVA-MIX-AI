"""Configuration from the environment only - no secret is ever read from the repository."""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass, field


def _csv(name: str, default: str) -> list[str]:
    return [v.strip() for v in os.environ.get(name, default).split(",") if v.strip()]


def token_id(token: str) -> str:
    """Stable, non-reversible id for a client token (used for logs, limits and storage keys)."""
    return hashlib.sha256(token.encode("utf-8")).hexdigest()[:16]


@dataclass
class Settings:
    # Claude API key: environment / secret manager only (ANTHROPIC_API_KEY).
    anthropic_api_key: str | None = field(default_factory=lambda: os.environ.get("ANTHROPIC_API_KEY"))
    # Client tokens allowed to use this backend, as sha256 hex digests (NOVA_CLIENT_TOKEN_SHA256,
    # comma separated) - the backend never needs the plain tokens.
    client_token_hashes: set[str] = field(default_factory=lambda: set(_csv("NOVA_CLIENT_TOKEN_SHA256", "")))
    allowed_models: list[str] = field(default_factory=lambda: _csv("NOVA_ALLOWED_MODELS", "claude-fable-5-1,claude-opus-5,claude-sonnet-5"))
    max_tokens_cap: int = field(default_factory=lambda: int(os.environ.get("NOVA_MAX_TOKENS_CAP", "16000")))
    max_request_bytes: int = field(default_factory=lambda: int(os.environ.get("NOVA_MAX_REQUEST_BYTES", str(2 * 1024 * 1024))))
    requests_per_minute: int = field(default_factory=lambda: int(os.environ.get("NOVA_REQUESTS_PER_MINUTE", "30")))
    data_dir: str = field(default_factory=lambda: os.environ.get("NOVA_DATA_DIR", os.path.join(os.path.dirname(__file__), "..", ".data")))

    def token_allowed(self, token: str) -> bool:
        return hashlib.sha256(token.encode("utf-8")).hexdigest() in self.client_token_hashes
