# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2026-03-25

### Added

- Username-only identity model used across UI and voice responses.
- Setup and Settings support for username editing and wake tuning controls.
- Per-exchange AI logging to separate files in bin/logs/AI.
- Enhanced setup requirement checks for endpoint, models, and local voice/wake toolchain.

### Changed

- Prompt contracts strengthened for concise, spoken-safe responses and safer command extraction output.
- LM Studio OpenAI-compatible client hardened:
  - Endpoint normalization for /v1 variations
  - More robust SSE parsing
  - Improved JSON error extraction
  - Optional request parameters (top_p, max_tokens, seed)
- Wake word engine tuning improved with moving-average stability checks, consecutive-frame gating, and richer detection logs.
- Wake threshold defaults and clamp range updated to support 0.30 baseline.
- Setup wizard and settings pages updated so wake tuning persists immediately after user interaction.
- Setup wizard layout updated so navigation actions remain visible even with long content.
- Settings window field sync improved so runtime state is reloaded when opened.

### Fixed

- Prevented stale Settings UI values by syncing controls from backend state on window open.
- Prevented Setup navigation controls from being pushed off-screen by long step content.

### Build and Packaging

- Project semantic version updated to 1.0.0 in CMake and vcpkg manifest.
