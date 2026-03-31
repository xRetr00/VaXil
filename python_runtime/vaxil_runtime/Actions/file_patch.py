from __future__ import annotations

from .common import failure, is_writable, resolve_path, success

SPEC = {
    "name": "file_patch",
    "title": "File Patch",
    "description": "Replace text inside a writable file.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}, "find": {"type": "string"}, "replace": {"type": "string"}}, "required": ["path", "find", "replace"]},
    "risk_level": "workspace_write",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["files", "coding"],
}


def run(service, args, _context):
    path = resolve_path(service.context, str(args.get("path") or ""))
    if not is_writable(service.context, path) or not path.exists():
        return failure("Patch denied", f"{path} is not patchable.")
    find = str(args.get("find") or "")
    replace = str(args.get("replace") or "")
    text = path.read_text(encoding="utf-8", errors="replace")
    if find not in text:
        return failure("Patch failed", "Target text was not found.")
    path.write_text(text.replace(find, replace), encoding="utf-8")
    return success("File patched", f"Patched {path}.", path=str(path))
