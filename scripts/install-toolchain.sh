#!/usr/bin/env bash
# Installs or updates the CE C/C++ toolchain (CEdev) and the matching clibs.8xg.
# Usage: scripts/install-toolchain.sh [--force] [vX.Y]
set -euo pipefail

PREFIX="${CEDEV_PREFIX:-/opt/CEdev}"
PROFILE_SNIPPET=/etc/profile.d/cedev.sh
TOOLCHAIN_REPO=https://github.com/CE-Programming/toolchain
LIBRARIES_REPO=https://github.com/CE-Programming/libraries

force=0
tag=
for arg in "$@"; do
	case "$arg" in
		-f | --force) force=1 ;;
		-h | --help)
			sed -n '2,3s/^# //p' "$0"
			exit 0
			;;
		v*) tag="$arg" ;;
		*)
			echo "Unknown argument: $arg" >&2
			exit 1
			;;
	esac
done

sudo_if_needed() {
	if [[ -w "$(dirname "$PREFIX")" ]]; then "$@"; else sudo "$@"; fi
}

missing=()
for dep in make curl tar gzip 'libz.so.1()(64bit)'; do
	rpm -q --whatprovides "$dep" &>/dev/null || missing+=("$dep")
done
if ((${#missing[@]})); then
	echo "Installing host dependencies: ${missing[*]}"
	sudo dnf install "${missing[@]}"
fi

if [[ -z "$tag" ]]; then
	latest_url=$(curl -fsSLo /dev/null -w '%{url_effective}' "$TOOLCHAIN_REPO/releases/latest")
	tag="${latest_url##*/}"
fi

installed=
if [[ -x "$PREFIX/bin/cedev-config" ]]; then
	installed=$("$PREFIX/bin/cedev-config" --version)
fi

if [[ "$installed" == "$tag" && $force -eq 0 ]]; then
	echo "CEdev $tag is already installed at $PREFIX"
else
	echo "Installing CEdev $tag to $PREFIX${installed:+ (replacing $installed)}"

	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT

	curl -fL --progress-bar -o "$work/CEdev.tar.gz" "$TOOLCHAIN_REPO/releases/download/$tag/CEdev-Linux.tar.gz"
	tar -xzf "$work/CEdev.tar.gz" -C "$work"

	if curl -fsSL -o "$work/CEdev/clibs.8xg" "$LIBRARIES_REPO/releases/download/$tag/clibs.8xg"; then
		echo "Fetched clibs.8xg $tag"
	else
		echo "warning: no clibs.8xg release for $tag; get it from $LIBRARIES_REPO/releases" >&2
	fi

	sudo_if_needed rm -rf "$PREFIX.old"
	[[ -e "$PREFIX" ]] && sudo_if_needed mv "$PREFIX" "$PREFIX.old"
	sudo_if_needed mv "$work/CEdev" "$PREFIX"
	sudo_if_needed chown -R --reference="$(dirname "$PREFIX")" "$PREFIX"
	sudo_if_needed rm -rf "$PREFIX.old"
fi

snippet="export CEDEV=$PREFIX
export PATH=\"\$CEDEV/bin:\$PATH\""
if [[ "$(cat "$PROFILE_SNIPPET" 2>/dev/null)" != "$snippet" ]]; then
	echo "Writing $PROFILE_SNIPPET"
	printf '%s\n' "$snippet" | sudo tee "$PROFILE_SNIPPET" >/dev/null
fi

echo "$("$PREFIX/bin/ez80-clang" --version | head -1)"
[[ -f "$PREFIX/clibs.8xg" ]] && echo "Note $PREFIX/clibs.8xg must be sent to the calculator"
exit 0
