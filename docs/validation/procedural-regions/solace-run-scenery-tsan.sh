#!/usr/bin/env bash
set -euo pipefail
cd /workspace/scratch/87b29863f015/solace-procedural-regions-review
out=/tmp/solace-scenery-tsan
mkdir -p "$out"
sources=(src/geo.cpp src/region_travel.cpp src/procedural_regions.cpp src/world.cpp src/road_network.cpp src/bridges.cpp src/settlements.cpp src/road_furniture.cpp src/landmarks.cpp src/road_traffic.cpp src/scenery.cpp src/entities.cpp src/airport_scenery.cpp tests/region_scenery_isolation_test.cpp)
printf '%s\n' "${sources[@]}" > "$out/sources.txt"
printf '%s\n' "${sources[@]}" | xargs -P 4 -I{} bash -c 'g++ -std=c++17 -O1 -g -fsanitize=thread -fno-omit-frame-pointer -pthread -I src -c "$1" -o "/tmp/solace-scenery-tsan/$(basename "$1").o"' _ {}
g++ -std=c++17 -O1 -g -fsanitize=thread -fno-omit-frame-pointer -pthread "$out"/*.o -o "$out/region_scenery_isolation_test"
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 "$out/region_scenery_isolation_test"
