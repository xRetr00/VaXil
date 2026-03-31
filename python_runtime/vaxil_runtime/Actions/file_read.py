from __future__ import annotations

from .common import failure, is_readable, read_text_limited, resolve_path, success

SPEC = {
    "name": "file_read",
    "title": "File Read",
    "description": "Read a UTF-8 text file from a readable path.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["files", "coding"],
}


def run(service, args, _context):
    path = resolve_path(service.context, str(args.get("path") or ""))
    if not is_readable(path) or not path.is_file():
        return failure("File unreadable", f"{path} is not readable.")
    return success("File read", f"Read {path}.", text=read_text_limited(path), path=str(path))
