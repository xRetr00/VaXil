#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-linux-release"
APPDIR="${ROOT}/dist/AppDir"
LINUXDEPLOYQT_BIN="${LINUXDEPLOYQT_BIN:-linuxdeployqt}"
DESKTOP_FILE="${APPDIR}/usr/share/applications/vaxil.desktop"

cmake --preset linux-release
cmake --build --preset linux-release --parallel

if ! command -v "${LINUXDEPLOYQT_BIN}" >/dev/null 2>&1; then
  echo "[ERROR] linuxdeployqt was not found. Set LINUXDEPLOYQT_BIN or add it to PATH." >&2
  exit 1
fi

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/512x512/apps"
cp "${ROOT}/bin/jarvis" "${APPDIR}/usr/bin/"
cp "${ROOT}/bin/jarvis_sherpa_wake_helper" "${APPDIR}/usr/bin/" || true
cp "${ROOT}/assets/logo.png" "${APPDIR}/usr/share/icons/hicolor/512x512/apps/vaxil.png"

cat > "${DESKTOP_FILE}" <<'EOF'
[Desktop Entry]
Type=Application
Name=Vaxil
Comment=Local-first desktop AI assistant
Exec=jarvis
Icon=vaxil
Categories=Utility;AudioVideo;
Terminal=false
StartupNotify=true
EOF

"${LINUXDEPLOYQT_BIN}" \
  "${DESKTOP_FILE}" \
  -qmldir="${ROOT}/src/gui/qml" \
  -appimage

echo "[OK] AppImage packaging completed."
