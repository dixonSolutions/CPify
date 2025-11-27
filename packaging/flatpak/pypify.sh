#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="/app/pypify"
VENV_PATH="${APP_ROOT}/venv"

if [ -f "${VENV_PATH}/bin/activate" ]; then
  # shellcheck disable=SC1090
  source "${VENV_PATH}/bin/activate"
fi

export PYPIFY_DATA_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/Pypify"
exec python3 "${APP_ROOT}/main.py"

