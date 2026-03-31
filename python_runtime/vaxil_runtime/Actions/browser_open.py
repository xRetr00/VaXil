from __future__ import annotations

from .common import ensure_absolute_url, failure, success

SPEC = {
    "name": "browser_open",
    "title": "Browser Open",
    "description": "Open a URL with Playwright browser automation.",
    "args_schema": {"type": "object", "properties": {"url": {"type": "string"}, "timeout_ms": {"type": "integer"}}, "required": ["url"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["browser", "playwright"],
}


def run(service, args, _context):
    if not service.playwright.available:
        return failure("Playwright unavailable", "Playwright is not installed or browser binaries are missing.")
    url = ensure_absolute_url(str(args.get("url") or ""))
    if not url:
        return failure("Browser open failed", "A URL is required.")
    payload = service.playwright.open_url(url, timeout_ms=max(1000, int(args.get("timeout_ms") or 30000)))
    return success("Browser page opened", f"Opened {payload['url']} with Playwright.", **payload)
