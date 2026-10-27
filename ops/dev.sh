#!/bin/sh
# Run a command inside the build/benchmark image with this repo at /src and
# the ITCH data volume at /data. Used on machines where Linux is only
# available through Docker (the README numbers were taken this way).
#   ops/dev.sh cmake -S . -B build && ops/dev.sh cmake --build build
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
docker image inspect itchfeed-dev >/dev/null 2>&1 ||
    docker build -t itchfeed-dev -f "$here/ops/dev.Dockerfile" "$here/ops"
MSYS_NO_PATHCONV=1 exec docker run --rm --privileged -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/src \
    -v "$here:/src" -v itchfeed-data:/data -w /src itchfeed-dev "$@"
