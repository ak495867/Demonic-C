#!/usr/bin/env sh
set -e
ROOT="$(cd -- "$(dirname -- "$0")/.." && pwd)"
exec "$ROOT/bin/dmc-native" test "$@"
