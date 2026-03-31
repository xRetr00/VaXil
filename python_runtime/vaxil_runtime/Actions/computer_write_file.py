from __future__ import annotations

from .common import failure, resolve_user_base_dir, success

SPEC = {
    "name": "computer_write_file",
    "title": "Desktop File Write",
    "description": "Create a text file in a known user directory.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}, "content": {"type": "string"}, "base_dir": {"type": "string"}, "overwrite": {"type": "boolean"}}, "required": ["path", "content"]},
    "risk_level": "desktop_write",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["desktop", "files"],
}


def run(_service, args, _context):
    target = resolve_user_base_dir(str(args.get("base_dir") or "desktop")) / str(args.get("path") or "")
    overwrite = bool(args.get("overwrite", False))
    if target.exists() and not overwrite:
        return failure("File exists", f"{target} already exists.")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(str(args.get("content") or ""), encoding="utf-8")
    return success("Desktop file written", f"Wrote {target}.", path=str(target))
