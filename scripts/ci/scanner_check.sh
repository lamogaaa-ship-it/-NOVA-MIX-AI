#!/usr/bin/env bash
# End-to-end check of the out-of-process plug-in scanner on a real, built plug-in:
#   1. scans a folder containing the plug-in and validates the database against the shared schema
#   2. simulates a crash during a previous scan (leftover pending entry) and checks the plug-in
#      that crashed is blocklisted and skipped
# usage: scripts/ci/scanner_check.sh <nova-plugin-scanner> <plugin bundle> [python]
set -euo pipefail
SCANNER="$1"; BUNDLE="$2"; PY="${3:-python3}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
mkdir -p "$WORK/plugins"
cp -R "$BUNDLE" "$WORK/plugins/"
export NOVA_USER_DATA_DIR="$WORK/userdata"

"$SCANNER" --output "$WORK/db.json" --path "$WORK/plugins"
"$PY" - "$WORK/db.json" "$ROOT/shared/schemas/plugin-database.schema.json" <<'PYEOF'
import json, sys, jsonschema
db = json.load(open(sys.argv[1])); jsonschema.validate(db, json.load(open(sys.argv[2])))
ok = [p for p in db["plugins"] if p["loaded_ok"] and p["num_params"] > 0 and p.get("description_xml")]
assert ok, "no plug-in loaded with parameters"
print("scanner database valid:", [(p["name"], p["format"], p["num_params"], p["capabilities"]) for p in ok])
PYEOF

# crash recovery: a pending entry left by a crashed scan must be blocklisted and skipped
ID="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['plugins'][0]['file'])" "$WORK/db.json")"
printf '%s' "$ID" > "$WORK/scan_pending.txt"
OUT="$("$SCANNER" --output "$WORK/db.json" --path "$WORK/plugins" 2>&1)"
echo "$OUT"
grep -qF "$ID" "$WORK/scan_blocklist.txt"
echo "$OUT" | grep -q "skip (blocked)"
[ ! -e "$WORK/scan_pending.txt" ]
echo "crash blocklist OK"
