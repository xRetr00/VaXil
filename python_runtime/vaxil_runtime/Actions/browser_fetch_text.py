from __future__ import annotations

from .common import ensure_absolute_url, failure, success

SPEC = {
    "name": "browser_fetch_text",
    "title": "Browser Fetch Text",
    "description": "Fetch page text with Playwright browser automation.",
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
        return failure("Browser fetch failed", "A URL is required.")
    payload = service.playwright.fetch_text(url, timeout_ms=max(1000, int(args.get("timeout_ms") or 30000)))
    text = payload.get("text", "")
    if len(text) > 12000:
        payload["text"] = text[:12000] + "\n...[truncated]"
    return success("Browser text fetched", f"Fetched and extracted text from {payload['url']} with Playwright.", **payload)
