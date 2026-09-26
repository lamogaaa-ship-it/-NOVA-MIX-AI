# NOVA Cloud backend

This is an authenticated proxy between the NOVA MIX AI plug-in and the Claude API. The plug-in never holds the provider key.

```
plug-in --(Bearer client token, analysis numbers + text, no audio)--> NOVA Cloud --(ANTHROPIC_API_KEY)--> Claude API
```

## Run

```sh
pip install -r requirements.txt
export ANTHROPIC_API_KEY=...            # from your secret manager, never committed
export NOVA_CLIENT_TOKEN_SHA256=$(printf '%s' "$CLIENT_TOKEN" | sha256sum | cut -d' ' -f1)
python -m nova_backend --port 8080
```

In NOVA's settings, set the NOVA Cloud URL to this server and the NOVA Cloud token to the plain client token. The server stores only the token's SHA-256.

## Endpoints

| | |
|---|---|
| `GET /v1/health` | status, allowed models, whether a provider key is configured (never the key) |
| `POST /v1/engineer/messages` | Messages API request from the plug-in (see below) |
| `GET/PUT/DELETE /v1/prefs` | optional, opt-in sync of the learned taste profile |

What `POST /v1/engineer/messages` does:

- validates the request: only known fields are accepted, models come from an allow-list, and `max_tokens` is capped;
- rate-limits per client token;
- forwards the request through the official Anthropic Python SDK. It streams internally and adds the `fallbacks: "default"` server-side refusal fallback when the plug-in sends `x-nova-fallbacks: default`;
- returns the Messages API response unchanged. Errors keep the Messages API error shape.

## Configuration (environment)

| Variable | Default | |
|---|---|---|
| `ANTHROPIC_API_KEY` | – | provider key (required to serve requests) |
| `NOVA_CLIENT_TOKEN_SHA256` | – | comma-separated SHA-256 digests of allowed client tokens |
| `NOVA_ALLOWED_MODELS` | `claude-fable-5-1,claude-opus-5,claude-sonnet-5` | |
| `NOVA_MAX_TOKENS_CAP` | 16000 | |
| `NOVA_REQUESTS_PER_MINUTE` | 30 | per client token |
| `NOVA_MAX_REQUEST_BYTES` | 2 MiB | |
| `NOVA_DATA_DIR` | `backend/.data` | preference sync storage |

## Privacy

- Message content is never logged. Logs contain only the token id (a hash), model, request size, latency and status.
- Audio never reaches this server.

## Tests

```sh
pip install -r requirements-dev.txt && pytest
```
