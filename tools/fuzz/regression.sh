#!/usr/bin/env sh
set -u
COMPILER="${1:?usage: regression.sh <compiler>}"
case "$COMPILER" in
  /*) ;;
  *) COMPILER="$(pwd)/$COMPILER" ;;
esac
export DMC_STDLIB="${DMC_STDLIB:-$(pwd)/stdlib}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FAILED=0

expect_clean_failure() {
  name="$1"
  shift
  "$COMPILER" build "$WORK/$name.dmc" --emit-c -o "$WORK/$name.c" >"$WORK/$name.out" 2>&1
  code=$?
  if [ "$code" -ne 1 ]; then
    echo "FAIL $name: expected exit code 1, got $code"
    FAILED=1
    return
  fi
  if ! grep -q "$1" "$WORK/$name.out"; then
    echo "FAIL $name: expected message containing '$1'"
    FAILED=1
    return
  fi
  echo "PASS $name"
}

printf 'fn main() -> int {\n    let s = "abc;\n    return 0;\n}\n' > "$WORK/unterminated_string.dmc"
expect_clean_failure unterminated_string "unterminated string"

printf '' > "$WORK/empty_file.dmc"
"$COMPILER" build "$WORK/empty_file.dmc" --emit-c -o "$WORK/empty_file.c" >/dev/null 2>&1
code=$?
if [ "$code" -ge 128 ]; then
  echo "FAIL empty_file: crashed with $code"
  FAILED=1
else
  echo "PASS empty_file"
fi

printf 'fn main() -> int { return (((((((((((((((((((((((((((((( 1 ; }\n' > "$WORK/unbalanced.dmc"
"$COMPILER" build "$WORK/unbalanced.dmc" --emit-c -o "$WORK/unbalanced.c" >/dev/null 2>&1
code=$?
if [ "$code" -ge 128 ]; then
  echo "FAIL unbalanced: crashed with $code"
  FAILED=1
else
  echo "PASS unbalanced"
fi

exit "$FAILED"
