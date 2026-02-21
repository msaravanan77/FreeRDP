#!/bin/bash
#
# start-proxy.sh — convenience wrapper to run freerdp-proxy from the build tree
#
# Usage:
#   ./start-proxy.sh                          # uses default config path below
#   ./start-proxy.sh /path/to/proxy.ini       # uses custom config
#   ./start-proxy.sh /path/to/proxy.ini &     # background mode
#
# The script sets LD_LIBRARY_PATH so the proxy finds its own shared libraries
# in the local build tree without requiring a system-wide install.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Default config location — override by passing a path as the first argument
CONFIG="${1:-/tmp/freerdp-proxy-test/proxy.ini}"

if [ ! -f "$BUILD_DIR/server/proxy/cli/freerdp-proxy" ]; then
    echo "ERROR: binary not found: $BUILD_DIR/server/proxy/cli/freerdp-proxy"
    echo "Run:  cd build && make -j\$(nproc) freerdp-proxy"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: config not found: $CONFIG"
    echo "See docs/proxy-build-and-usage.md for setup instructions."
    exit 1
fi

export LD_LIBRARY_PATH="\
$BUILD_DIR/libfreerdp:\
$BUILD_DIR/winpr/libwinpr:\
$BUILD_DIR/libfreerdp/core:\
$BUILD_DIR/server/proxy\
${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo "Starting freerdp-proxy with config: $CONFIG"
exec "$BUILD_DIR/server/proxy/cli/freerdp-proxy" "$CONFIG"
