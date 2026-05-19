#!/usr/bin/env bash
set -euo pipefail

IMAGE_TAR="${1:-images/twin_packaging_onnx_ms_ubuntu22.04_x86_64.tar}"
IMAGE_NAME="twin/packaging:onnx-ms"

echo "[1/3] docker load..."
docker load -i "${IMAGE_TAR}"

echo "[2/3] run container..."
mkdir -p ~/Projects/twin_sandbox/work

docker run --rm -p 8084:8084 \
  -e TWIN_WORKDIR=/opt/twin/work \
  -v ~/Projects/twin_sandbox/work/config.json:/opt/twin/work/config.json \
  -v ~/Projects/twin_sandbox/work/requests:/opt/twin/work/requests \
  --network host \
  twin/packaging:onnx-ms
echo "[3/3] done."