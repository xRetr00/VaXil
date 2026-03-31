from __future__ import annotations

import os
import urllib.parse
import urllib.request

from .common import failure, success

SPEC = {
    "name": "web_search",
    "title": "Web Search",
    "description": "Search the web using Brave or DuckDuckGo fallback.",
    "args_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["web"],
}


def run(_service, args, context):
    query = str(args.get("query") or "").strip()
    if not query:
        return failure("Search failed", "A query is required.")
    brave_key = str(context.get("braveSearchApiKey") or os.environ.get("BRAVE_SEARCH_API_KEY") or "").strip()
    if brave_key:
        url = f"https://api.search.brave.com/res/v1/web/search?q={urllib.parse.quote(query)}"
        request = urllib.request.Request(url, headers={"Accept": "application/json", "X-Subscription-Token": brave_key, "User-Agent": "VaxilPythonRuntime/1.0"})
        with urllib.request.urlopen(request, timeout=20) as response:
            body = response.read().decode("utf-8", errors="replace")
        return success("Search complete", "Fetched Brave Search results.", json=body)
    fallback_url = f"https://api.duckduckgo.com/?q={urllib.parse.quote(query)}&format=json&no_html=1&skip_disambig=1"
    request = urllib.request.Request(fallback_url, headers={"User-Agent": "VaxilPythonRuntime/1.0"})
    with urllib.request.urlopen(request, timeout=20) as response:
        body = response.read().decode("utf-8", errors="replace")
    return success("Search complete", "Fetched DuckDuckGo fallback results.", json=body)
