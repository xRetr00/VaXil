from __future__ import annotations

from .common import failure, is_readable, resolve_path, success

SPEC = {
    "name": "file_search",
    "title": "File Search",
    "description": "Search for text under a readable directory.",
    "args_schema": {"type": "object", "properties": {"root": {"type": "string"}, "query": {"type": "string"}}, "required": ["root", "query"]},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["files", "coding"],
}


def run(service, args, _context):
    root = resolve_path(service.context, str(args.get("root") or ""))
    query = str(args.get("query") or "")
    if not query or not is_readable(root) or not root.is_dir():
        return failure("Search failed", "Provide a readable root and a non-empty query.")
    matches = []
    for path in root.rglob("*"):
        if len(matches) >= 25 or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue
        if query.lower() in text.lower():
            matches.append(str(path))
    return success("Search complete", f"Found {len(matches)} matching files.", matches=matches)
