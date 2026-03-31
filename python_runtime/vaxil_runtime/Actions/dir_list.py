from __future__ import annotations

from .common import failure, is_readable, resolve_path, success

SPEC = {
    "name": "dir_list",
    "title": "Directory List",
    "description": "List files under a readable directory.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["files"],
}


def run(service, args, _context):
    path = resolve_path(service.context, str(args.get("path") or ""))
    if not is_readable(path) or not path.is_dir():
        return failure("Directory unreadable", f"{path} is not readable.")
    entries = [{"type": "dir" if entry.is_dir() else "file", "name": entry.name} for entry in sorted(path.iterdir())[:100]]
    return success("Directory listed", f"Listed {path}.", entries=entries)
