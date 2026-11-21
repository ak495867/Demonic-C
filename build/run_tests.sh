#!/usr/bin/env sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"
if [ "$#" -eq 0 ]; then set -- test-files; fi
exec "$ROOT/dmc" test "$@"
