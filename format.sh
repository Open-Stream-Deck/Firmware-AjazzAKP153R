#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

formatter=${CLANG_FORMAT:-clang-format}
files=(src/*.c include/*.h tests/*.c)

case "${1:-}" in
    '') "$formatter" -i "${files[@]}" ;;
    --check) "$formatter" --dry-run --Werror "${files[@]}" ;;
    *) printf 'Usage: bash format.sh [--check]\n' >&2; exit 2 ;;
esac
