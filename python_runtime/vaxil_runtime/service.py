from __future__ import annotations

import json
import re
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Any

from vaxil_runtime.Actions import ACTIONS
from vaxil_runtime.playwright_manager import PlaywrightBrowserManager
from vaxil_runtime.runtime_types import RuntimeContext


def _optional_import(module_name: str):
    try:
        return __import__(module_name)
    except Exception:
        return None


class VaxilRuntimeService:
    def __init__(self) -> None:
        self.context = RuntimeContext()
        self.youtube_transcript_api = _optional_import("youtube_transcript_api")
        self.playwright = PlaywrightBrowserManager()
        self._actions = ACTIONS

    def close(self) -> None:
        self.playwright.close()

    def handle(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if method == "initialize":
            return self._initialize(params)
        if method == "catalog.list":
            return {"actions": self._catalog()}
        if method == "action.execute":
            return self._execute_action(params)
        if method == "skill.list":
            return {"skills": self._list_skills()}
        if method == "skill.create":
            return self._create_skill(params)
        if method == "skill.install":
            return self._install_skill(params)
        raise RuntimeError(f"Unknown method: {method}")

    def _initialize(self, params: dict[str, Any]) -> dict[str, Any]:
        self.context.workspace_root = Path(params.get("workspace_root") or Path.cwd())
        self.context.source_root = Path(params.get("source_root") or self.context.workspace_root)
        self.context.application_dir = Path(params.get("application_dir") or self.context.workspace_root)
        self.context.skills_root = Path(params.get("skills_root") or (self.context.workspace_root / "skills"))
        self.context.skills_root.mkdir(parents=True, exist_ok=True)
        self.context.allowed_roots = [Path(path) for path in params.get("allowed_roots") or [] if path]
        if self.context.workspace_root not in self.context.allowed_roots:
            self.context.allowed_roots.append(self.context.workspace_root)
        if self.context.skills_root not in self.context.allowed_roots:
            self.context.allowed_roots.append(self.context.skills_root)
        return {
            "ok": True,
            "platform": self._platform_id(),
            "actions": len(self._actions),
            "playwright_available": self.playwright.available,
        }

    def _platform_id(self) -> str:
        import platform

        return platform.system().lower()

    def _catalog(self) -> list[dict[str, Any]]:
        return [entry["spec"] for entry in self._actions.values()]

    def _execute_action(self, params: dict[str, Any]) -> dict[str, Any]:
        name = str(params.get("name") or "").strip()
        args = params.get("args") or {}
        context = params.get("context") or {}
        action = self._actions.get(name)
        if action is None:
            raise RuntimeError(f"Unsupported action: {name}")
        return action["run"](self, args, context)

    def _normalize_skill_id(self, value: str) -> str:
        normalized = re.sub(r"[^a-zA-Z0-9_-]+", "-", value.strip().lower())
        normalized = re.sub(r"-{2,}", "-", normalized).strip("-")
        return normalized

    def _result(self, ok: bool, summary: str, detail: str, **payload: Any) -> dict[str, Any]:
        return {
            "ok": ok,
            "summary": summary,
            "detail": detail,
            "payload": payload,
            "artifacts": [],
            "logs": [],
            "requires_confirmation": False,
        }

    def _list_skills(self) -> list[dict[str, Any]]:
        skills: list[dict[str, Any]] = []
        for manifest_path in self.context.skills_root.rglob("skill.json"):
            try:
                payload = json.loads(manifest_path.read_text(encoding="utf-8"))
            except Exception:
                continue
            payload.setdefault("runtime", "declarative")
            skills.append(payload)
        return skills

    def _create_skill(self, params: dict[str, Any]) -> dict[str, Any]:
        skill_id = self._normalize_skill_id(str(params.get("id") or ""))
        skill_name = str(params.get("name") or "").strip()
        description = str(params.get("description") or "").strip()
        if not skill_id or not skill_name:
            return self._result(False, "Invalid skill", "Skill id and name are required.")

        root = self.context.skills_root / skill_id
        if root.exists():
            return self._result(False, "Skill exists", f"{skill_id} already exists.")

        root.mkdir(parents=True, exist_ok=True)
        manifest = {
            "id": skill_id,
            "name": skill_name,
            "version": "1.0.0",
            "description": description,
            "prompt_template": "prompt.txt",
            "runtime": "declarative",
            "python_module": "",
            "actions": [],
            "dependencies": [],
            "permissions": [],
            "platforms": [],
        }
        (root / "skill.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        (root / "README.md").write_text(f"# {skill_name}\n\n{description}\n", encoding="utf-8")
        (root / "prompt.txt").write_text(
            f"Skill: {skill_name}\nPurpose: {description}\nInstructions:\n- Replace this scaffold with concrete guidance.\n",
            encoding="utf-8",
        )
        return self._result(True, "Skill created", f"Created {skill_id}.", path=str(root))

    def _install_skill(self, params: dict[str, Any]) -> dict[str, Any]:
        url = str(params.get("url") or "").strip()
        if not url:
            return self._result(False, "Invalid skill URL", "A skill URL is required.")

        download_url = self._resolve_skill_download_url(url)
        with tempfile.TemporaryDirectory(prefix="vaxil-skill-") as temp_dir:
            zip_path = Path(temp_dir) / "skill.zip"
            urllib.request.urlretrieve(download_url, zip_path)
            extract_root = Path(temp_dir) / "extract"
            with zipfile.ZipFile(zip_path) as archive:
                archive.extractall(extract_root)

            manifest_paths = list(extract_root.rglob("skill.json"))
            if not manifest_paths:
                return self._result(False, "Invalid skill package", "Downloaded skill is missing skill.json.")

            manifest_path = manifest_paths[0]
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            skill_id = self._normalize_skill_id(str(manifest.get("id") or ""))
            if not skill_id:
                return self._result(False, "Invalid skill package", "skill.json is missing a valid id.")

            destination = self.context.skills_root / skill_id
            if destination.exists():
                shutil.rmtree(destination)
            shutil.copytree(manifest_path.parent, destination)
            return self._result(True, "Skill installed", f"Installed {skill_id}.", path=str(destination))

    def _resolve_skill_download_url(self, url: str) -> str:
        match = re.match(r"^https://github\.com/([^/]+)/([^/]+)/?$", url, re.IGNORECASE)
        if match:
            return f"https://codeload.github.com/{match.group(1)}/{match.group(2)}/zip/refs/heads/main"
        return url
