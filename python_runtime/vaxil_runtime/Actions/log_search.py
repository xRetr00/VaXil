from __future__ import annotations

from .common import failure, is_readable, resolve_path, success

SPEC = {
    "name": "log_search",
    "title": "Log Search",
    "description": "Search a readable log directory or log file for a pattern.",
    "args_schema": {"type": "object", "properties": {"path": {"type": "string"}, "query": {"type": "string"}}, "required": ["path", "query"]},
    "risk_level": "read_only",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["logs"],
}


def run(service, args, _context):
    root = resolve_path(service.context, str(args.get("path") or ""))
    query = str(args.get("query") or "")
    if not query or not is_readable(root):
        return failure("Log search failed", "Provide a readable path and a query.")
    matches = []
    files = root.rglob("*") if root.is_dir() else [root]
    for path in files:
        if len(matches) >= 150 or not path.is_file():
            continue
        try:
            for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), start=1):
                if query.lower() in line.lower():
                    matches.append(f"{path}:{line_number}: {line.strip()}")
                    if len(matches) >= 150:
                        break
        except Exception:
            continue
    if not matches:
        return failure("No log matches", "The query did not match any readable log lines.")
    return success("Log search complete", f"Found {len(matches)} matches.", matches=matches)
