#!/bin/bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd -P)"
runtime="$project_dir/local/Mac32.app/Contents/MacOS/mac32"
if [[ ! -x "$runtime" ]]; then
    printf 'Build Mac32.app and copy it into local/ as described in README.md.\n' >&2
    exit 1
fi

log_dir="$(mktemp -d "$project_dir/local/session.XXXXXX")"
log_file="$log_dir/session.log"
printf 'DiRT 3 runtime log: %s\n' "$log_file"
exec /usr/bin/python3 "$project_dir/probe.py" run --engine "$runtime" --log "$log_file" "$@"
