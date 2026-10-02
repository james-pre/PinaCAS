#!/usr/bin/env bash
# Writes src/version.h with the version from the latest git tag and the build date, leaving it untouched when nothing changed.
# On a tag vX.Y.Z the version is X.Y.Z. After it, the commit count and hash are added as build metadata: X.Y.Z+N.gHASH[.dirty].
set -euo pipefail

cd "$(dirname "$0")/.."

if describe=$(git describe --tags --long --dirty --match 'v[0-9]*' 2>/dev/null); then
	dirty=
	if [[ $describe == *-dirty ]]; then
		dirty=dirty
		describe=${describe%-dirty}
	fi

	hash=${describe##*-}
	describe=${describe%-*}
	count=${describe##*-}
	version=${describe%-*}
	version=${version#v}

	metadata=
	((count > 0)) && metadata=$count.$hash
	[[ -n $dirty ]] && metadata=${metadata:+$metadata.}$dirty
	version+=${metadata:++$metadata}
else
	version=0.0.0+git$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
fi

date=$(date +%F)

content="#pragma once

#define PCAS_VERSION \"$version\"
#define PCAS_BUILD_DATE \"$date\""

if [[ ! -f src/version.h || $(<src/version.h) != "$content" ]]; then
	printf '%s\n' "$content" >src/version.h
fi
