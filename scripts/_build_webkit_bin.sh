#!/bin/bash
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
bash "$SCRIPT_DIR/build_and_stage_webkit.sh"
