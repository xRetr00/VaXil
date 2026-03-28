#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_DIR="${ROOT_DIR}/.venv"
SERVER_URL="${SERVER_URL:-}"
NODE_ID="${NODE_ID:-laptop-vision-node}"
CAMERA_INDEX="${CAMERA_INDEX:-0}"
FPS="${FPS:-12}"
SEND_INTERVAL_MS="${SEND_INTERVAL_MS:-120}"
MAX_SNAPSHOTS_PER_SECOND="${MAX_SNAPSHOTS_PER_SECOND:-6}"
YOLO_EVERY_N_FRAMES="${YOLO_EVERY_N_FRAMES:-4}"
OPEN_TIMEOUT_SEC="${OPEN_TIMEOUT_SEC:-20}"
DEFAULT_MODEL_NAME="yolov8n.pt"
MODEL_NAME="${MODEL_NAME:-}"
OBJECTS_MIN_CONFIDENCE="${OBJECTS_MIN_CONFIDENCE:-0.60}"
GESTURES_MIN_CONFIDENCE="${GESTURES_MIN_CONFIDENCE:-0.70}"
DELTA_THRESHOLD="${DELTA_THRESHOLD:-0.12}"
PIP_INDEX_URL="${PIP_INDEX_URL:-}"

TORCH_INDEX_URL_ENV="${TORCH_INDEX_URL-}"
TORCH_INDEX_URL_DEFAULT="https://download.pytorch.org/whl/cpu"
TORCH_CUDA_INDEX_URL_DEFAULT="https://download.pytorch.org/whl/cu121"
TORCH_INDEX_URL="${TORCH_INDEX_URL_ENV:-${TORCH_INDEX_URL_DEFAULT}}"
TORCH_INDEX_URL_USER_SET=0
if [[ -n "${TORCH_INDEX_URL_ENV}" ]]; then
  TORCH_INDEX_URL_USER_SET=1
fi

DEBUG_UI="${DEBUG_UI:-}"
SKIP_INSTALL=0
NON_INTERACTIVE=0

ENV_CHECK_MESSAGE=""
ENV_MISSING_MODULES=""
ENV_PIP_CHECK_OK=0
ENV_TORCH_CUDA=""
ENV_TORCH_ISSUE=""

CUDA_RUNTIME_DETECTED=0
CUDA_DETECTION_REASON="none"

log_info() {
  echo "[INFO] $*"
}

log_warn() {
  echo "[WARN] $*"
}

log_error() {
  echo "[ERROR] $*" >&2
}

sanitize_value() {
  local value="${1-}"
  value="${value//$'\r'/}"
  printf '%s' "${value}"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

hash_text() {
  local value="$1"

  if need_cmd sha256sum; then
    printf '%s' "${value}" | sha256sum | awk '{print $1}'
    return
  fi

  if need_cmd shasum; then
    printf '%s' "${value}" | shasum -a 256 | awk '{print $1}'
    return
  fi

  if need_cmd openssl; then
    printf '%s' "${value}" | openssl dgst -sha256 -r | awk '{print $1}'
    return
  fi

  printf '%s' "${value}"
}

hash_file() {
  local path="$1"

  if [[ ! -f "${path}" ]]; then
    printf 'missing'
    return
  fi

  if need_cmd sha256sum; then
    sha256sum "${path}" | awk '{print $1}'
    return
  fi

  if need_cmd shasum; then
    shasum -a 256 "${path}" | awk '{print $1}'
    return
  fi

  if need_cmd openssl; then
    openssl dgst -sha256 -r "${path}" | awk '{print $1}'
    return
  fi

  wc -c < "${path}" | tr -d '[:space:]'
}

dependency_state_file() {
  local active_python="$1"
  if [[ "${active_python}" == "${VENV_DIR}/bin/python" ]]; then
    echo "${VENV_DIR}/.vision_node_deps_state"
  else
    echo "${ROOT_DIR}/.vision_node_deps_state"
  fi
}

read_saved_dependency_fingerprint() {
  local state_file="$1"
  if [[ -f "${state_file}" ]]; then
    sed -n '1p' "${state_file}" | tr -d '\r\n'
  fi
}

write_saved_dependency_fingerprint() {
  local state_file="$1"
  local fingerprint="$2"
  mkdir -p "$(dirname "${state_file}")"
  printf '%s\n' "${fingerprint}" > "${state_file}"
}

requirements_fingerprint() {
  local active_python="$1"
  local req_hash=""
  local py_meta=""

  req_hash="$(hash_file "${ROOT_DIR}/requirements.txt")"
  py_meta="$(${active_python} - <<'EOF'
import sys
print(f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}|{sys.executable}")
EOF
)"

  hash_text "${req_hash}|${py_meta}|${TORCH_INDEX_URL}"
}

contains_csv_item() {
  local csv="$1"
  local needle="$2"
  [[ ",${csv}," == *",${needle},"* ]]
}

detect_cuda_runtime() {
  CUDA_RUNTIME_DETECTED=0
  CUDA_DETECTION_REASON="none"

  if need_cmd nvidia-smi && nvidia-smi -L >/dev/null 2>&1; then
    CUDA_RUNTIME_DETECTED=1
    CUDA_DETECTION_REASON="nvidia-smi"
  elif need_cmd nvcc; then
    CUDA_RUNTIME_DETECTED=1
    CUDA_DETECTION_REASON="nvcc"
  elif [[ -f "/usr/local/cuda/version.json" || -f "/usr/local/cuda/version.txt" ]]; then
    CUDA_RUNTIME_DETECTED=1
    CUDA_DETECTION_REASON="cuda-home"
  fi

  if [[ ${CUDA_RUNTIME_DETECTED} -eq 1 ]]; then
    log_info "CUDA runtime detected (${CUDA_DETECTION_REASON}), but the vision node defaults to CPU-only torch wheels."
    if [[ ${TORCH_INDEX_URL_USER_SET} -eq 1 ]]; then
      log_info "Using user-selected PyTorch index: ${TORCH_INDEX_URL}"
    else
      log_info "Using CPU PyTorch index: ${TORCH_INDEX_URL}"
    fi
  else
    log_info "CUDA runtime not detected; using CPU PyTorch index."
  fi
}

usage() {
  cat <<'EOF'
Usage:
  ./run_vision_node.sh [options]

Options:
  --server-url URL                 WebSocket URL exposed by the main PC
  --node-id ID                     Stable node id (default: laptop-vision-node)
  --camera-index N                 OpenCV camera index (default: 0)
  --fps N                          Target FPS (default: 12)
  --send-interval-ms N             Minimum send interval (default: 120)
  --max-snapshots-per-second N     Hard send cap (default: 6)
  --yolo-every-n-frames N          Run YOLO every N frames (default: 4)
  --open-timeout-sec N             WebSocket opening handshake timeout (default: 20)
  --model-name NAME                YOLO model name/path (skips model selection prompt)
  --objects-min-confidence N       Object threshold (default: 0.60)
  --gestures-min-confidence N      Gesture threshold (default: 0.70)
  --delta-threshold N              Resend delta threshold (default: 0.12)
  --pip-index-url URL              Override pip package index for dependency install
  --torch-index-url URL            PyTorch wheel index (default: CPU-only)
  --debug-ui                       Enable OpenCV debug window
  --python PATH                    Python interpreter to use
  --skip-install                   Do not offer dependency install
  --non-interactive                Fail instead of prompting for missing values
  -h, --help                       Show this help
EOF
}

prompt_value() {
  local prompt_text="$1"
  local default_value="${2:-}"
  local result=""

  if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
    echo "${default_value}"
    return
  fi

  if [[ -n "${default_value}" ]]; then
    read -r -p "${prompt_text} [${default_value}]: " result
    sanitize_value "${result:-${default_value}}"
  else
    read -r -p "${prompt_text}: " result
    sanitize_value "${result}"
  fi
}

prompt_yes_no() {
  local prompt_text="$1"
  local default_value="${2:-y}"
  local answer=""

  if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
    [[ "${default_value}" =~ ^[Yy]$ ]] && return 0 || return 1
  fi

  read -r -p "${prompt_text} [${default_value}]: " answer
  answer="${answer:-${default_value}}"
  [[ "${answer}" =~ ^[Yy]$ ]]
}

ensure_python() {
  if need_cmd "${PYTHON_BIN}"; then
    return
  fi

  if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
    log_error "Python interpreter not found: ${PYTHON_BIN}"
    exit 1
  fi

  log_warn "Python interpreter not found: ${PYTHON_BIN}"
  PYTHON_BIN="$(prompt_value "Enter a valid Python executable" "python3")"
  if ! need_cmd "${PYTHON_BIN}"; then
    log_error "Python interpreter not found: ${PYTHON_BIN}"
    exit 1
  fi
}

ensure_virtualenv() {
  if [[ -x "${VENV_DIR}/bin/python" ]]; then
    return
  fi

  if [[ "${SKIP_INSTALL}" -eq 1 ]]; then
    log_warn "Virtual environment not found at ${VENV_DIR}; using system Python."
    return
  fi

  if prompt_yes_no "Create a local virtual environment at ${VENV_DIR}?" "y"; then
    "${PYTHON_BIN}" -m venv "${VENV_DIR}"
  fi
}

check_python_environment() {
  local active_python="$1"
  local output=""
  local status=0

  set +e
  output="$(${active_python} - <<'EOF'
import importlib.util
import subprocess
import sys

modules = ("cv2", "mediapipe", "ultralytics", "websockets", "torch", "torchvision")
missing = [name for name in modules if importlib.util.find_spec(name) is None]

torch_cuda = "none"
torch_issue = "none"
mediapipe_solutions = "unknown"
try:
    import torch
    cuda_value = getattr(getattr(torch, "version", None), "cuda", None)
    if cuda_value:
        torch_cuda = str(cuda_value)
except Exception as exc:
    torch_issue = str(exc).replace("\n", " ")[:240]
    if "torch" not in missing:
        missing.append("torch")

try:
    import mediapipe
    if getattr(mediapipe, "solutions", None) is None:
        mediapipe_solutions = "missing"
    else:
        mediapipe_solutions = "ok"
except Exception:
    mediapipe_solutions = "import_failed"

pip_check_ok = True
pip_check_error = ""
try:
    check = subprocess.run(
        [sys.executable, "-m", "pip", "check"],
        capture_output=True,
        text=True,
        timeout=45,
    )
    pip_check_ok = check.returncode == 0
    if not pip_check_ok:
        pip_check_error = (check.stdout + "\n" + check.stderr).strip().replace("\n", " ")[:300]
except Exception as exc:
    pip_check_ok = False
    pip_check_error = str(exc).replace("\n", " ")[:300]

print("missing:" + ",".join(missing))
print(f"torch_cuda:{torch_cuda}")
print(f"torch_issue:{torch_issue}")
print(f"mediapipe_solutions:{mediapipe_solutions}")
print("pip_check:" + ("ok" if pip_check_ok else "fail"))
if pip_check_error:
    print("pip_check_error:" + pip_check_error)

if missing or not pip_check_ok or mediapipe_solutions != "ok":
    raise SystemExit(1)

print("ok")
EOF
)"
  status=$?
  set -e

  ENV_CHECK_MESSAGE="$(sanitize_value "${output}")"
  ENV_MISSING_MODULES="$(printf '%s\n' "${ENV_CHECK_MESSAGE}" | sed -n 's/^missing://p' | head -n1)"
  ENV_TORCH_CUDA="$(printf '%s\n' "${ENV_CHECK_MESSAGE}" | sed -n 's/^torch_cuda://p' | head -n1)"
  ENV_TORCH_ISSUE="$(printf '%s\n' "${ENV_CHECK_MESSAGE}" | sed -n 's/^torch_issue://p' | head -n1)"
  if printf '%s\n' "${ENV_CHECK_MESSAGE}" | grep -q '^pip_check:ok$'; then
    ENV_PIP_CHECK_OK=1
  else
    ENV_PIP_CHECK_OK=0
  fi

  return ${status}
}

install_torch_stack() {
  local active_python="$1"
  local selected_index_url="$2"
  local fallback_index_url="${TORCH_INDEX_URL_DEFAULT}"
  local install_attempt=1
  local max_attempts=3
  local index_url=""
  local -a index_urls=()

  index_urls+=("${selected_index_url}")
  if [[ "${selected_index_url}" != "${fallback_index_url}" ]]; then
    index_urls+=("${fallback_index_url}")
  fi

  for index_url in "${index_urls[@]}"; do
    install_attempt=1
    while [[ ${install_attempt} -le ${max_attempts} ]]; do
      local -a install_cmd=(
        "${active_python}"
        -m pip install
        --no-cache-dir
        --prefer-binary
        --upgrade
        --force-reinstall
        --retries 5
        --timeout 60
        --index-url "${index_url}"
        torch
        torchvision
      )

      log_info "Installing torch stack (attempt ${install_attempt}/${max_attempts})..."
      log_info "Using PyTorch index: ${index_url}"

      if env PIP_DISABLE_PIP_VERSION_CHECK=1 "${install_cmd[@]}"; then
        TORCH_INDEX_URL="${index_url}"
        return 0
      fi

      log_warn "Torch install attempt ${install_attempt} failed for ${index_url}."
      install_attempt=$((install_attempt + 1))
      if [[ ${install_attempt} -le ${max_attempts} ]]; then
        sleep 2
      fi
    done
  done

  return 1
}

install_requirements() {
  local active_python="$1"
  local install_attempt=1
  local max_attempts=3
  local attempt_index_url="${PIP_INDEX_URL}"
  local -a pip_args=(
    -m pip install
    --no-cache-dir
    --prefer-binary
    --retries 5
    --timeout 60
    -r "${ROOT_DIR}/requirements.txt"
  )
  local -a install_cmd=()

  while [[ ${install_attempt} -le ${max_attempts} ]]; do
    if [[ -z "${attempt_index_url}" && ${install_attempt} -ge 2 ]]; then
      attempt_index_url="https://pypi.org/simple"
    fi

    log_info "Installing Python dependencies (attempt ${install_attempt}/${max_attempts})..."
    if [[ -n "${attempt_index_url}" ]]; then
      log_info "Using pip index: ${attempt_index_url}"
    fi

    install_cmd=("${active_python}" "${pip_args[@]}")
    if [[ -n "${attempt_index_url}" ]]; then
      install_cmd+=(--index-url "${attempt_index_url}")
    fi

    if env PIP_DISABLE_PIP_VERSION_CHECK=1 "${install_cmd[@]}"; then
      return 0
    fi

    log_warn "Dependency installation attempt ${install_attempt} failed."
    install_attempt=$((install_attempt + 1))
    if [[ ${install_attempt} -le ${max_attempts} ]]; then
      sleep 2
    fi
  done

  return 1
}

ensure_requirements() {
  local active_python="${PYTHON_BIN}"
  local env_healthy=0
  local deps_need_update=0
  local should_install=0
  local should_install_torch=0
  local state_file=""
  local current_fingerprint=""
  local saved_fingerprint=""

  if [[ -x "${VENV_DIR}/bin/python" ]]; then
    active_python="${VENV_DIR}/bin/python"
  fi

  detect_cuda_runtime

  if check_python_environment "${active_python}"; then
    env_healthy=1
  else
    env_healthy=0
  fi

  state_file="$(dependency_state_file "${active_python}")"
  current_fingerprint="$(requirements_fingerprint "${active_python}")"
  saved_fingerprint="$(read_saved_dependency_fingerprint "${state_file}")"

  if [[ -n "${saved_fingerprint}" && "${saved_fingerprint}" != "${current_fingerprint}" ]]; then
    deps_need_update=1
  fi

  if [[ ${env_healthy} -eq 1 && -z "${saved_fingerprint}" ]]; then
    write_saved_dependency_fingerprint "${state_file}" "${current_fingerprint}"
  fi

  if [[ ${env_healthy} -eq 1 && ${deps_need_update} -eq 0 ]]; then
    if [[ -n "${ENV_TORCH_CUDA}" && "${ENV_TORCH_CUDA}" != "none" ]]; then
      log_info "Python deps are healthy (torch CUDA ${ENV_TORCH_CUDA}). Skipping install."
    else
      log_info "Python deps are healthy. Skipping install."
    fi
    PYTHON_BIN="${active_python}"
    return
  fi

  if [[ "${SKIP_INSTALL}" -eq 1 ]]; then
    if [[ ${env_healthy} -eq 1 && ${deps_need_update} -eq 1 ]]; then
      log_warn "Dependency requirements changed, but --skip-install was set."
    else
      log_warn "Python dependencies appear unhealthy and --skip-install was set."
      if [[ -n "${ENV_CHECK_MESSAGE}" ]]; then
        log_warn "Environment check: ${ENV_CHECK_MESSAGE}"
      fi
    fi
    PYTHON_BIN="${active_python}"
    return
  fi

  if [[ ${env_healthy} -eq 1 && ${deps_need_update} -eq 1 ]]; then
    log_info "Dependency fingerprint changed (requirements/python/index). Update is recommended."
    if prompt_yes_no "Update vision node Python dependencies now?" "y"; then
      should_install=1
    else
      PYTHON_BIN="${active_python}"
      return
    fi
  else
    should_install=1
    if [[ -n "${ENV_CHECK_MESSAGE}" ]]; then
      log_info "Python environment needs repair: ${ENV_CHECK_MESSAGE}"
    fi
    if ! prompt_yes_no "Install/repair vision node Python dependencies from requirements.txt?" "y"; then
      PYTHON_BIN="${active_python}"
      return
    fi
  fi

  if [[ ${should_install} -eq 1 ]]; then
    if [[ ${env_healthy} -eq 0 ]]; then
      if contains_csv_item "${ENV_MISSING_MODULES}" "torch" || contains_csv_item "${ENV_MISSING_MODULES}" "torchvision" || [[ -n "${ENV_TORCH_ISSUE}" && "${ENV_TORCH_ISSUE}" != "none" ]]; then
        should_install_torch=1
      fi
    elif [[ ${deps_need_update} -eq 1 ]]; then
      if contains_csv_item "${ENV_MISSING_MODULES}" "torch" || contains_csv_item "${ENV_MISSING_MODULES}" "torchvision"; then
        should_install_torch=1
      fi
    fi

    if [[ ${should_install_torch} -eq 1 ]]; then
      if ! install_torch_stack "${active_python}" "${TORCH_INDEX_URL}"; then
        log_error "Unable to install a working torch stack."
        log_error "You can retry later with:"
        log_error "  ${active_python} -m pip install --no-cache-dir --prefer-binary --upgrade --force-reinstall --index-url ${TORCH_INDEX_URL} torch torchvision"
        exit 1
      fi
    fi

    if ! install_requirements "${active_python}"; then
      if [[ "${NON_INTERACTIVE}" -eq 0 ]] && prompt_yes_no "Dependency install failed. Retry with a custom pip index URL?" "n"; then
        PIP_INDEX_URL="$(prompt_value "Enter pip index URL" "https://pypi.org/simple")"
        if ! install_requirements "${active_python}"; then
          log_error "Unable to install Python dependencies after retrying with ${PIP_INDEX_URL}."
          exit 1
        fi
      else
        log_error "Unable to install Python dependencies after multiple attempts."
        log_error "You can retry later with:"
        if [[ -n "${PIP_INDEX_URL}" ]]; then
          log_error "  ${active_python} -m pip install --no-cache-dir --prefer-binary --index-url ${PIP_INDEX_URL} -r ${ROOT_DIR}/requirements.txt"
        else
          log_error "  ${active_python} -m pip install --no-cache-dir --prefer-binary -r ${ROOT_DIR}/requirements.txt"
        fi
        exit 1
      fi
    fi

    if ! check_python_environment "${active_python}"; then
      log_error "Dependency installation completed but the environment is still unhealthy."
      if [[ -n "${ENV_CHECK_MESSAGE}" ]]; then
        log_error "Environment check: ${ENV_CHECK_MESSAGE}"
      fi
      exit 1
    fi

    current_fingerprint="$(requirements_fingerprint "${active_python}")"
    write_saved_dependency_fingerprint "${state_file}" "${current_fingerprint}"
  fi

  PYTHON_BIN="${active_python}"
}

normalize_debug_ui() {
  local raw="${1:-}"
  local normalized
  normalized="$(echo "${raw}" | tr '[:upper:]' '[:lower:]' | xargs)"

  case "${normalized}" in
    1|true|yes|y|on)
      DEBUG_UI=1
      ;;
    0|false|no|n|off|"")
      DEBUG_UI=0
      ;;
    *)
      log_warn "Unrecognized DEBUG_UI value '${raw}', defaulting to disabled."
      DEBUG_UI=0
      ;;
  esac
}

print_yolo_model_catalog() {
  cat <<'EOF'
[INFO] YOLO model catalog
  1) yolov8n.pt  - Nano (smallest, fastest; lowest accuracy, best for weak CPUs)
  2) yolov8s.pt  - Small (good speed/accuracy balance)
  3) yolov8m.pt  - Medium (higher accuracy; moderate CPU load)
  4) yolov8l.pt  - Large (high accuracy; heavier CPU usage)
  5) yolov8x.pt  - XLarge (best accuracy; slowest and most resource-heavy)
  0) Custom      - Enter your own local model path or model name
EOF
}

collect_model_selection() {
  if [[ -n "${MODEL_NAME}" ]]; then
    return
  fi

  if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
    MODEL_NAME="${DEFAULT_MODEL_NAME}"
    return
  fi

  print_yolo_model_catalog

  local choice=""
  while true; do
    choice="$(prompt_value "Choose YOLO model (0-5)" "1")"
    case "${choice}" in
      1)
        MODEL_NAME="yolov8n.pt"
        return
        ;;
      2)
        MODEL_NAME="yolov8s.pt"
        return
        ;;
      3)
        MODEL_NAME="yolov8m.pt"
        return
        ;;
      4)
        MODEL_NAME="yolov8l.pt"
        return
        ;;
      5)
        MODEL_NAME="yolov8x.pt"
        return
        ;;
      0)
        MODEL_NAME="$(prompt_value "Enter custom YOLO model path/name" "${DEFAULT_MODEL_NAME}")"
        if [[ -n "${MODEL_NAME}" ]]; then
          return
        fi
        ;;
      *)
        log_warn "Invalid choice '${choice}'. Please enter 0, 1, 2, 3, 4, or 5."
        ;;
    esac
  done
}

collect_runtime_choices() {
  if [[ -n "${DEBUG_UI}" ]]; then
    normalize_debug_ui "${DEBUG_UI}"
    return
  fi

  if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
    DEBUG_UI=0
    return
  fi

  if prompt_yes_no "Enable OpenCV debug UI window?" "n"; then
    DEBUG_UI=1
  else
    DEBUG_UI=0
  fi

  collect_model_selection
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --server-url)
        SERVER_URL="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --node-id)
        NODE_ID="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --camera-index)
        CAMERA_INDEX="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --fps)
        FPS="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --send-interval-ms)
        SEND_INTERVAL_MS="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --max-snapshots-per-second)
        MAX_SNAPSHOTS_PER_SECOND="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --yolo-every-n-frames)
        YOLO_EVERY_N_FRAMES="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --open-timeout-sec)
        OPEN_TIMEOUT_SEC="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --model-name)
        MODEL_NAME="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --objects-min-confidence)
        OBJECTS_MIN_CONFIDENCE="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --gestures-min-confidence)
        GESTURES_MIN_CONFIDENCE="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --delta-threshold)
        DELTA_THRESHOLD="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --pip-index-url)
        PIP_INDEX_URL="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --torch-index-url)
        TORCH_INDEX_URL="$(sanitize_value "${2:-}")"
        TORCH_INDEX_URL_USER_SET=1
        shift 2
        ;;
      --debug-ui)
        DEBUG_UI=1
        shift
        ;;
      --python)
        PYTHON_BIN="$(sanitize_value "${2:-}")"
        shift 2
        ;;
      --skip-install)
        SKIP_INSTALL=1
        shift
        ;;
      --non-interactive)
        NON_INTERACTIVE=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        log_error "Unknown option: $1"
        usage
        exit 1
        ;;
    esac
  done
}

collect_missing_inputs() {
  if [[ -z "${SERVER_URL}" ]]; then
    if [[ "${NON_INTERACTIVE}" -eq 1 ]]; then
      log_error "--server-url is required in non-interactive mode."
      exit 1
    fi
    SERVER_URL="$(prompt_value "Enter the WebSocket server URL from the main PC" "ws://MAIN-PC-IP:8765/vision")"
  fi
}

print_summary() {
  cat <<EOF
[INFO] Vision node launch configuration
  Python:                  ${PYTHON_BIN}
  Server URL:              ${SERVER_URL}
  Node ID:                 ${NODE_ID}
  Camera index:            ${CAMERA_INDEX}
  FPS:                     ${FPS}
  Send interval (ms):      ${SEND_INTERVAL_MS}
  Max snapshots/sec:       ${MAX_SNAPSHOTS_PER_SECOND}
  YOLO every N frames:     ${YOLO_EVERY_N_FRAMES}
  Open timeout (sec):      ${OPEN_TIMEOUT_SEC}
  Model:                   ${MODEL_NAME}
  Objects min confidence:  ${OBJECTS_MIN_CONFIDENCE}
  Gestures min confidence: ${GESTURES_MIN_CONFIDENCE}
  Delta threshold:         ${DELTA_THRESHOLD}
  Torch index URL:         ${TORCH_INDEX_URL}
  Debug UI:                $( [[ "${DEBUG_UI}" -eq 1 ]] && echo "enabled" || echo "disabled" )
EOF
}

main() {
  local -a run_cmd=()

  parse_args "$@"
  ensure_python
  ensure_virtualenv
  ensure_requirements
  collect_missing_inputs
  collect_runtime_choices
  if [[ -z "${MODEL_NAME}" ]]; then
    MODEL_NAME="${DEFAULT_MODEL_NAME}"
  fi
  print_summary

  cd "${ROOT_DIR}"
  run_cmd=(
    "${PYTHON_BIN}" "${ROOT_DIR}/main.py"
    --server-url "${SERVER_URL}"
    --node-id "${NODE_ID}"
    --camera-index "${CAMERA_INDEX}"
    --fps "${FPS}"
    --send-interval-ms "${SEND_INTERVAL_MS}"
    --max-snapshots-per-second "${MAX_SNAPSHOTS_PER_SECOND}"
    --yolo-every-n-frames "${YOLO_EVERY_N_FRAMES}"
    --open-timeout-sec "${OPEN_TIMEOUT_SEC}"
    --model-name "${MODEL_NAME}"
    --objects-min-confidence "${OBJECTS_MIN_CONFIDENCE}"
    --gestures-min-confidence "${GESTURES_MIN_CONFIDENCE}"
    --delta-threshold "${DELTA_THRESHOLD}"
  )

  if [[ "${DEBUG_UI}" -eq 1 ]]; then
    run_cmd+=(--debug-ui)
  fi

  exec "${run_cmd[@]}"
}

main "$@"
