#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Wextra -Iinclude -Isrc test/host/test_packetizer.cpp src/packetizer.cpp -o /tmp/test_packetizer
/tmp/test_packetizer
