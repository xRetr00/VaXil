from __future__ import annotations

import json
import os
import sys
import traceback
from pathlib import Path

if getattr(sys, "frozen", False):
    PACKAGE_ROOT = Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    bundled_browsers = Path(sys.executable).resolve().parent / "ms-playwright"
    if bundled_browsers.exists():
        os.environ.setdefault("PLAYWRIGHT_BROWSERS_PATH", str(bundled_browsers))
else:
    PACKAGE_ROOT = Path(__file__).resolve().parent.parent

if str(PACKAGE_ROOT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_ROOT))

from vaxil_runtime.service import VaxilRuntimeService


def write_message(payload: dict) -> None:
    sys.stdout.write(json.dumps(payload, ensure_ascii=True) + "\n")
    sys.stdout.flush()


def main() -> int:
    service = VaxilRuntimeService()

    try:
        for raw_line in sys.stdin:
            line = raw_line.strip()
            if not line:
                continue

            try:
                request = json.loads(line)
            except json.JSONDecodeError as exc:
                write_message(
                    {
                        "jsonrpc": "2.0",
                        "error": {"code": -32700, "message": f"Invalid JSON: {exc}"},
                    }
                )
                continue

            request_id = request.get("id")
            method = request.get("method")
            params = request.get("params") or {}

            if method == "shutdown":
                break

            try:
                result = service.handle(method, params)
                write_message({"jsonrpc": "2.0", "id": request_id, "result": result})
            except Exception as exc:  # pragma: no cover - defensive runtime path
                traceback.print_exc(file=sys.stderr)
                write_message(
                    {
                        "jsonrpc": "2.0",
                        "id": request_id,
                        "error": {"code": -32000, "message": str(exc)},
                    }
                )
    finally:
        service.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
