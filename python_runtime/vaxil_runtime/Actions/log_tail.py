from __future__ import annotations

from .common import failure, is_readable, resolve_path, success

SPEC = {
    "name": "log_tail",
    "title": "Log Tail",
    "description": "Read the tail of a readable log file.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}, "lines": {"type": "integer"}}, "required": ["path"]},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["logs"],
}


def run(service, args, _context):
    path = resolve_path(service.context, str(args.get("path") or ""))
    if not is_readable(path):
        return failure("Log unreadable", f"{path} is not readable.")
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    count = max(1, int(args.get("lines") or 120))
    return success("Log tail", f"Read the last {count} lines from {path}.", text="\n".join(lines[-count:]))
