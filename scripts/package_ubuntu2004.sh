#!/usr/bin/env bash
# Build the Ubuntu 20.04 x86_64 install tree and pack it.
# The binary links glibc and libsqlite3 from the machine that runs this script.
# Run it on Ubuntu 20.04 x86_64. A newer host produces a binary that may not
# run on 20.04.
set -euo pipefail

usage() {
  echo "usage: scripts/package_ubuntu2004.sh [--build-dir DIR]" >&2
  echo "  Without --build-dir, configures build-release as Release and builds rrd-convert." >&2
}

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      if [[ $# -lt 2 || -z "${2}" ]]; then
        echo "error: --build-dir needs a directory" >&2
        exit 2
      fi
      build_dir="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

arch="$(uname -m)"
if [[ "${arch}" != "x86_64" ]]; then
  echo "error: this package is for x86_64, not ${arch}" >&2
  exit 1
fi

if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "20.04" ]]; then
    echo "warning: ${PRETTY_NAME:-this host} is not Ubuntu 20.04; the binary may need a newer glibc" >&2
  fi
fi

if [[ -z "${build_dir}" ]]; then
  build_dir="${root}/build-release"
  cmake -S "${root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRRD_CONVERT_BUILD_TESTS=OFF
  cmake --build "${build_dir}" -j"$(nproc)" --target rrd-convert
fi

if [[ ! -x "${build_dir}/rrd-convert" ]]; then
  echo "error: ${build_dir}/rrd-convert is missing; build it before packaging" >&2
  exit 1
fi

version="0.1.0"
name="rrd-convert-${version}-ubuntu20.04-x86_64"
dist="${root}/dist"
prefix="${dist}/${name}"
rm -rf "${prefix}"
mkdir -p "${prefix}"
cmake --install "${build_dir}" --prefix "${prefix}"

cat > "${prefix}/README.txt" <<'EOF'
rrd-convert 0.1.0 for Ubuntu 20.04 x86_64.

The binary needs libsqlite3.so.0 from the system. It does not include a ROS 2
install. Open the .rrd with Rerun Viewer 0.28.x or 0.29.x. Older viewers cannot
read this file. H.264 and H.265 playback needs FFmpeg 5.1 or newer and a stream
without B-frames. Ubuntu 20.04 apt FFmpeg is 4.2, so install a newer FFmpeg or
use the Docker image in the source repository.

  rrd-convert convert <bag-dir|file.db3> -o run.rrd
EOF

archive="${dist}/${name}.tar.gz"
tar -C "${dist}" -czf "${archive}" "${name}"
echo "wrote ${archive}"
