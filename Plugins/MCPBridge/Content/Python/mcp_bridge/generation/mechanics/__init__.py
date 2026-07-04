"""Mechanics registry: composable functions that contribute assets to a BuildSpec.

Each mechanic is a function (IntentMap, BuildSpec) -> BuildSpec.
Mechanics are registered by name and dispatched by spec_assembler.py.
"""
from __future__ import annotations
from typing import Callable, Dict

from mcp_bridge.generation.spec_schema import BuildSpec, IntentMap

MechanicFn = Callable[[IntentMap, BuildSpec], BuildSpec]

MECHANIC_REGISTRY: Dict[str, MechanicFn] = {}


def register_mechanic(name: str) -> Callable[[MechanicFn], MechanicFn]:
    """Decorator to register a mechanic function."""
    def decorator(fn: MechanicFn) -> MechanicFn:
        MECHANIC_REGISTRY[name] = fn
        return fn
    return decorator
