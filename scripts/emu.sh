#!/usr/bin/env bash
# Builds PCAS, sends it to a CEmu instance (starting one if needed), and launches it through Cesium.
# Usage: scripts/emu.sh [--libs] [--no-build] [--no-launch]
set -euo pipefail

cd "$(dirname "$0")/.."

ROM="${CEMU_ROM:-tmp/TI84+CE.rom}"
ID="${CEMU_ID:-pcas}"
CESIUM_APP="${CEMU_CESIUM_APP:-4}"
CEDEV="${CEDEV:-/opt/CEdev}"

libs=0
build=1
launch=1
for arg in "$@"; do
	case "$arg" in
		--libs) libs=1 ;;
		--no-build) build=0 ;;
		--no-launch) launch=0 ;;
		-h | --help)
			sed -n '2,3s/^# //p' "$0"
			exit 0
			;;
		*)
			echo "Unknown argument: $arg" >&2
			exit 1
			;;
	esac
done

((build)) && make

files=("\"$PWD/bin/PCAS.8xp\"")
((libs)) && files=("\"$CEDEV/clibs.8xg\"" "${files[@]}")

if ! pgrep -f "cemu --id $ID( |$)" >/dev/null; then
	[[ -f "$ROM" ]] || {
		echo "ROM not found at $ROM (set CEMU_ROM)" >&2
		exit 1
	}
	echo "Starting CEmu ($ID)"
	setsid cemu --id "$ID" -r "$PWD/$ROM" &>/dev/null &
	sleep 5
fi

sequence='"key|clear", "delay|500", "key|clear", "delay|300"'
if ((launch)); then
	sequence+=", \"key|apps\", \"delay|800\", \"key|$CESIUM_APP\", \"delay|2500\", \"key|8\", \"delay|500\", \"key|enter\""
fi

test="${XDG_RUNTIME_DIR:-/tmp}/cemu-$ID.json"
cat >"$test" <<EOF
{
  "transfer_files": [$(IFS=,; echo "${files[*]}")],
  "target": {"name": "PCAS", "isASM": true},
  "sequence": [$sequence],
  "hashes": {}
}
EOF

cemu --id "$ID" --no-test-dialog -t "$test"
