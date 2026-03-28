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
AUTO_INSTALL_SPEEXDSP=1
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

linux_whisper_names() {
  cat <<'EOF'
whisper-cli
whisper
main
EOF
}

linux_piper_names() {
  cat <<'EOF'
piper
EOF
}

has_any_executable() {
  local candidate
  for candidate in "$@"; do
    [[ -z "${candidate}" ]] && continue
    if need_cmd "${candidate}"; then
      return 0
    fi
  done
  return 1
}

whisper_local_bin_path() {
  echo "${HOME}/.local/share/jarvis/tools/whisper/bin/whisper-cli"
}

ensure_whisper_on_path_from_local_install() {
  local local_whisper
  local local_whisper_dir

  local_whisper="$(whisper_local_bin_path)"
  if [[ -x "${local_whisper}" ]]; then
    local_whisper_dir="$(dirname "${local_whisper}")"
    export PATH="${local_whisper_dir}:${PATH}"
    log_info "whisper.cpp already installed at ${local_whisper}; skipping reinstall."
    return 0
  fi

  return 1
}

first_available_package() {
  local manager="$1"
  shift

  local package
  for package in "$@"; do
    [[ -z "${package}" ]] && continue
    case "${manager}" in
      apt)
        if apt-cache show "${package}" >/dev/null 2>&1; then
          echo "${package}"
          return 0
        fi
        ;;
      dnf)
        if dnf list "${package}" >/dev/null 2>&1; then
          echo "${package}"
          return 0
        fi
        ;;
      pacman)
        if pacman -Si "${package}" >/dev/null 2>&1; then
          echo "${package}"
          return 0
        fi
        ;;
      zypper)
        if zypper --non-interactive info "${package}" >/dev/null 2>&1; then
          echo "${package}"
          return 0
        fi
        ;;
    esac
  done

  return 1
}

install_package_by_manager() {
  local manager="$1"
  local package="$2"

  case "${manager}" in
    apt)
      run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${package}"
      ;;
    dnf)
      run_as_root dnf install -y "${package}"
      ;;
    pacman)
      run_as_root pacman -Sy --needed --noconfirm "${package}"
      ;;
    zypper)
      run_as_root zypper --non-interactive install -y "${package}"
      ;;
    *)
      return 1
      ;;
  esac
}

ensure_linux_voice_tool() {
  local manager="$1"
  local label="$2"
  shift 2

  local separator_idx=-1
  local idx=0
  local item
  for item in "$@"; do
    if [[ "${item}" == "--" ]]; then
      separator_idx=${idx}
      break
    fi
    idx=$((idx + 1))
  done

  if [[ ${separator_idx} -lt 1 || ${separator_idx} -ge $# ]]; then
    log_warn "Internal bootstrap configuration error for ${label}."
    return
  fi

  local -a exe_names=("${@:1:${separator_idx}}")
  local -a package_candidates=("${@:$((separator_idx + 2))}")

  if has_any_executable "${exe_names[@]}"; then
    log_info "${label} executable already available."
    return
  fi

  local package=""
  if package="$(first_available_package "${manager}" "${package_candidates[@]}")"; then
    log_info "Installing ${label} via package '${package}'..."
    install_package_by_manager "${manager}" "${package}"
  else
    log_warn "No known ${label} package was found for ${manager}."
    return
  fi

  if has_any_executable "${exe_names[@]}"; then
    log_info "${label} executable is now available."
  else
    log_warn "${label} package installed but executable was not found on PATH."
  fi
}

ensure_linux_whisper_source_build() {
  if ensure_whisper_on_path_from_local_install; then
    return
  fi

  if has_any_executable whisper-cli whisper main; then
    return
  fi

  if ! need_cmd cmake; then
    log_warn "Skipping whisper.cpp source bootstrap because cmake is unavailable."
    return
  fi

  if ! need_cmd make && ! need_cmd ninja; then
    log_warn "Skipping whisper.cpp source bootstrap because no build tool (make/ninja) is available."
    return
  fi

  local whisper_version="v1.8.4"
  local whisper_tools_root="${HOME}/.local/share/jarvis/tools/whisper"
  local whisper_src_root="${whisper_tools_root}/src"
  local whisper_archive="${whisper_tools_root}/whisper-${whisper_version}.tar.gz"
  local whisper_build_dir=""
  local whisper_src_dir=""
  local whisper_bin=""

  mkdir -p "${whisper_tools_root}" "${whisper_src_root}"

  log_info "Bootstrapping whisper.cpp from source (${whisper_version})..."
  curl -fL "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${whisper_version}.tar.gz" -o "${whisper_archive}"
  rm -rf "${whisper_src_root:?}"/*
  tar -xf "${whisper_archive}" -C "${whisper_src_root}"

  whisper_src_dir="$(find "${whisper_src_root}" -maxdepth 2 -type f -name CMakeLists.txt -printf '%h\n' | head -n1)"
  if [[ -z "${whisper_src_dir}" ]]; then
    log_warn "whisper.cpp source extracted, but CMakeLists.txt was not found."
    return
  fi

  whisper_build_dir="${whisper_src_dir}/build-jarvis"
  cmake -S "${whisper_src_dir}" -B "${whisper_build_dir}" -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=ON

  if ! cmake --build "${whisper_build_dir}" --parallel --target whisper-cli; then
    if ! cmake --build "${whisper_build_dir}" --parallel --target main; then
      if ! cmake --build "${whisper_build_dir}" --parallel; then
        log_warn "whisper.cpp source build failed."
        return
      fi
    fi
  fi

  whisper_bin="$(find "${whisper_build_dir}" -type f \( -name whisper-cli -o -name main \) | head -n1)"
  if [[ -z "${whisper_bin}" ]]; then
    log_warn "whisper.cpp built but no executable (whisper-cli/main) was found."
    return
  fi

  mkdir -p "${whisper_tools_root}/bin"
  install -m 0755 "${whisper_bin}" "${whisper_tools_root}/bin/whisper-cli"
  export PATH="${whisper_tools_root}/bin:${PATH}"
  log_info "whisper.cpp installed at ${whisper_tools_root}/bin/whisper-cli"
}

first_match_under_dir() {
  local root="$1"
  local pattern="$2"
  find "${root}" -type f -name "${pattern}" 2>/dev/null | head -n1
}

ensure_linux_sherpa_wake_assets() {
  local sherpa_root="${ROOT}/third_party/sherpa-onnx"
  local sherpa_archive="${sherpa_root}/sherpa-onnx-v1.12.33-linux-x64-shared.tar.bz2"
  local sherpa_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.33/sherpa-onnx-v1.12.33-linux-x64-shared.tar.bz2"

  local wake_model_root="${ROOT}/third_party/sherpa-kws-model"
  local wake_model_archive="${wake_model_root}/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2"
  local wake_model_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2"

  mkdir -p "${sherpa_root}" "${wake_model_root}"

  local sherpa_header
  sherpa_header="$(first_match_under_dir "${sherpa_root}" "cxx-api.h")"
  if [[ -z "${sherpa_header}" ]]; then
    if ! need_cmd curl; then
      log_warn "Skipping sherpa-onnx runtime bootstrap because curl is unavailable."
    elif ! need_cmd tar; then
      log_warn "Skipping sherpa-onnx runtime bootstrap because tar is unavailable."
    else
      log_info "Bootstrapping sherpa-onnx runtime package for wake-word support..."
      curl -fL "${sherpa_url}" -o "${sherpa_archive}"
      tar -xf "${sherpa_archive}" -C "${sherpa_root}"
      sherpa_header="$(first_match_under_dir "${sherpa_root}" "cxx-api.h")"
      if [[ -n "${sherpa_header}" ]]; then
        log_info "sherpa-onnx runtime ready: ${sherpa_header}"
      else
        log_warn "sherpa-onnx runtime extracted, but cxx-api.h was not found."
      fi
    fi
  else
    log_info "sherpa-onnx runtime already available."
  fi

  local wake_encoder
  wake_encoder="$(first_match_under_dir "${wake_model_root}" "encoder-*.onnx")"
  if [[ -z "${wake_encoder}" ]]; then
    if ! need_cmd curl; then
      log_warn "Skipping sherpa wake model bootstrap because curl is unavailable."
    elif ! need_cmd tar; then
      log_warn "Skipping sherpa wake model bootstrap because tar is unavailable."
    else
      log_info "Bootstrapping sherpa wake model package..."
      curl -fL "${wake_model_url}" -o "${wake_model_archive}"
      tar -xf "${wake_model_archive}" -C "${wake_model_root}"
      wake_encoder="$(first_match_under_dir "${wake_model_root}" "encoder-*.onnx")"
      if [[ -n "${wake_encoder}" ]]; then
        log_info "sherpa wake model ready: ${wake_encoder}"
      else
        log_warn "sherpa wake model extracted, but encoder ONNX file was not found."
      fi
    fi
  else
    log_info "sherpa wake model already available."
  fi
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
        libxkbcommon-x11-0 \
        libxcb-cursor0 \
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
  [[ -f "${rnnoise_root}/include/rnnoise.h" && -f "${rnnoise_root}/src/denoise.c" ]]
}

rnnoise_model_data_ready() {
  local rnnoise_root="$1"
  [[ -f "${rnnoise_root}/src/rnnoise_data.h" && -f "${rnnoise_root}/src/rnnoise_data.c" ]]
}

normalize_rnnoise_model_data() {
  local rnnoise_root="$1"

  if [[ -f "${rnnoise_root}/src/rnnoise_data.h" && -f "${rnnoise_root}/src/rnnoise_data.c" ]]; then
    return
  fi

  # Some rnnoise model archives may extract data files at repository root.
  if [[ -f "${rnnoise_root}/rnnoise_data.h" && -f "${rnnoise_root}/rnnoise_data.c" ]]; then
    mv -f "${rnnoise_root}/rnnoise_data.h" "${rnnoise_root}/src/rnnoise_data.h"
    mv -f "${rnnoise_root}/rnnoise_data.c" "${rnnoise_root}/src/rnnoise_data.c"
  fi

  # Recover from previous buggy runs where download_model.sh executed from repo root.
  if [[ -f "${ROOT}/rnnoise_data.h" && -f "${ROOT}/rnnoise_data.c" ]]; then
    mv -f "${ROOT}/rnnoise_data.h" "${rnnoise_root}/src/rnnoise_data.h"
    mv -f "${ROOT}/rnnoise_data.c" "${rnnoise_root}/src/rnnoise_data.c"
  fi
}

ensure_rnnoise_model_data() {
  local rnnoise_root="$1"

  normalize_rnnoise_model_data "${rnnoise_root}"

  if rnnoise_model_data_ready "${rnnoise_root}"; then
    return
  fi

  if [[ ! -f "${rnnoise_root}/download_model.sh" ]]; then
    log_error "RNNoise model data is missing and download_model.sh is not available."
    exit 1
  fi

  if ! need_cmd tar; then
    log_error "tar is required to extract RNNoise model data archives."
    exit 1
  fi

  if ! need_cmd wget && ! need_cmd curl; then
    log_error "Either wget or curl is required to fetch RNNoise model data."
    exit 1
  fi

  if ! need_cmd wget && need_cmd curl; then
    log_info "wget not found; using curl wrapper for RNNoise model download."
    local curl_wrapper="${rnnoise_root}/wget"
    cat >"${curl_wrapper}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
url="${1:-}"
if [[ -z "${url}" ]]; then
  echo "wget shim requires a URL" >&2
  exit 1
fi
outfile="$(basename "${url}")"
curl -fL --retry 3 --connect-timeout 15 -o "${outfile}" "${url}"
EOF
    chmod +x "${curl_wrapper}"
    (
      cd "${rnnoise_root}"
      PATH="${rnnoise_root}:$PATH" sh ./download_model.sh
    )
    rm -f "${curl_wrapper}"
  else
    (cd "${rnnoise_root}" && sh ./download_model.sh)
  fi

  normalize_rnnoise_model_data "${rnnoise_root}"

  if ! rnnoise_model_data_ready "${rnnoise_root}"; then
    log_error "RNNoise model bootstrap ran, but rnnoise_data.h/.c are still missing."
    exit 1
  fi
}

ensure_rnnoise_source() {
  if [[ "${AUTO_INSTALL_RNNOISE}" -eq 0 ]]; then
    log_info "Skipping RNNoise source bootstrap (nornnoise)."
    return
  fi

  local rnnoise_root
  rnnoise_root="$(rnnoise_root_dir)"

  if rnnoise_source_ready "${rnnoise_root}"; then
    ensure_rnnoise_model_data "${rnnoise_root}"
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
    log_error "Expected: ${rnnoise_root}/include/rnnoise.h and ${rnnoise_root}/src/denoise.c"
    exit 1
  fi

  ensure_rnnoise_model_data "${rnnoise_root}"

  log_info "RNNoise source ready at ${rnnoise_root}"
}

speexdsp_root_dir() {
  echo "${JARVIS_SPEEXDSP_ROOT:-${ROOT}/third_party/speexdsp}"
}

speexdsp_source_ready() {
  local speex_root="$1"
  [[ -f "${speex_root}/include/speex/speex_echo.h" && -f "${speex_root}/libspeexdsp/mdf.c" ]]
}

ensure_speexdsp_source() {
  if [[ "${AUTO_INSTALL_SPEEXDSP}" -eq 0 ]]; then
    log_info "Skipping SpeexDSP source bootstrap (nospeex)."
    return
  fi

  local speex_root
  speex_root="$(speexdsp_root_dir)"

  if speexdsp_source_ready "${speex_root}"; then
    log_info "SpeexDSP source ready at ${speex_root}"
    return
  fi

  if [[ -d "${speex_root}" && ! -d "${speex_root}/.git" ]]; then
    log_error "SpeexDSP directory exists but is incomplete: ${speex_root}"
    log_error "Remove it manually, then rerun ./build.sh so SpeexDSP can be re-downloaded."
    exit 1
  fi

  if ! need_cmd git; then
    log_error "git is required to bootstrap SpeexDSP source."
    exit 1
  fi

  mkdir -p "$(dirname "${speex_root}")"

  if [[ -d "${speex_root}/.git" ]]; then
    log_info "Updating SpeexDSP source in ${speex_root}"
    git -C "${speex_root}" fetch --depth 1 origin
    git -C "${speex_root}" reset --hard FETCH_HEAD
  else
    log_info "Cloning SpeexDSP source into ${speex_root}"
    git clone --depth 1 https://github.com/xiph/speexdsp "${speex_root}"
  fi

  if ! speexdsp_source_ready "${speex_root}"; then
    log_error "SpeexDSP source bootstrap completed but required files are still missing."
    log_error "Expected: ${speex_root}/include/speex/speex_echo.h and ${speex_root}/libspeexdsp/mdf.c"
    exit 1
  fi

  log_info "SpeexDSP source ready at ${speex_root}"
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

  # Auto-bootstrap optional voice executables when missing. Never reinstalls if already present.
  ensure_whisper_on_path_from_local_install || true

  ensure_linux_voice_tool "${manager}" "whisper.cpp" \
    whisper-cli whisper main -- \
    whisper.cpp whisper-cpp
  ensure_linux_whisper_source_build

  ensure_linux_voice_tool "${manager}" "piper" \
    piper -- \
    piper-tts piper

  ensure_linux_sherpa_wake_assets
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
    nospeex)
      AUTO_INSTALL_SPEEXDSP=0
      ;;
    *)
      log_error "Unknown argument: ${arg}"
      echo "Usage: ./build.sh [debug|release] [clean] [notest] [nodeps] [bootstrap] [noqt] [nornnoise] [nospeex]" >&2
      exit 1
      ;;
  esac
done

ensure_dependencies
ensure_qt_kit
ensure_rnnoise_source
ensure_speexdsp_source

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
