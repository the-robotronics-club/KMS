#!/usr/bin/env bash
# Publish a rack command straight to the broker, skipping the backend entirely.
#
#   ./firmware/tools/dispense.sh 7            # dispense slot 7
#   ./firmware/tools/dispense.sh 7 unlock     # a key return
#   ./firmware/tools/dispense.sh 7 dispense 10.123.223.5
#
# Why this script exists rather than a mosquitto_pub one-liner in the README:
# PowerShell mangles the payload. It strips the outer single quotes, then either
# swallows the inner double quotes or turns escaped backslashes into something
# mosquitto_pub reads as a command-line flag. The ESP32 logs "[mqtt] bad JSON"
# and you spend an hour blaming the firmware. Writing the JSON to a file and
# passing -f sidesteps every layer of shell quoting on every platform.
set -euo pipefail

SLOT="${1:-}"
ACTION="${2:-dispense}"
HOST="${3:-}"
DEVICE_UUID="${DEVICE_UUID:-11111111-2222-3333-4444-555555555555}"

if [[ -z "$SLOT" ]]; then
  echo "usage: $0 <slot 1-24> [dispense|unlock] [broker-host]" >&2
  exit 1
fi
if ! [[ "$SLOT" =~ ^[0-9]+$ ]] || (( SLOT < 1 || SLOT > 24 )); then
  echo "slot must be 1-24, got '$SLOT'" >&2
  exit 1
fi

# A fresh nonce every run. The firmware keeps a 64-entry ring and silently drops
# a repeat as a replay, so a hardcoded nonce works exactly once and then looks
# like the rack has died.
NONCE="$(head -c16 /dev/urandom | od -An -tx1 | tr -d ' \n')"

PAYLOAD="$(mktemp)"
trap 'rm -f "$PAYLOAD"' EXIT
cat > "$PAYLOAD" <<EOF
{"action":"$ACTION","slot_number":$SLOT,"nonce":"$NONCE"}
EOF

TOPIC="device/$DEVICE_UUID/rack/command"
echo "-> $TOPIC"
echo "   $(cat "$PAYLOAD")"

if [[ -n "$HOST" ]]; then
  mosquitto_pub -h "$HOST" -p 1883 -t "$TOPIC" -f "$PAYLOAD"
else
  # Default path: the broker running in this repo's docker compose stack.
  docker exec -i kms-merged-mosquitto-1 mosquitto_pub \
    -h localhost -t "$TOPIC" -m "$(cat "$PAYLOAD")"
fi

echo "published. watch the ESP32 log for '[mqtt] $ACTION slot $SLOT'"
