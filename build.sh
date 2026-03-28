#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Release"
CONFIGURE_PRESET="linux-release"
BUILD_PRESET="linux-release"
BUILD_DIR="${ROOT}/build-linux-release"
RUN_TESTS=1
FRESH=0
AUTO_INSTALL_DEPS=1
INSTALL_ONLY=0
AUTO_INSTALL_QT=1
AUTO_INSTALL_RNNOISE=1
REQUIRED_QT_VERSION="6.6.0"
DEFAULT_AQT_QT_VERSION="6.6.3"

required_qt_cmake_component_files() {
  cat <<'EOF'
Qt6Multimedia/Qt6MultimediaConfig.cmake
Qt6WebSockets/Qt6WebSocketsConfig.cmake
Qt6ShaderToolsTools/Qt6ShaderToolsToolsConfig.cmake
Qt6Svg/Qt6SvgConfig.cmake
Qt6QuickControls2/Qt6QuickControls2Config.cmake
EOF
}

log_info() {
  echo "[INFO] $*"
}

log_warn() {
  echo "[WARN] $*"
}

log_error() {
  echo "[ERROR] $*" >&2
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

version_ge() {
  local left="$1"
  local right="$2"
  [[ "$(printf '%s\n%s\n' "${left}" "${right}" | sort -V | tail -n1)" == "${left}" ]]
}

prepend_path_var() {
  local value="$1"
  local current="${2:-}"
  if [[ -z "${current}" ]]; then
    echo "${value}"
  else
    echo "${value}:${current}"
  fi
}

run_as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
    return
  fi

  if need_cmd sudo; then
    sudo "$@"
    return
  fi

  log_error "This step requires root privileges. Re-run as root or install sudo."
  exit 1
}

detect_package_manager() {
  if need_cmd apt-get; then
    echo "apt"
    return
  fi

  if need_cmd dnf; then
    echo "dnf"
    return
  fi

  if need_cmd pacman; then
    echo "pacman"
    return
  fi

  if need_cmd zypper; then
    echo "zypper"
    return
  fi

  echo ""
}

install_linux_dependencies() {
  local manager="$1"

  log_info "Installing Linux build dependencies via ${manager}..."
  case "${manager}" in
    apt)
      run_as_root apt-get update
      run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        ffmpeg \
        git \
        ninja-build \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        qt6-base-dev \
        qt6-base-dev-tools \
        qt6-declarative-dev \
        qt6-declarative-dev-tools \
        qt6-shadertools-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        qt6-multimedia-dev \
        qt6-svg-dev \
        qt6-websockets-dev
      ;;
    dnf)
      run_as_root dnf install -y \
        ca-certificates \
        cmake \
        curl \
        ffmpeg \
        gcc-c++ \
        git \
        ninja-build \
        pkgconf-pkg-config \
        python3 \
        python3-pip \
        qt6-qtbase-devel \
        qt6-qtdeclarative-devel \
        qt6-qtshadertools-devel \
        qt6-qttools-devel \
        qt6-qtmultimedia-devel \
        qt6-qtsvg-devel \
        qt6-qtwebsockets-devel
      ;;
    pacman)
      run_as_root pacman -Sy --needed --noconfirm \
        base-devel \
        ca-certificates \
        cmake \
        curl \
        ffmpeg \
        git \
        ninja \
        pkgconf \
        python \
        python-pip \
        qt6-base \
        qt6-declarative \
        qt6-multimedia \
        qt6-shadertools \
        qt6-svg \
        qt6-tools \
        qt6-websockets
      ;;
    zypper)
      run_as_root zypper --non-interactive install -y \
        ca-certificates \
        cmake \
        curl \
        ffmpeg \
        gcc-c++ \
        git \
        libqt6-qtbase-devel \
        libqt6-qtdeclarative-devel \
        libqt6-qtmultimedia-devel \
        libqt6-qtsvg-devel \
        libqt6-qtwebsockets-devel \
        ninja \
        pkgconf-pkg-config \
        python3 \
        python3-pip \
        qt6-shadertools-devel \
        qt6-tools-devel
      ;;
    *)
      log_error "Unsupported package manager: ${manager}"
      exit 1
      ;;
  esac
}

get_system_qt_version() {
  if need_cmd pkg-config && pkg-config --exists Qt6Core; then
    pkg-config --modversion Qt6Core
    return
  fi

  if need_cmd qmake6; then
    qmake6 -query QT_VERSION
    return
  fi

  echo ""
}

configure_qt_env_from_dir() {
  local qt_dir="$1"
  if [[ ! -f "${qt_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    log_error "Qt directory does not contain Qt6 CMake package: ${qt_dir}"
    exit 1
  fi

  export QT_DIR="${qt_dir}"
  export CMAKE_PREFIX_PATH="$(prepend_path_var "${QT_DIR}" "${CMAKE_PREFIX_PATH:-}")"
  log_info "Using Qt from ${QT_DIR}"
}

qt_has_required_components() {
  local qt_dir="$1"
  local rel
  local missing=0

  while IFS= read -r rel; do
    [[ -z "${rel}" ]] && continue
    if [[ ! -f "${qt_dir}/lib/cmake/${rel}" ]]; then
      log_warn "Missing Qt CMake component file: ${qt_dir}/lib/cmake/${rel}"
      missing=1
    fi
  done < <(required_qt_cmake_component_files)

  [[ ${missing} -eq 0 ]]
}

aqt_install_required_modules() {
  local aqt_python="$1"
  local qt_version="$2"
  local qt_root="$3"

  # Core modules (qtbase/qtdeclarative/qtsvg/qttools) are part of the base desktop kit.
  # Install optional modules that this project needs and that may be missing.
  local optional_modules=(
    qtmultimedia
    qtwebsockets
    qtshadertools
  )

  local module
  for module in "${optional_modules[@]}"; do
    log_info "Ensuring Qt module '${module}' is installed for Qt ${qt_version}"
    if "${aqt_python}" -m aqt install-qt linux desktop "${qt_version}" gcc_64 --modules "${module}" -O "${qt_root}"; then
      :
    else
      log_warn "aqt could not install optional module '${module}'."
      log_warn "Available modules can be listed with: ${aqt_python} -m aqt list-qt linux desktop --modules ${qt_version} gcc_64"
    fi
  done
}

install_qt_with_aqt() {
  local qt_version="${JARVIS_QT_VERSION:-${DEFAULT_AQT_QT_VERSION}}"
  local qt_root="${JARVIS_QT_ROOT:-${HOME}/Qt}"
  local qt_dir="${qt_root}/${qt_version}/gcc_64"
  local aqt_venv_dir="${JARVIS_AQT_VENV_DIR:-${HOME}/.cache/jarvis/aqt-venv}"
  local aqt_python="${aqt_venv_dir}/bin/python"

  if [[ -f "${qt_dir}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    if qt_has_required_components "${qt_dir}"; then
      log_info "Qt ${qt_version} already present at ${qt_dir}"
      configure_qt_env_from_dir "${qt_dir}"
      return
    fi
    log_warn "Qt ${qt_version} exists but is missing required modules; attempting to install them."
  fi

  if ! need_cmd python3; then
    log_error "python3 is required to install Qt via aqt."
    exit 1
  fi

  if [[ ! -x "${aqt_python}" ]]; then
    log_info "Creating Python virtual environment for aqt at ${aqt_venv_dir}"
    python3 -m venv "${aqt_venv_dir}"
  fi

  log_info "Installing/updating Qt ${qt_version} (gcc_64) via aqt into ${qt_root}"
  "${aqt_python}" -m pip install --upgrade pip aqtinstall

  aqt_install_required_modules "${aqt_python}" "${qt_version}" "${qt_root}"

  if ! qt_has_required_components "${qt_dir}"; then
    log_error "Qt ${qt_version} installation finished but required Qt modules are still missing."
    log_error "Try forcing a clean Qt install by removing ${qt_dir} and re-running ./build.sh."
    exit 1
  fi

  configure_qt_env_from_dir "${qt_dir}"
}

ensure_qt_kit() {
  if [[ "${AUTO_INSTALL_QT}" -eq 0 ]]; then
    log_info "Skipping Qt auto-install/version checks (noqt)."
    return
  fi

  if [[ -n "${QT_DIR:-}" ]]; then
    configure_qt_env_from_dir "${QT_DIR}"
    return
  fi

  local system_qt
  system_qt="$(get_system_qt_version)"
  if [[ -n "${system_qt}" ]] && version_ge "${system_qt}" "${REQUIRED_QT_VERSION}"; then
    log_info "Detected system Qt ${system_qt} (meets >= ${REQUIRED_QT_VERSION})."
    return
  fi

  if [[ -n "${system_qt}" ]]; then
    log_warn "Detected system Qt ${system_qt}, but project requires >= ${REQUIRED_QT_VERSION}."
  else
    log_warn "No usable Qt6 kit detected."
  fi

  install_qt_with_aqt
}

rnnoise_root_dir() {
  echo "${JARVIS_RNNOISE_ROOT:-${ROOT}/third_party/rnnoise/rnnoise-main}"
}

rnnoise_source_ready() {
  local rnnoise_root="$1"
  [[ -f "${rnnoise_root}/include/rnnoise.h" && -f "${rnnoise_root}/src/rnnoise_data.c" ]]
}

ensure_rnnoise_source() {
  if [[ "${AUTO_INSTALL_RNNOISE}" -eq 0 ]]; then
    log_info "Skipping RNNoise source bootstrap (nornnoise)."
    return
  fi

  local rnnoise_root
  rnnoise_root="$(rnnoise_root_dir)"

  if rnnoise_source_ready "${rnnoise_root}"; then
    log_info "RNNoise source ready at ${rnnoise_root}"
    return
  fi

  if [[ -d "${rnnoise_root}" && ! -d "${rnnoise_root}/.git" ]]; then
    log_error "RNNoise directory exists but is incomplete: ${rnnoise_root}"
    log_error "Remove it manually, then rerun ./build.sh so RNNoise can be re-downloaded."
    exit 1
  fi

  if ! need_cmd git; then
    log_error "git is required to bootstrap RNNoise source."
    exit 1
  fi

  mkdir -p "$(dirname "${rnnoise_root}")"

  if [[ -d "${rnnoise_root}/.git" ]]; then
    log_info "Updating RNNoise source in ${rnnoise_root}"
    git -C "${rnnoise_root}" fetch --depth 1 origin
    git -C "${rnnoise_root}" reset --hard FETCH_HEAD
  else
    log_info "Cloning RNNoise source into ${rnnoise_root}"
    git clone --depth 1 https://github.com/xiph/rnnoise "${rnnoise_root}"
  fi

  if ! rnnoise_source_ready "${rnnoise_root}"; then
    log_error "RNNoise source bootstrap completed but required files are still missing."
    exit 1
  fi

  log_info "RNNoise source ready at ${rnnoise_root}"
}

ensure_dependencies() {
  if [[ "${AUTO_INSTALL_DEPS}" -eq 0 ]]; then
    log_info "Skipping dependency installation (nodeps)."
    return
  fi

  local manager
  manager="$(detect_package_manager)"
  if [[ -z "${manager}" ]]; then
    log_warn "No supported package manager found (apt/dnf/pacman/zypper)."
    log_warn "Install CMake, Ninja, a C++ toolchain, pkg-config, and Qt 6 dev packages manually."
    return
  fi

  install_linux_dependencies "${manager}"
}

for arg in "$@"; do
  case "${arg}" in
    debug)
      BUILD_TYPE="Debug"
      CONFIGURE_PRESET="linux-debug"
      BUILD_PRESET="linux-debug"
      BUILD_DIR="${ROOT}/build-linux-debug"
      ;;
    release)
      BUILD_TYPE="Release"
      CONFIGURE_PRESET="linux-release"
      BUILD_PRESET="linux-release"
      BUILD_DIR="${ROOT}/build-linux-release"
      ;;
    clean)
      FRESH=1
      ;;
    notest)
      RUN_TESTS=0
      ;;
    nodeps)
      AUTO_INSTALL_DEPS=0
      ;;
    bootstrap)
      INSTALL_ONLY=1
      ;;
    noqt)
      AUTO_INSTALL_QT=0
      ;;
    nornnoise)
      AUTO_INSTALL_RNNOISE=0
      ;;
    *)
      log_error "Unknown argument: ${arg}"
      echo "Usage: ./build.sh [debug|release] [clean] [notest] [nodeps] [bootstrap] [noqt] [nornnoise]" >&2
      exit 1
      ;;
  esac
done

ensure_dependencies
ensure_qt_kit
ensure_rnnoise_source

if [[ ${INSTALL_ONLY} -eq 1 ]]; then
  echo
  echo "[OK] Dependency bootstrap complete."
  exit 0
fi

if [[ ${FRESH} -eq 1 ]]; then
  log_info "Removing previous build directory: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

if [[ -z "${QT_DIR:-}" && -z "${CMAKE_PREFIX_PATH:-}" ]]; then
  log_warn "QT_DIR/CMAKE_PREFIX_PATH is not set. CMake must still be able to find your Qt 6 kit."
fi

echo "[INFO] Root:       ${ROOT}"
echo "[INFO] Build type: ${BUILD_TYPE}"
echo "[INFO] Build dir:  ${BUILD_DIR}"
echo "[INFO] Preset:     ${CONFIGURE_PRESET}"

cmake --preset "${CONFIGURE_PRESET}"
cmake --build --preset "${BUILD_PRESET}" --parallel

if [[ ${RUN_TESTS} -eq 1 ]]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

echo
echo "[OK] Build complete."
echo "[OK] Executable: ${ROOT}/bin/jarvis"
echo "[OK] Logs:       ${ROOT}/bin/logs"
