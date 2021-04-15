#!/usr/bin/env bash

BASE_DIR="$(pwd)"

docker build -t local/obs-node-linux-dev:latest -f "$BASE_DIR/linux-dev/Dockerfile" "$BASE_DIR/linux-dev"
docker run --rm -ti --privileged \
  --cap-add sys_ptrace \
  -p127.0.0.1:2222:22 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v "$(pwd)":/obs-node \
  local/obs-node-linux-dev:latest \
  bash