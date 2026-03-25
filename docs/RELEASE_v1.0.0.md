# Release v1.0.0

Release date: 2026-03-25

## Summary

v1.0.0 establishes a stable local-first desktop assistant baseline with:

- Guided first-run setup
- Runtime settings control surface
- Local wake/STT/TTS workflow
- OpenAI-compatible backend integration
- Improved wake stability and observability
- Prompt and command extraction hardening
- Per-exchange AI logging

## Included Improvements

### Product and UX

- Setup and settings UX refinements for reliability and clarity
- Persistent wake detection tuning from UI interactions
- Better runtime settings visibility and synchronization

### AI and Prompting

- Stronger prompt contracts for concise, spoken-safe answers
- Better uncertainty behavior and extraction constraints
- Improved OpenAI-compatible payload support and streaming parsing

### Audio and Wake

- Mycroft Precise wake stabilization using moving-average and frame consistency checks
- Tunable threshold and cooldown with practical defaults
- Expanded wake probability/detection logs

### Identity and Voice

- Username-based identity across UI and spoken responses

### Logging

- Prompt/response exchanges written one-per-file in logs/AI

## Verification Snapshot

- QML diagnostics for setup and settings changes: clean
- Build and test command: build.bat

## Versioning

- CMake project version: 1.0.0
- vcpkg manifest version-string: 1.0.0

## Suggested Release Tag

- v1.0.0

## Notes

If publishing to GitHub Releases, use CHANGELOG.md section 1.0.0 as the base release notes body.
