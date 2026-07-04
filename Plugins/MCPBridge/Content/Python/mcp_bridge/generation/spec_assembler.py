"""Spec assembler: compose mechanics into a BuildSpec from an IntentMap."""
from __future__ import annotations
import importlib
import pkgutil
from typing import List

from mcp_bridge.generation.spec_schema import BuildSpec, IntentMap

# Auto-discover and import all mechanic modules to trigger @register_mechanic
import mcp_bridge.generation.mechanics as _mechanics_pkg
from mcp_bridge.generation.mechanics import MECHANIC_REGISTRY

for _importer, _modname, _ispkg in pkgutil.iter_modules(_mechanics_pkg.__path__):
    importlib.import_module(f"mcp_bridge.generation.mechanics.{_modname}")


def assemble_spec(intent: IntentMap) -> BuildSpec:
    """Create a BuildSpec from an IntentMap by dispatching registered mechanics.

    Iterates over intent.mechanics, looks each up in MECHANIC_REGISTRY, and
    calls the function. Unknown mechanics are skipped (logged as warnings in
    acceptance_tests). After all mechanics run, generates acceptance tests from
    the spec contents.
    """
    spec = BuildSpec(
        feature_name=intent.feature_name,
        genre=intent.genre,
        description=intent.description,
    )
    skipped: List[str] = []
    for mechanic in intent.mechanics:
        fn = MECHANIC_REGISTRY.get(mechanic.name)
        if fn is None:
            skipped.append(mechanic.name)
            continue
        spec = fn(intent, spec)
    spec.acceptance_tests = _generate_acceptance_tests(spec, skipped)
    return spec


def _generate_acceptance_tests(spec: BuildSpec, skipped: List[str]) -> List[str]:
    """Generate acceptance test strings based on what the spec contains."""
    tests: List[str] = []
    if spec.blueprints:
        tests.append(f"All {len(spec.blueprints)} Blueprint assets exist and compiled")
    if spec.widgets:
        tests.append(f"All {len(spec.widgets)} Widget assets exist")
    if spec.materials:
        tests.append(f"All {len(spec.materials)} materials exist")
    if spec.data_assets:
        tests.append(f"All {len(spec.data_assets)} data assets exist")
    if spec.levels:
        tests.append(f"All {len(spec.levels)} maps exist")
    if spec.input_mappings.action_mappings or spec.input_mappings.axis_mappings:
        tests.append("Input mappings written to DefaultInput.ini")
    if spec.blackboards:
        for bb in spec.blackboards:
            tests.append(f"{bb.name} blackboard exists")
    if spec.behavior_trees:
        for bt in spec.behavior_trees:
            tests.append(f"{bt.name} behavior tree exists")
    for name in skipped:
        tests.append(f"WARNING: mechanic '{name}' was not found in registry")
    return tests
