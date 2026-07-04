# manifest.py -- Write and read generation manifests to disk.
from __future__ import annotations
import json
import os
import time
from typing import Any, Dict


def write_manifest(
    run_id: str,
    prompt: str,
    spec_dict: Dict[str, Any],
    generation_results: Dict[str, Any],
    compile_results: Dict[str, Any],
    output_path: str,
) -> str:
    """Write a generation manifest JSON file. Returns the absolute file path."""
    manifest = {
        "run_id": run_id,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "prompt": prompt,
        "spec": spec_dict,
        "generation": generation_results,
        "compile": compile_results,
    }
    os.makedirs(output_path, exist_ok=True)
    file_path = os.path.join(output_path, f"manifest_{run_id}.json")
    with open(file_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    return file_path


def read_manifest(file_path: str) -> Dict[str, Any]:
    """Read a manifest JSON file. Returns the parsed dict."""
    with open(file_path, encoding="utf-8") as f:
        return json.load(f)
