from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass(slots=True)
class RuntimeContext:
    workspace_root: Path = Path.cwd()
    source_root: Path = Path.cwd()
    application_dir: Path = Path.cwd()
    skills_root: Path = Path.cwd() / "skills"
    allowed_roots: list[Path] = field(default_factory=list)
