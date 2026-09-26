"""The upstream model call. Isolated behind a small interface so tests never touch the network."""

from __future__ import annotations

from typing import Any, Protocol

import anthropic

FALLBACK_BETA = "server-side-fallback-2026-07-01"   # pairs with the scalar form fallbacks="default"


class UpstreamError(Exception):
    def __init__(self, status: int, error_type: str, message: str):
        super().__init__(message)
        self.status = status
        self.error_type = error_type
        self.message = message


class Engine(Protocol):
    async def create(self, params: dict[str, Any], use_fallbacks: bool) -> str:
        """Run one Messages API request; returns the response message as JSON (API field names)."""
        ...


class AnthropicEngine:
    """Claude via the official Anthropic Python SDK. The API key comes from the environment."""

    def __init__(self, api_key: str | None):
        # max_retries: the SDK retries 408/409/429/5xx and connection errors with backoff.
        self.client = anthropic.AsyncAnthropic(api_key=api_key, max_retries=2, timeout=300.0)

    async def create(self, params: dict[str, Any], use_fallbacks: bool) -> str:
        kwargs = dict(params)
        betas: list[str] = []
        if use_fallbacks:
            kwargs["fallbacks"] = "default"
            betas.append(FALLBACK_BETA)
        try:
            # Streaming under the hood: long agent turns never hit the non-streaming time guard.
            async with self.client.beta.messages.stream(**kwargs, betas=betas) as stream:
                message = await stream.get_final_message()
        except anthropic.APIStatusError as e:
            body = e.body if isinstance(e.body, dict) else {}
            err = body.get("error", {}) if isinstance(body.get("error"), dict) else {}
            raise UpstreamError(e.status_code, err.get("type", "api_error"), err.get("message", str(e))) from e
        except anthropic.APIConnectionError as e:
            raise UpstreamError(502, "api_connection_error", "could not reach the model provider") from e
        return message.to_json(indent=None)
