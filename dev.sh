#!/usr/bin/env bash
# One entry point for everything. Usage: ./dev.sh {build|test|demo|bench|server|train|clean}
set -e
cd "$(dirname "$0")"

build() { cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null && cmake --build build -j 4; }

case "$1" in
  build)  build ;;
  test)   build && ./build/test_all ;;
  demo)   build && ./build/rate_limiter_demo ;;
  bench)  build && ./build/rate_limiter_bench ;;
  server) build && ./build/rate_limiter_server ;;
  train)  python3 tools/train_model.py ;;
  clean)  rm -rf build ;;
  *)      echo "Usage: ./dev.sh {build|test|demo|bench|server|train|clean}" ;;
esac
