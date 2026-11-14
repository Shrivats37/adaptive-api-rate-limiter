#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

build()   { mkdir -p build && (cd build && cmake -G "Unix Makefiles" .. > /dev/null && make); }
ring()    { g++ -std=c++17 -Iinclude test_ring.cpp -o test_ring && ./test_ring; }
threads() { g++ -std=c++17 -O2 -pthread -Iinclude test_threads.cpp -o test_threads && ./test_threads; }

case "$1" in
  build)   build ;;
  run)     build && ./build/rate_limiter_app ;;
  ring)    ring ;;
  threads) threads ;;
  test)    ring && threads ;;
  clean)   rm -rf build test_ring test_threads ;;
  *)       echo "Usage: ./dev.sh {build|run|ring|threads|test|clean}" ;;
esac
