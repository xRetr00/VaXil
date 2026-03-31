from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Any

from vaxil_runtime.runtime_types import RuntimeContext


def success(summary: str, detail: str, **payload: Any) -> dict[str, Any]:
    return {
        "ok": True,
        "summary": summary,
        "detail": detail,
        "payload": payload,
        "artifacts": [],
        "logs": [],
        "requires_confirmation": False,
    }


def failure(summary: str, detail: str, **payload: Any) -> dict[str, Any]:
    return {
        "ok": False,
        "summary": summary,
        "detail": detail,
        "payload": payload,
        "artifacts": [],
        "logs": [],
        "requires_confirmation": False,
    }


def resolve_path(runtime_context: RuntimeContext, raw_path: str) -> Path:
    candidate = Path(raw_path)
    if candidate.is_absolute():
        return candidate
    return (runtime_context.workspace_root / candidate).resolve(strict=False)


def is_readable(path: Path) -> bool:
    return path.exists() and os.access(path, os.R_OK)


def is_writable(runtime_context: RuntimeContext, path: Path) -> bool:
    try:
        resolved = path.resolve(strict=False)
    except Exception:
        resolved = path.absolute()
    for root in runtime_context.allowed_roots:
        try:
            resolved.relative_to(root.resolve(strict=False))
            return True
        except Exception:
            continue
    return False


def read_text_limited(path: Path, max_chars: int = 12000) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    if len(text) > max_chars:
        return text[:max_chars] + "\n...[truncated]"
    return text


def resolve_user_base_dir(base_dir: str) -> Path:
    home = Path.home()
    normalized = (base_dir or "desktop").strip().lower()
    mapping = {
        "desktop": home / "Desktop",
        "documents": home / "Documents",
        "downloads": home / "Downloads",
        "pictures": home / "Pictures",
        "music": home / "Music",
        "videos": home / "Videos",
        "home": home,
    }
    return mapping.get(normalized, home / "Desktop")


def ensure_absolute_url(url: str) -> str:
    value = url.strip()
    if not value:
        return value
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", value):
        return value
    if not re.search(r"://", value):
        return f"https://{value}"
    return value
