from __future__ import annotations

from .common import failure, resolve_path
from .file_read import run as file_read_run

SPEC = {
    "name": "ai_log_read",
    "title": "AI Log Read",
    "description": "Read the latest AI exchange log or a specific readable AI log file.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}}},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["logs", "ai"],
}


def run(service, args, _context):
    raw_path = str(args.get("path") or "")
    if raw_path:
        return file_read_run(service, {"path": raw_path}, {})
    candidates = sorted((service.context.workspace_root / "bin" / "logs" / "AI").glob("*.log"), key=lambda item: item.stat().st_mtime, reverse=True)
    if not candidates:
        return failure("No AI logs", "No AI logs were found.")
    return file_read_run(service, {"path": str(resolve_path(service.context, str(candidates[0])))}, {})
