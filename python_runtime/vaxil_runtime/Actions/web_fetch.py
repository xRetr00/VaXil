from __future__ import annotations

import urllib.request

from .common import failure, success

SPEC = {
    "name": "web_fetch",
    "title": "Web Fetch",
    "description": "Fetch a public URL.",
    "args_schema": {"type": "object", "properties": {"url": {"type": "string"}}, "required": ["url"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["web"],
}


def run(_service, args, _context):
    url = str(args.get("url") or "")
    if not url:
        return failure("Fetch failed", "A URL is required.")
    request = urllib.request.Request(url, headers={"User-Agent": "VaxilPythonRuntime/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        body = response.read().decode("utf-8", errors="replace")
    if len(body) > 12000:
        body = body[:12000] + "\n...[truncated]"
    return success("Fetch complete", f"Fetched {url}.", text=body, url=url)
