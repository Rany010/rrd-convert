# Runtime image for converting a ROS 2 bag and viewing the .rrd in a browser.
# The binary is built on Ubuntu 24.04 and is not the Ubuntu 20.04 release artifact.
# Building requires network access to download the Rerun C++ SDK and Arrow.
#
# UBUNTU_IMAGE overrides the base when Docker Hub is unreachable. The default
# stays the official name so a normal `docker build` does not depend on a mirror.

ARG UBUNTU_IMAGE=ubuntu:24.04
FROM ${UBUNTU_IMAGE} AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        git \
        libsqlite3-dev \
        ninja-build \
        pkg-config \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY src ./src

ARG RRD_CONVERT_RERUN_URL=https://github.com/rerun-io/rerun/releases/download/0.28.2/rerun_cpp_sdk.zip

RUN cmake -S . -B /tmp/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DRRD_CONVERT_BUILD_TESTS=OFF \
        -DRRD_CONVERT_RERUN_URL="${RRD_CONVERT_RERUN_URL}" \
    && cmake --build /tmp/build -j"$(nproc)" --target rrd-convert

FROM ${UBUNTU_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive \
    PATH="/opt/rerun/bin:${PATH}" \
    HOME=/tmp

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        ffmpeg \
        libsqlite3-0 \
        python3 \
        python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /opt/rerun \
    && /opt/rerun/bin/pip install --no-cache-dir "rerun-sdk==0.28.2"

COPY --from=build /tmp/build/rrd-convert /usr/local/bin/rrd-convert
COPY docker/entrypoint.sh /usr/local/bin/rrd-convert-docker
RUN chmod 755 /usr/local/bin/rrd-convert /usr/local/bin/rrd-convert-docker

WORKDIR /data
EXPOSE 9090 9876
ENTRYPOINT ["rrd-convert-docker"]
