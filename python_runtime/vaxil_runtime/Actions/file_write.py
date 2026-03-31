from __future__ import annotations

from .common import failure, is_writable, resolve_path, success

SPEC = {
    "name": "file_write",
    "title": "File Write",
    "description": "Write a UTF-8 file under an allowed writable root.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}, "content": {"type": "string"}}, "required": ["path", "content"]},
    "risk_level": "workspace_write",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["files", "coding"],
}


def run(service, args, _context):
    path = resolve_path(service.context, str(args.get("path") or ""))
    if not is_writable(service.context, path):
        return failure("Write denied", f"{path} is outside allowed roots.")
    content = str(args.get("content") or "")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return success("File written", f"Wrote {path}.", path=str(path), bytes=len(content.encode("utf-8")))
