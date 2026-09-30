#!/bin/bash
# Prepares Claude Code on the web sessions: installs PlatformIO and pyserial
# and tries to pre-fetch the ESP8266 toolchain.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

pip install --quiet --disable-pip-version-check --root-user-action=ignore platformio pyserial

# Needs access to the PlatformIO registry (api.registry.platformio.org,
# dl.registry.platformio.org). Without it only tools/host_test.sh works.
if ! pio pkg install -e native >/dev/null 2>&1 || ! pio pkg install -e node1 >/dev/null 2>&1; then
  echo "session-start: PlatformIO registry not reachable; use tools/host_test.sh for unit tests" >&2
  ./tools/host_test.sh >/dev/null || true
fi
