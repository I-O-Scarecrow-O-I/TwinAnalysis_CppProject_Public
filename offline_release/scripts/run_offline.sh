#!/usr/bin/env bash
set -euo pipefail

IMAGE_TAR="${1:-images/twin_packaging_onnx_ms_ubuntu22.04_x86_64.tar}"
IMAGE_NAME="twin/packaging:onnx-ms"

echo "[1/3] docker load..."
docker load -i "${IMAGE_TAR}"

echo "[2/3] run container..."
mkdir -p ~/Projects/twin_sandbox/work

docker run --rm -p 8080:8080 \
  -e TWIN_WORKDIR=/opt/twin/work \
  -v ~/Projects/twin_sandbox/work:/opt/twin/work \
  -v ~./config.json:/opt/twin/app/config.json \
  --network host \
  twin/packaging:onnx-ms

echo "[3/3] done."