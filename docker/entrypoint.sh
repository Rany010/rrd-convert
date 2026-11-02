#!/usr/bin/env bash
# Container entrypoint. "view" serves the Rerun web viewer; every other
# command is passed to rrd-convert.
set -euo pipefail

if [[ "${1:-}" == "view" ]]; then
  shift
  web_port="${RRD_CONVERT_WEB_PORT:-9090}"
  grpc_port="${RRD_CONVERT_GRPC_PORT:-9876}"
  echo "Open http://127.0.0.1:${web_port} in a browser on the host." >&2
  exec rerun \
    --serve-web \
    --bind 0.0.0.0 \
    --port "${grpc_port}" \
    --web-viewer-port "${web_port}" \
    --persist-state false \
    "$@"
fi

exec rrd-convert "$@"
