from __future__ import annotations

import json
import platform
import subprocess

from .common import failure, success

SPEC = {
    "name": "computer_list_apps",
    "title": "List Apps",
    "description": "List installed apps on Windows.",
    "args_schema": {"type": "object", "properties": {"query": {"type": "string"}, "limit": {"type": "integer"}}},
    "risk_level": "read_only",
    "platforms": ["windows"],
    "supports_background": True,
    "tags": ["desktop", "apps"],
}


def run(_service, args, _context):
    if platform.system().lower() != "windows":
        return failure("Unsupported platform", "App listing currently targets Windows.")
    query = str(args.get("query") or "").strip()
    limit = max(1, int(args.get("limit") or 20))
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "Get-StartApps | Sort-Object Name | Select-Object Name,AppID | ConvertTo-Json -Depth 3"]
    output = subprocess.run(command, capture_output=True, text=True, timeout=20, check=True)
    rows = json.loads(output.stdout or "[]")
    if isinstance(rows, dict):
        rows = [rows]
    apps = []
    for row in rows:
        name = str(row.get("Name") or "")
        if query and query.lower() not in name.lower():
            continue
        apps.append({"name": name, "app_id": row.get("AppID")})
        if len(apps) >= limit:
            break
    return success("App list complete", f"Found {len(apps)} apps.", apps=apps)
