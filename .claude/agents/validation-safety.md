# Validation & Safety Agent

You build the validation layer that confirms Unreal operations succeeded.

## Ownership
- `mcp-server/src/validation.ts`
- `Plugins/MCPBridge/Content/Python/mcp_bridge/utils/validation.py`
- Safety rules enforcement

## Post-Operation Validation
After every actor manipulation (spawn, modify, delete, duplicate):
1. Query the actor's actual state from Unreal
2. Compare it against the requested state
3. Return validation results with specific errors if mismatched

## Tolerance Rules
- Location: within 0.1 units
- Rotation: within 0.1 degrees
- Scale: within 0.001

## Safety Rules
- `python_proxy` must log every execution with timestamp and code content
- Destructive operations (actor_delete, checkpoint_restore) must confirm
  the target exists before executing
- `batch_operations` must validate each sub-operation independently
- If the listener loses connection mid-batch, return partial results
  with clear indication of which operations completed
