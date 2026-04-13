from __future__ import annotations

import argparse
import sys
from pathlib import Path


DEFAULT_EXTENSIONS = {".cpp", ".h", ".qml", ".ts", ".tsx", ".py"}
DEFAULT_INCLUDE_PREFIXES = (
    "src/",
    "tests/",
    "scripts/",
    "config/",
    "vision_node/",
    "python_runtime/vaxil_runtime/",
)


def load_allowlist(path: Path) -> set[str]:
    if not path.exists():
        return set()
    return {
        line.strip().replace("\\", "/")
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.strip().startswith("#")
    }


def iter_candidate_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in DEFAULT_EXTENSIONS:
            continue
        relative = path.relative_to(root).as_posix()
        if not relative.startswith(DEFAULT_INCLUDE_PREFIXES):
            continue
        files.append(path)
    return files


def line_count(path: Path) -> int:
    return path.read_text(encoding="utf-8", errors="ignore").count("\n") + 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when authored files exceed the maximum line count.")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--allowlist", type=Path, required=True)
    parser.add_argument("--max-lines", type=int, default=500)
    args = parser.parse_args()

    root = args.root.resolve()
    allowlist = load_allowlist(args.allowlist.resolve())
    failures: list[tuple[str, int]] = []

    for path in iter_candidate_files(root):
        relative = path.relative_to(root).as_posix()
        if relative in allowlist:
            continue
        count = line_count(path)
        if count > args.max_lines:
            failures.append((relative, count))

    if not failures:
        print(f"file-cap check passed: no non-allowlisted files exceed {args.max_lines} lines")
        return 0

    print(f"file-cap check failed: {len(failures)} file(s) exceed {args.max_lines} lines", file=sys.stderr)
    for relative, count in failures:
        print(f"  {count:5d} {relative}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
