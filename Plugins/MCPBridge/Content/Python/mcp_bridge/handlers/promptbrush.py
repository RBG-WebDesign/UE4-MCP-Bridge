# promptbrush.py -- Handle prompt_generate, prompt_status, prompt_spec_list commands.
# This is the orchestrator that drives all generation pipeline modules.
from __future__ import annotations
import json
import os
import time
import uuid
from typing import Any, Dict

from mcp_bridge.generation.prompt_to_spec import prompt_to_spec
from mcp_bridge.generation.spec_schema import spec_to_dict
from mcp_bridge.generation.blueprint_generator import generate_all_blueprints
from mcp_bridge.generation.widget_generator import generate_all_widgets
from mcp_bridge.generation.asset_generator import generate_all_materials, generate_all_data_assets
from mcp_bridge.generation.level_generator import generate_all_levels
from mcp_bridge.generation.compile_loop import compile_all_blueprints
from mcp_bridge.generation.manifest import write_manifest
from mcp_bridge.generation.ai_generator import (
    generate_all_blackboards, generate_all_behavior_trees, generate_all_eqs_queries,
)
from mcp_bridge.generation.audio_generator import (
    generate_all_sound_attenuations, generate_all_sound_classes, generate_all_sound_mixes,
)
from mcp_bridge.generation.sequence_generator import generate_all_level_sequences
from mcp_bridge.generation.localization_generator import generate_all_string_tables
from mcp_bridge.generation.cook_generator import generate_all_primary_asset_labels


def _get_output_dir() -> str:
    """Return the output directory for specs and manifests, creating if needed."""
    try:
        import unreal
        project_dir = unreal.Paths.project_dir()
        output = os.path.join(project_dir, "PromptBrushOutput")
    except Exception:
        # Fallback for non-UE4 contexts (tests)
        output = os.path.join(os.path.dirname(__file__), "..", "..", "PromptBrushOutput")
    os.makedirs(output, exist_ok=True)
    return output


def _write_input_mappings(action_mappings: list, axis_mappings: list, project_dir: str) -> Dict[str, Any]:
    """Append action and axis mappings to DefaultInput.ini without duplicating exact lines."""
    ini_path = os.path.join(project_dir, "Config", "DefaultInput.ini")
    lines_to_add = []

    for mapping in action_mappings:
        name = mapping.get("name", "")
        key = mapping.get("key", "")
        if name and key:
            lines_to_add.append(
                f'+ActionMappings=(ActionName="{name}",'
                f'bShift=False,bCtrl=False,bAlt=False,bCmd=False,Key={key})'
            )

    for mapping in axis_mappings:
        name = mapping.get("name", "")
        key = mapping.get("key", "")
        scale = mapping.get("scale", 1.0)
        if name and key:
            lines_to_add.append(
                f'+AxisMappings=(AxisName="{name}",Scale={scale},Key={key})'
            )

    try:
        existing_lines = set()
        if os.path.exists(ini_path):
            with open(ini_path, encoding="utf-8-sig") as f:
                existing_lines = {line.strip() for line in f}

        unique_lines = []
        skipped = 0
        seen_new = set()
        for line in lines_to_add:
            if line in existing_lines or line in seen_new:
                skipped += 1
                continue
            seen_new.add(line)
            unique_lines.append(line)

        if not unique_lines:
            return {
                "success": True,
                "mappings_written": 0,
                "mappings_skipped": skipped,
                "ini_path": ini_path,
            }

        os.makedirs(os.path.dirname(ini_path), exist_ok=True)
        with open(ini_path, "a", encoding="utf-8") as f:
            f.write("\n; PromptBrush generated mappings\n")
            for line in unique_lines:
                f.write(line + "\n")
        return {
            "success": True,
            "mappings_written": len(unique_lines),
            "mappings_skipped": skipped,
            "ini_path": ini_path,
        }
    except Exception as e:
        return {"success": False, "error": str(e)}


def handle_prompt_generate(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run the full generation pipeline from a natural language prompt.

    Args:
        params:
            - prompt (str): The user's natural language request.
            - dry_run (bool): If True, return the spec without creating assets.
    """
    try:
        prompt = params.get("prompt", "").strip()
        if not prompt:
            return {"success": False, "data": {}, "error": "Missing 'prompt' parameter"}

        dry_run = bool(params.get("dry_run", False))
        run_id = str(uuid.uuid4())[:8]
        output_dir = _get_output_dir()

        # Phase 1: Convert prompt to build spec
        spec = prompt_to_spec(prompt)
        spec_dict = spec_to_dict(spec)

        # Persist spec to disk
        spec_path = os.path.join(output_dir, f"spec_{run_id}.json")
        with open(spec_path, "w", encoding="utf-8") as f:
            json.dump(spec_dict, f, indent=2)

        if dry_run:
            return {
                "success": True,
                "data": {
                    "run_id": run_id,
                    "dry_run": True,
                    "spec_path": spec_path,
                    "feature_name": spec.feature_name,
                    "genre": spec.genre,
                    "blueprint_count": len(spec.blueprints),
                    "widget_count": len(spec.widgets),
                    "material_count": len(spec.materials),
                    "data_asset_count": len(spec.data_assets),
                    "level_count": len(spec.levels),
                    "spec": spec_dict,
                },
            }

        # Phase 2: Generate all assets
        generation_results: Dict[str, Any] = {}

        bp_result = generate_all_blueprints(spec.blueprints)
        generation_results["blueprints"] = bp_result

        widget_result = generate_all_widgets(spec.widgets)
        generation_results["widgets"] = widget_result

        mat_result = generate_all_materials(spec.materials)
        generation_results["materials"] = mat_result

        data_result = generate_all_data_assets(spec.data_assets)
        generation_results["data_assets"] = data_result

        level_result = generate_all_levels(spec.levels)
        generation_results["levels"] = level_result

        # AI assets
        bb_result = generate_all_blackboards(spec.blackboards)
        generation_results["blackboards"] = bb_result

        bt_result = generate_all_behavior_trees(spec.behavior_trees)
        generation_results["behavior_trees"] = bt_result

        eqs_result = generate_all_eqs_queries(spec.eqs_queries)
        generation_results["eqs_queries"] = eqs_result

        # Audio assets
        attn_result = generate_all_sound_attenuations(spec.sound_attenuations)
        generation_results["sound_attenuations"] = attn_result

        sc_result = generate_all_sound_classes(spec.sound_classes)
        generation_results["sound_classes"] = sc_result

        mix_result = generate_all_sound_mixes(spec.sound_mixes)
        generation_results["sound_mixes"] = mix_result

        # Sequencer
        seq_result = generate_all_level_sequences(spec.level_sequences)
        generation_results["level_sequences"] = seq_result

        # Localization
        st_result = generate_all_string_tables(spec.string_tables)
        generation_results["string_tables"] = st_result

        # Cook labels
        pal_result = generate_all_primary_asset_labels(spec.primary_asset_labels)
        generation_results["primary_asset_labels"] = pal_result

        # Phase 3: Input mappings
        try:
            import unreal
            project_dir = unreal.Paths.project_dir()
            input_result = _write_input_mappings(
                spec.input_mappings.action_mappings,
                spec.input_mappings.axis_mappings,
                project_dir,
            )
            generation_results["input_mappings"] = input_result
        except Exception as e:
            generation_results["input_mappings"] = {"success": False, "error": str(e)}

        # Phase 4: Compile all generated Blueprints
        bp_paths = [f"{s.content_path}/{s.name}" for s in spec.blueprints]
        compile_results = compile_all_blueprints(bp_paths)

        # Phase 5: Write manifest
        manifest_path = write_manifest(
            run_id=run_id,
            prompt=prompt,
            spec_dict=spec_dict,
            generation_results=generation_results,
            compile_results=compile_results,
            output_path=output_dir,
        )

        # Build summary counts
        _new_results = [
            bb_result, bt_result, eqs_result,
            attn_result, sc_result, mix_result,
            seq_result, st_result, pal_result,
        ]
        total_assets = (
            bp_result["total"] + widget_result["total"]
            + mat_result["total"] + data_result["total"] + level_result["total"]
            + sum(r["total"] for r in _new_results)
        )
        total_succeeded = (
            bp_result["succeeded"] + widget_result["succeeded"]
            + mat_result["succeeded"] + data_result["succeeded"] + level_result["succeeded"]
            + sum(r["succeeded"] for r in _new_results)
        )

        return {
            "success": True,
            "data": {
                "run_id": run_id,
                "feature_name": spec.feature_name,
                "genre": spec.genre,
                "total_assets": total_assets,
                "succeeded": total_succeeded,
                "failed": total_assets - total_succeeded,
                "compile": {
                    "total": compile_results["total"],
                    "compiled": compile_results["succeeded"],
                    "failed": compile_results["failed"],
                },
                "generation": generation_results,
                "spec_path": spec_path,
                "manifest_path": manifest_path,
                "acceptance_tests": spec.acceptance_tests,
            },
        }

    except Exception as e:
        import traceback
        return {
            "success": False,
            "data": {},
            "error": f"{e}\n{traceback.format_exc()}",
        }


def handle_prompt_status(params: Dict[str, Any]) -> Dict[str, Any]:
    """Return status of the PromptBrush system."""
    try:
        import unreal

        has_bp_builder = hasattr(unreal, "BlueprintGraphBuilderLibrary")
        has_widget_builder = hasattr(unreal, "WidgetBlueprintBuilderLibrary")
        output_dir = _get_output_dir()

        manifest_count = len(
            [f for f in os.listdir(output_dir) if f.startswith("manifest_")]
        )
        spec_count = len(
            [f for f in os.listdir(output_dir) if f.startswith("spec_")]
        )

        return {
            "success": True,
            "data": {
                "ready": has_bp_builder and has_widget_builder,
                "blueprint_builder_available": has_bp_builder,
                "widget_builder_available": has_widget_builder,
                "output_dir": output_dir,
                "manifests_on_disk": manifest_count,
                "specs_on_disk": spec_count,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_prompt_spec_list(params: Dict[str, Any]) -> Dict[str, Any]:
    """List previously generated specs from the output directory."""
    try:
        output_dir = _get_output_dir()
        specs = []

        for fname in sorted(os.listdir(output_dir)):
            if not (fname.startswith("spec_") and fname.endswith(".json")):
                continue
            fpath = os.path.join(output_dir, fname)
            try:
                with open(fpath, encoding="utf-8") as f:
                    data = json.load(f)
                specs.append({
                    "file": fname,
                    "path": fpath,
                    "feature_name": data.get("feature_name", "?"),
                    "genre": data.get("genre", "?"),
                    "description": data.get("description", "")[:100],
                })
            except Exception:
                specs.append({"file": fname, "path": fpath, "error": "Could not parse"})

        return {"success": True, "data": {"specs": specs, "count": len(specs)}}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}
