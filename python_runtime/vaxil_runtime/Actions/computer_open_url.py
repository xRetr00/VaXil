from __future__ import annotations

import webbrowser

from .common import ensure_absolute_url, failure, success

SPEC = {
    "name": "computer_open_url",
    "title": "Open URL",
    "description": "Open a URL with the system handler.",
    "args_schema": {"type": "object", "properties": {"url": {"type": "string"}}, "required": ["url"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["desktop", "browser"],
}


def run(_service, args, _context):
    url = ensure_absolute_url(str(args.get("url") or ""))
    if not url:
        return failure("Open URL failed", "A URL is required.")
    if webbrowser.open(url):
        return success("URL opened", f"Opened {url}.", url=url)
    return failure("Open URL failed", f"Could not open {url}.")
