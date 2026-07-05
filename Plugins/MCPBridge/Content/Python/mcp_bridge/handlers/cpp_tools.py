"""C++ class generation and UnrealBuildTool integration.

cpp_class_create writes UCLASS header/source boilerplate into a game module.
cpp_build runs UBT as a background job (see mcp_bridge.jobs) and parses
compiler errors into structured results. Building the editor target while
the editor is running compiles normally but may fail at the link step with
locked DLLs; compile errors are still fully reported, which is the primary
validation use case. A full reload of new classes requires an editor restart.
"""

import json
import os
import re
from typing import Any, Dict, List, Optional, Tuple

from mcp_bridge import jobs


_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# parent key -> (prefix, include path, base class name, extra module deps note)
_PARENT_CLASSES: Dict[str, Tuple[str, str, str, str]] = {
    "Actor": ("A", "GameFramework/Actor.h", "AActor", ""),
    "Pawn": ("A", "GameFramework/Pawn.h", "APawn", ""),
    "Character": ("A", "GameFramework/Character.h", "ACharacter", ""),
    "GameModeBase": ("A", "GameFramework/GameModeBase.h", "AGameModeBase", ""),
    "GameStateBase": ("A", "GameFramework/GameStateBase.h", "AGameStateBase", ""),
    "PlayerController": ("A", "GameFramework/PlayerController.h", "APlayerController", ""),
    "PlayerState": ("A", "GameFramework/PlayerState.h", "APlayerState", ""),
    "HUD": ("A", "GameFramework/HUD.h", "AHUD", ""),
    "AIController": ("A", "AIController.h", "AAIController", "AIModule"),
    "ActorComponent": ("U", "Components/ActorComponent.h", "UActorComponent", ""),
    "SceneComponent": ("U", "Components/SceneComponent.h", "USceneComponent", ""),
    "GameInstance": ("U", "Engine/GameInstance.h", "UGameInstance", ""),
    "SaveGame": ("U", "GameFramework/SaveGame.h", "USaveGame", ""),
    "Object": ("U", "UObject/NoExportTypes.h", "UObject", ""),
    "BTTaskNode": ("U", "BehaviorTree/BTTaskNode.h", "UBTTaskNode", "AIModule, GameplayTasks"),
    "BTService": ("U", "BehaviorTree/BTService.h", "UBTService", "AIModule, GameplayTasks"),
    "BlueprintFunctionLibrary": ("U", "Kismet/BlueprintFunctionLibrary.h", "UBlueprintFunctionLibrary", ""),
}

_HEADER_TEMPLATE = """#pragma once

#include "CoreMinimal.h"
#include "{include}"
#include "{name}.generated.h"

UCLASS({uclass_specifiers})
class {api_macro} {prefix}{name} : public {base}
{{
	GENERATED_BODY()

public:
	{prefix}{name}();
}};
"""

_SOURCE_TEMPLATE = """#include "{header_rel}"

{prefix}{name}::{prefix}{name}()
{{
}}
"""


def _project_dir() -> str:
    import unreal

    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def _project_file() -> str:
    import unreal

    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())


def _engine_dir() -> str:
    import unreal

    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_dir())


def _game_modules() -> List[str]:
    """Module names declared in the .uproject."""
    try:
        with open(_project_file(), "r", encoding="utf-8") as fh:
            data = json.load(fh)
        return [m.get("Name", "") for m in data.get("Modules", [])]
    except Exception:
        return []


def handle_cpp_class_create(params: Dict[str, Any]) -> Dict[str, Any]:
    """Generate a UCLASS header/source pair in a game module.

    Args:
        params: name (str, no prefix letter), parent_class (key of the
            supported parent set), module (str, default: first .uproject
            module), subdir (str, optional path under Public/Private or the
            module root), uclass_specifiers (str, e.g. "Blueprintable").

    Returns:
        Created file paths. Files are not compiled; run cpp_build next.
    """
    name = params.get("name", "")
    parent_key = params.get("parent_class", "Actor")

    if not _IDENT_RE.match(name or ""):
        return {"success": False, "data": {},
                "error": f"Invalid class name '{name}': must be a C++ identifier without prefix letter"}
    if parent_key not in _PARENT_CLASSES:
        return {"success": False, "data": {},
                "error": f"Unsupported parent_class '{parent_key}'. Supported: {sorted(_PARENT_CLASSES)}"}

    modules = _game_modules()
    module = params.get("module") or (modules[0] if modules else "")
    if not module:
        return {"success": False, "data": {}, "error": "No game modules found in .uproject"}
    if module not in modules:
        return {"success": False, "data": {},
                "error": f"Module '{module}' not in .uproject modules: {modules}"}

    module_dir = os.path.join(_project_dir(), "Source", module)
    if not os.path.isdir(module_dir):
        return {"success": False, "data": {}, "error": f"Module directory not found: {module_dir}"}

    prefix, include, base, dep_note = _PARENT_CLASSES[parent_key]
    api_macro = f"{module.upper()}_API"
    subdir = params.get("subdir", "")

    # Use Public/Private split when the module has it, else flat layout.
    public_dir = os.path.join(module_dir, "Public")
    private_dir = os.path.join(module_dir, "Private")
    if os.path.isdir(public_dir) and os.path.isdir(private_dir):
        header_dir = os.path.join(public_dir, subdir) if subdir else public_dir
        source_dir = os.path.join(private_dir, subdir) if subdir else private_dir
        header_rel = (subdir + "/" if subdir else "") + f"{name}.h"
    else:
        header_dir = os.path.join(module_dir, subdir) if subdir else module_dir
        source_dir = header_dir
        header_rel = f"{name}.h"

    header_path = os.path.join(header_dir, f"{name}.h")
    source_path = os.path.join(source_dir, f"{name}.cpp")
    for path in (header_path, source_path):
        if os.path.exists(path):
            return {"success": False, "data": {"path": path},
                    "error": f"Refusing to overwrite existing file: {path}"}

    header = _HEADER_TEMPLATE.format(
        include=include, name=name, prefix=prefix, base=base,
        api_macro=api_macro,
        uclass_specifiers=params.get("uclass_specifiers", ""),
    )
    source = _SOURCE_TEMPLATE.format(header_rel=header_rel.replace("\\", "/"), prefix=prefix, name=name)

    os.makedirs(header_dir, exist_ok=True)
    os.makedirs(source_dir, exist_ok=True)
    with open(header_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(header)
    with open(source_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(source)

    data = {
        "class": f"{prefix}{name}",
        "parent": base,
        "module": module,
        "header": header_path,
        "source": source_path,
        "next_step": "Run cpp_build to compile; new classes need an editor restart to load.",
    }
    if dep_note:
        data["module_dependency_note"] = (
            f"Ensure {module}.Build.cs lists these modules: {dep_note}"
        )
    return {"success": True, "data": data}


_MSVC_DIAG_RE = re.compile(
    r"^\s*(?P<file>.+?)\((?P<line>\d+)\)\s*:\s*(?P<kind>error|warning)\s+(?P<code>[A-Z]+\d+)\s*:\s*(?P<message>.*)$"
)
_LINK_DIAG_RE = re.compile(
    r"^\s*(?P<file>.+?)\s*:\s*(?P<kind>fatal error|error)\s+(?P<code>LNK\d+)\s*:\s*(?P<message>.*)$"
)


def parse_build_output(lines: List[str], returncode: int) -> Dict[str, Any]:
    """Parse UBT/MSVC output into structured errors and warnings."""
    errors: List[Dict[str, Any]] = []
    warnings: List[Dict[str, Any]] = []
    for line in lines:
        m = _MSVC_DIAG_RE.match(line) or _LINK_DIAG_RE.match(line)
        if not m:
            continue
        entry = {
            "file": m.group("file").strip(),
            "line": int(m.groupdict().get("line") or 0) if m.groupdict().get("line") else None,
            "code": m.group("code"),
            "message": m.group("message").strip(),
        }
        if "error" in m.group("kind"):
            errors.append(entry)
        else:
            warnings.append(entry)

    locked_dll = any("LNK1104" in (e.get("code") or "") for e in errors)
    result: Dict[str, Any] = {
        "build_succeeded": returncode == 0,
        "error_count": len(errors),
        "warning_count": len(warnings),
        "errors": errors[:50],
        "warnings": warnings[:30],
    }
    if locked_dll:
        result["note"] = (
            "LNK1104 usually means the editor is running and holds the target "
            "DLL. Compilation diagnostics above are still valid; restart the "
            "editor to link and load new binaries."
        )
    return result


def handle_cpp_build(params: Dict[str, Any]) -> Dict[str, Any]:
    """Start a UBT build of the project as a background job.

    Args:
        params: target (str, default '<ProjectName>Editor'), platform (str,
            default Win64), configuration (str, default Development),
            extra_args (list of str, optional).

    Returns:
        job_id envelope; poll with job_status.
    """
    project_file = _project_file()
    project_name = os.path.splitext(os.path.basename(project_file))[0]
    target = params.get("target") or f"{project_name}Editor"
    platform = params.get("platform", "Win64")
    configuration = params.get("configuration", "Development")

    ubt = os.path.join(_engine_dir(), "Binaries", "DotNET", "UnrealBuildTool.exe")
    if not os.path.isfile(ubt):
        return {"success": False, "data": {}, "error": f"UnrealBuildTool not found: {ubt}"}

    argv = [ubt, target, platform, configuration, f"-Project={project_file}", "-WaitMutex"]
    extra = params.get("extra_args") or []
    if isinstance(extra, list):
        argv.extend(str(a) for a in extra)

    return jobs.start_subprocess_job(
        kind="cpp_build",
        description=f"UBT {target} {platform} {configuration}",
        argv=argv,
        cwd=os.path.dirname(project_file),
        timeout_seconds=float(params.get("timeout_seconds", 1800)),
        parse_result=parse_build_output,
    )


def handle_job_status(params: Dict[str, Any]) -> Dict[str, Any]:
    """Get one job's status, structured result, and output tail."""
    job_id = params.get("job_id", "")
    if not job_id:
        return {"success": False, "data": {}, "error": "Missing 'job_id'"}
    return jobs.get_job(job_id, include_output=bool(params.get("include_output", True)))


def handle_job_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List all jobs started in this editor session."""
    return jobs.list_jobs()


def handle_job_cancel(params: Dict[str, Any]) -> Dict[str, Any]:
    """Cancel a running job (terminates its subprocess)."""
    job_id = params.get("job_id", "")
    if not job_id:
        return {"success": False, "data": {}, "error": "Missing 'job_id'"}
    return jobs.cancel_job(job_id)
