from __future__ import annotations

import platform
import subprocess
from pathlib import Path

from .common import failure, success

SPEC = {
    "name": "shell_run",
    "title": "Shell Run",
    "description": "Run a local shell command with timeout and cwd.",
    "args_schema": {"type": "object", "properties": {"command": {"type": "string"}, "cwd": {"type": "string"}, "timeout_ms": {"type": "integer"}}, "required": ["command"]},
    "risk_level": "shell_exec",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["shell", "coding"],
}


def run(service, args, _context):
    command = str(args.get("command") or "").strip()
    if not command:
        return failure("Shell run failed", "A command is required.")
    cwd_value = str(args.get("cwd") or service.context.workspace_root)
    cwd = Path(cwd_value)
    timeout_ms = max(1000, int(args.get("timeout_ms") or 30000))
    if platform.system().lower() == "windows":
        run_args = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command]
    else:
        run_args = ["/bin/sh", "-lc", command]
    completed = subprocess.run(run_args, cwd=str(cwd), capture_output=True, text=True, timeout=timeout_ms / 1000.0)
    title = "Shell command finished" if completed.returncode == 0 else "Shell command failed"
    return success(title, f"Exit code {completed.returncode}", stdout=completed.stdout[-12000:], stderr=completed.stderr[-12000:], exit_code=completed.returncode)
