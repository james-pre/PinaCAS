#!/usr/bin/env bash
# Builds the PinaCAS app, sends it and its installer to a CEmu instance (starting one if needed), and runs the installer through Cesium.
# Usage: scripts/emu.sh [--debug] [--libs] [--no-build] [--no-install]
# The installer does not replace an installed app, so delete PinaCAS in Mem Management (2nd, +, 2, Apps) before reinstalling.
set -euo pipefail

cd "$(dirname "$0")/.."

ROM="${CEMU_ROM:-tmp/TI84+CE.rom}"
ID="${CEMU_ID:-pcas}"
CESIUM_APP="${CEMU_CESIUM_APP:-4}"
CEDEV="${CEDEV:-/opt/CEdev}"

libs=0
build=1
install=1
bindir=bin
for arg in "$@"; do
	case "$arg" in
		--debug) bindir=bin/debug ;;
		--libs) libs=1 ;;
		--no-build) build=0 ;;
		--no-install) install=0 ;;
		-h | --help)
			sed -n '2,4s/^# //p' "$0"
			exit 0
			;;
		*)
			echo "Unknown argument: $arg" >&2
			exit 1
			;;
	esac
done

if ((build)); then
	if [[ $bindir == bin/debug ]]; then
		make debug
	else
		make
	fi
fi

files=("\"$PWD/$bindir/PINACAS.8xp\"")
for appvar in "$bindir"/PinaCAS.*.8xv; do
	files+=("\"$PWD/$appvar\"")
done
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
if ((install)); then
	# 8 is P, which selects the first program starting with P in Cesium
	sequence+=", \"key|apps\", \"delay|800\", \"key|$CESIUM_APP\", \"delay|2500\", \"key|8\", \"delay|500\", \"key|enter\""
fi

test="${XDG_RUNTIME_DIR:-/tmp}/cemu-$ID.json"
cat >"$test" <<EOF
{
  "transfer_files": [$(IFS=,; echo "${files[*]}")],
  "target": {"name": "PINACAS", "isASM": true},
  "sequence": [$sequence],
  "hashes": {}
}
EOF

cemu --id "$ID" --no-test-dialog -t "$test"
