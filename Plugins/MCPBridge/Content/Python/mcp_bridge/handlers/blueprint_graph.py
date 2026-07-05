"""Blueprint member and graph editing handlers.

Thin Python bridge over the C++ UBlueprintInspectorLibrary (read) and
UBlueprintMutatorLibrary (write). All pin connections go through
UEdGraphSchema_K2::CanCreateConnection in C++; nothing here links pins
directly. Every successful mutation compiles and saves the Blueprint;
failed mutations return an explicit error and do not save.
"""

import json
from typing import Any, Callable, Dict, Optional, Tuple

from mcp_bridge.utils.transactions import transactional


def _fail(message: str) -> Dict[str, Any]:
    return {"success": False, "data": {}, "error": message}


def _load_blueprint(path: str) -> Tuple[Optional[Any], Optional[str]]:
    """Load a Blueprint asset by content path. Returns (blueprint, error)."""
    import unreal

    if not path:
        return None, "Missing 'blueprint_path'"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None, f"Blueprint asset does not exist: {path}"
    asset = unreal.load_asset(path)
    if asset is None:
        return None, f"Failed to load asset: {path}"
    if not isinstance(asset, unreal.Blueprint):
        return None, f"Asset is not a Blueprint (got {type(asset).__name__}): {path}"
    return asset, None


def _resolve_class(class_path: str) -> Tuple[Optional[Any], Optional[str]]:
    """Resolve a UClass from a path like /Script/Engine.Actor or /Game/BP.BP_C."""
    import unreal

    if not class_path:
        return None, "Missing class path"
    cls = None
    try:
        cls = unreal.load_class(None, class_path)
    except Exception:
        cls = None
    if cls is None:
        try:
            obj = unreal.load_object(None, class_path)
            if isinstance(obj, unreal.Blueprint):
                cls = obj.generated_class()
            else:
                cls = obj
        except Exception:
            cls = None
    if cls is None:
        return None, f"Could not resolve class: {class_path}"
    return cls, None


def _mutator() -> Tuple[Optional[Any], Optional[str]]:
    import unreal

    lib = getattr(unreal, "BlueprintMutatorLibrary", None)
    if lib is None:
        return None, ("BlueprintMutatorLibrary not available. The MCPBridge "
                      "C++ plugin is not loaded (check plugin build/enable state).")
    return lib, None


def _inspector() -> Tuple[Optional[Any], Optional[str]]:
    import unreal

    lib = getattr(unreal, "BlueprintInspectorLibrary", None)
    if lib is None:
        return None, ("BlueprintInspectorLibrary not available. The MCPBridge "
                      "C++ plugin is not loaded (check plugin build/enable state).")
    return lib, None


def _finalize(bp: Any, path: str, op_data: Dict[str, Any]) -> Dict[str, Any]:
    """Compile and save after a successful mutation. Failure to compile is
    reported explicitly; the asset is still saved so the state is inspectable,
    with compiled=False so callers must not treat it as done."""
    import unreal
    from mcp_bridge.handlers.blueprints import _compile_blueprint_ue427_safe

    compile_report = _compile_blueprint_ue427_safe(bp, path)
    saved = False
    save_error = None
    try:
        saved = bool(unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
    except Exception as exc:
        save_error = str(exc)

    data = dict(op_data)
    data["compile"] = compile_report
    data["saved"] = saved
    if save_error:
        data["save_error"] = save_error

    compiled_ok = bool(compile_report.get("compiled", False)) and not compile_report.get("had_errors", False)
    if not compiled_ok:
        return {
            "success": False,
            "data": data,
            "error": "Mutation applied but Blueprint failed to compile; see data.compile.errors",
        }
    if not saved:
        return {
            "success": False,
            "data": data,
            "error": f"Mutation compiled but save failed: {save_error or 'unknown'}",
        }
    return {"success": True, "data": data}


# ---------------------------------------------------------------------------
# Inspection (read-only)
# ---------------------------------------------------------------------------

_INSPECT_ACTIONS = {
    "graphs": ("list_graphs", []),
    "functions": ("list_functions", []),
    "variables": ("list_variables", []),
    "macros": ("list_macros", []),
    "interfaces": ("list_interfaces", []),
    "event_dispatchers": ("list_event_dispatchers", []),
    "components": ("list_scs_nodes", []),
    "nodes": ("list_nodes_in_graph", ["graph_name"]),
    "node_detail": ("get_node_detail", ["graph_name", "node_guid"]),
    "find_nodes": ("find_nodes", ["query_json"]),
}


def handle_blueprint_inspect(params: Dict[str, Any]) -> Dict[str, Any]:
    """Read Blueprint structure: graphs, members, components, nodes, pins.

    Args:
        params: blueprint_path (str), action (str, one of graphs/functions/
            variables/macros/interfaces/event_dispatchers/components/nodes/
            node_detail/find_nodes), graph_name (str, for nodes/node_detail),
            node_guid (str, for node_detail), query (dict, for find_nodes).

    Returns:
        Parsed JSON structure from the C++ inspector.
    """
    action = params.get("action", "")
    spec = _INSPECT_ACTIONS.get(action)
    if spec is None:
        return _fail(f"Unknown action '{action}'. Available: {sorted(_INSPECT_ACTIONS)}")

    bp, err = _load_blueprint(params.get("blueprint_path", ""))
    if err:
        return _fail(err)
    lib, err = _inspector()
    if err:
        return _fail(err)

    method_name, extra_params = spec
    args = [bp]
    for p in extra_params:
        if p == "query_json":
            args.append(json.dumps(params.get("query", {})))
        else:
            value = params.get(p, "")
            if not value:
                return _fail(f"Missing '{p}' for action '{action}'")
            args.append(value)

    method = getattr(lib, method_name, None)
    if method is None:
        return _fail(f"Inspector method not available: {method_name}")

    raw = method(*args)
    try:
        parsed = json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        return _fail(f"Inspector returned non-JSON payload: {raw[:200]}")
    return {"success": True, "data": {"action": action, "result": parsed}}


# ---------------------------------------------------------------------------
# Mutations
# ---------------------------------------------------------------------------

def _run_mutation(
    params: Dict[str, Any],
    invoke: Callable[[Any, Any, Dict[str, Any]], Tuple[bool, Dict[str, Any], Optional[str]]],
) -> Dict[str, Any]:
    """Common mutation flow: load blueprint, call C++, compile, save."""
    path = params.get("blueprint_path", "")
    bp, err = _load_blueprint(path)
    if err:
        return _fail(err)
    lib, err = _mutator()
    if err:
        return _fail(err)

    ok, op_data, err = invoke(lib, bp, params)
    if not ok:
        return _fail(err or "Mutation failed (C++ library returned failure); "
                            "check Output Log for LogBlueprintMutator details")
    op_data["blueprint_path"] = path
    return _finalize(bp, path, op_data)


@transactional("MCP Blueprint Add Variable")
def handle_blueprint_add_variable(params: Dict[str, Any]) -> Dict[str, Any]:
    """Add a member variable. 'type' is a TypeDescriptor dict:
    {category, sub_category?, sub_category_object?, container_type?}."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        var_type = p.get("type")
        if not name or not isinstance(var_type, dict):
            return False, {}, "Requires 'name' (str) and 'type' (TypeDescriptor dict)"
        ok = lib.add_variable(
            bp, name, json.dumps(var_type),
            json.dumps(p.get("default_value")) if p.get("default_value") is not None else "",
            p.get("category", ""),
        )
        return bool(ok), {"variable": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Remove Variable")
def handle_blueprint_remove_variable(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove a member variable by name."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name:
            return False, {}, "Requires 'name'"
        return bool(lib.remove_variable(bp, name)), {"variable": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Set Variable Default")
def handle_blueprint_set_variable_default(params: Dict[str, Any]) -> Dict[str, Any]:
    """Set a member variable's default value."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name or "default_value" not in p:
            return False, {}, "Requires 'name' and 'default_value'"
        ok = lib.set_variable_default(bp, name, json.dumps(p["default_value"]))
        return bool(ok), {"variable": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Add Function")
def handle_blueprint_add_function(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create a user function graph. inputs/outputs: [{name, type: TypeDescriptor}]."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name:
            return False, {}, "Requires 'name'"
        graph_name = lib.add_function(
            bp, name,
            json.dumps(p.get("inputs", [])),
            json.dumps(p.get("outputs", [])),
        )
        if not graph_name:
            return False, {}, f"AddFunction failed for '{name}' (name collision or invalid signature)"
        return True, {"function": str(graph_name)}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Remove Function")
def handle_blueprint_remove_function(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove a user function graph by name."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name:
            return False, {}, "Requires 'name'"
        return bool(lib.remove_function(bp, name)), {"function": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Add Event Dispatcher")
def handle_blueprint_add_event_dispatcher(params: Dict[str, Any]) -> Dict[str, Any]:
    """Create an event dispatcher. signature: {parameters: [{name, type}]}."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name:
            return False, {}, "Requires 'name'"
        result = lib.add_event_dispatcher(bp, name, json.dumps(p.get("signature", {"parameters": []})))
        if not result:
            return False, {}, f"AddEventDispatcher failed for '{name}'"
        return True, {"dispatcher": str(result)}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Remove Event Dispatcher")
def handle_blueprint_remove_event_dispatcher(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove an event dispatcher by name."""
    def invoke(lib, bp, p):
        name = p.get("name", "")
        if not name:
            return False, {}, "Requires 'name'"
        return bool(lib.remove_event_dispatcher(bp, name)), {"dispatcher": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Add Interface")
def handle_blueprint_add_interface(params: Dict[str, Any]) -> Dict[str, Any]:
    """Implement an interface. interface_class: /Script/... or /Game/....BPI_X_C."""
    def invoke(lib, bp, p):
        cls, err = _resolve_class(p.get("interface_class", ""))
        if err:
            return False, {}, err
        ok = lib.add_interface_implementation(bp, cls)
        return bool(ok), {"interface": p.get("interface_class")}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Remove Interface")
def handle_blueprint_remove_interface(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove an interface implementation (its function graphs are deleted)."""
    def invoke(lib, bp, p):
        cls, err = _resolve_class(p.get("interface_class", ""))
        if err:
            return False, {}, err
        ok = lib.remove_interface_implementation(bp, cls)
        return bool(ok), {"interface": p.get("interface_class")}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Remove Component")
def handle_blueprint_component_remove(params: Dict[str, Any]) -> Dict[str, Any]:
    """Remove an SCS component node; children re-parent to its parent."""
    def invoke(lib, bp, p):
        name = p.get("component_name", "")
        if not name:
            return False, {}, "Requires 'component_name'"
        return bool(lib.remove_scs_node(bp, name)), {"component": name}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Rename Component")
def handle_blueprint_component_rename(params: Dict[str, Any]) -> Dict[str, Any]:
    """Rename an SCS component node."""
    def invoke(lib, bp, p):
        old = p.get("old_name", "")
        new = p.get("new_name", "")
        if not old or not new:
            return False, {}, "Requires 'old_name' and 'new_name'"
        return bool(lib.rename_scs_node(bp, old, new)), {"component": new, "renamed_from": old}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Add Node")
def handle_blueprint_node_add(params: Dict[str, Any]) -> Dict[str, Any]:
    """Add a graph node. node_type is a registered factory name (CallFunction,
    Event, CustomEvent, VariableGet/Set, Branch, Sequence, Cast, SpawnActor,
    input nodes, switches, delegates, ...). config depends on node_type."""
    def invoke(lib, bp, p):
        import unreal
        graph = p.get("graph_name", "")
        node_type = p.get("node_type", "")
        if not graph or not node_type:
            return False, {}, "Requires 'graph_name' and 'node_type'"
        pos = p.get("position", [0, 0])
        guid = lib.add_node(
            bp, graph, node_type,
            unreal.Vector2D(float(pos[0]), float(pos[1])),
            json.dumps(p.get("config", {})),
        )
        if not guid:
            return False, {}, (f"AddNode failed for type '{node_type}' in graph '{graph}'; "
                               "check node_type spelling and config shape (see Output Log)")
        return True, {"node_guid": str(guid), "node_type": node_type, "graph": graph}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Delete Node")
def handle_blueprint_node_delete(params: Dict[str, Any]) -> Dict[str, Any]:
    """Delete a node by GUID; all pin links are broken first."""
    def invoke(lib, bp, p):
        graph = p.get("graph_name", "")
        guid = p.get("node_guid", "")
        if not graph or not guid:
            return False, {}, "Requires 'graph_name' and 'node_guid'"
        return bool(lib.delete_node(bp, graph, guid)), {"node_guid": guid}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Move Node")
def handle_blueprint_node_move(params: Dict[str, Any]) -> Dict[str, Any]:
    """Move a node to a new graph position."""
    def invoke(lib, bp, p):
        import unreal
        graph = p.get("graph_name", "")
        guid = p.get("node_guid", "")
        pos = p.get("position")
        if not graph or not guid or not isinstance(pos, (list, tuple)) or len(pos) != 2:
            return False, {}, "Requires 'graph_name', 'node_guid', 'position' [x, y]"
        ok = lib.move_node(bp, graph, guid, unreal.Vector2D(float(pos[0]), float(pos[1])))
        return bool(ok), {"node_guid": guid, "position": list(pos)}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Set Node Enabled")
def handle_blueprint_node_set_enabled(params: Dict[str, Any]) -> Dict[str, Any]:
    """Enable or disable a node by GUID."""
    def invoke(lib, bp, p):
        graph = p.get("graph_name", "")
        guid = p.get("node_guid", "")
        if not graph or not guid or "enabled" not in p:
            return False, {}, "Requires 'graph_name', 'node_guid', 'enabled'"
        ok = lib.set_node_enabled(bp, graph, guid, bool(p["enabled"]))
        return bool(ok), {"node_guid": guid, "enabled": bool(p["enabled"])}, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Connect Pins")
def handle_blueprint_pins_connect(params: Dict[str, Any]) -> Dict[str, Any]:
    """Connect two pins. Validated by UEdGraphSchema_K2::CanCreateConnection
    in C++ before linking; incompatible types fail cleanly."""
    def invoke(lib, bp, p):
        required = ("graph_name", "source_node_guid", "source_pin", "target_node_guid", "target_pin")
        missing = [k for k in required if not p.get(k)]
        if missing:
            return False, {}, f"Missing required params: {missing}"
        ok = lib.connect_pins(
            bp, p["graph_name"],
            p["source_node_guid"], p["source_pin"],
            p["target_node_guid"], p["target_pin"],
        )
        if not ok:
            return False, {}, ("ConnectPins failed: schema rejected the connection "
                               "(incompatible pin types, missing pin, or missing node); "
                               "see LogBlueprintMutator in Output Log")
        return True, {
            "connection": f"{p['source_node_guid']}.{p['source_pin']} -> {p['target_node_guid']}.{p['target_pin']}",
        }, None
    return _run_mutation(params, invoke)


@transactional("MCP Blueprint Break Pin Links")
def handle_blueprint_pins_break(params: Dict[str, Any]) -> Dict[str, Any]:
    """Break all links on a single pin of a node."""
    def invoke(lib, bp, p):
        graph = p.get("graph_name", "")
        guid = p.get("node_guid", "")
        pin = p.get("pin_name", "")
        if not graph or not guid or not pin:
            return False, {}, "Requires 'graph_name', 'node_guid', 'pin_name'"
        ok = lib.break_pin_links(bp, graph, guid, pin)
        return bool(ok), {"node_guid": guid, "pin": pin}, None
    return _run_mutation(params, invoke)
