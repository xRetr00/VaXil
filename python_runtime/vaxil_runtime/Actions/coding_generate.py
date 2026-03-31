from __future__ import annotations

from .common import failure, is_writable, resolve_path, success

SPEC = {
    "name": "coding_generate",
    "title": "Coding Generate",
    "description": "Generate starter code or boilerplate for a requested task and optionally write it to a file.",
    "args_schema": {
        "type": "object",
        "properties": {
            "language": {"type": "string"},
            "task": {"type": "string"},
            "path": {"type": "string"},
            "write_to_file": {"type": "boolean"}
        },
        "required": ["language", "task"]
    },
    "risk_level": "workspace_write",
    "platforms": ["windows", "linux"],
    "supports_background": True,
    "tags": ["coding", "generate"],
}


def _template(language: str, task: str) -> str:
    lang = language.strip().lower()
    if lang in {"python", "py"}:
        return f'"""Generated starter for: {task}."""\n\n\ndef main() -> None:\n    print("TODO: implement {task}")\n\n\nif __name__ == "__main__":\n    main()\n'
    if lang in {"javascript", "js"}:
        return f'// Generated starter for: {task}\n\nfunction main() {{\n  console.log("TODO: implement {task}");\n}}\n\nmain();\n'
    if lang in {"typescript", "ts"}:
        return f'// Generated starter for: {task}\n\nfunction main(): void {{\n  console.log("TODO: implement {task}");\n}}\n\nmain();\n'
    if lang in {"cpp", "c++"}:
        return f'// Generated starter for: {task}\n#include <iostream>\n\nint main() {{\n    std::cout << "TODO: implement {task}" << std::endl;\n    return 0;\n}}\n'
    if lang in {"html"}:
        return f'<!doctype html>\n<html lang="en">\n<head>\n  <meta charset="utf-8" />\n  <title>{task}</title>\n</head>\n<body>\n  <main>\n    <h1>{task}</h1>\n    <p>TODO: implement.</p>\n  </main>\n</body>\n</html>\n'
    return f"# Generated starter for: {task}\n# Language: {language}\n\nTODO: implement\n"


def run(service, args, _context):
    language = str(args.get("language") or "").strip()
    task = str(args.get("task") or "").strip()
    if not language or not task:
        return failure("Code generation failed", "language and task are required.")
    code = _template(language, task)
    write_to_file = bool(args.get("write_to_file", False))
    path_value = str(args.get("path") or "").strip()
    payload = {"language": language, "task": task, "code": code}
    if write_to_file and path_value:
        target = resolve_path(service.context, path_value)
        if not is_writable(service.context, target):
            return failure("Code generation failed", f"{target} is outside allowed roots.", **payload)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(code, encoding="utf-8")
        payload["path"] = str(target)
    return success("Code generated", f"Generated starter code for {language}.", **payload)
