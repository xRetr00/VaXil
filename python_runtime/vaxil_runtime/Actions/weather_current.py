from __future__ import annotations

import urllib.request

from .common import success

SPEC = {
    "name": "weather_current",
    "title": "Weather Current",
    "description": "Fetch current weather by coordinates.",
    "args_schema": {"type": "object", "properties": {"latitude": {"type": "number"}, "longitude": {"type": "number"}}, "required": ["latitude", "longitude"]},
    "risk_level": "network_access",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["weather"],
}


def run(_service, args, _context):
    latitude = float(args.get("latitude"))
    longitude = float(args.get("longitude"))
    url = (
        "https://api.open-meteo.com/v1/forecast?"
        f"latitude={latitude}&longitude={longitude}&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
    )
    request = urllib.request.Request(url, headers={"User-Agent": "VaxilPythonRuntime/1.0"})
    with urllib.request.urlopen(request, timeout=20) as response:
        body = response.read().decode("utf-8", errors="replace")
    return success("Weather fetched", "Fetched current weather from Open-Meteo.", json=body)
