# Capability findings

Live probe results against the UE427PuerTSMCP editor. Each entry states what
was observed, with the reproduction. Phase P of docs/MASTERY_PLAN_2026-07-31.md
maintains this file; Phase L consumes it.

## Working

| Capability | Evidence (2026-07-31) |
|---|---|
| Object-reference reads | `read_property PlayerStart RootComponent` returns the component path string |
| Enum reads | `Mobility` returns the numeric value (0). No name; enhancement candidate |
| Empty array reads | `Tags` returns `[]` |
| Blueprint class spawn | `spawn_actor` with `/Game/MCPAcceptance/BP_TestActor.BP_TestActor_C` spawned and transacted, 12.3 ms |
| call_function with qualified names | `Actor.SetActorLabel ["ProbeRenamed"]` succeeded; `Actor.GetActorLocation` returns a proper `{x,y,z}` |
| Scalar property writes | `LightComponent0.Intensity` 5000 to 50000 and back via undo, verified by read-back |
| Targeted undo | `puerts_undo` with a transaction id reverted exactly that transaction |
| Full lifecycle | spawn, screenshot, modify, undo, delete, verify-gone exercised end to end |

## Defects and limitations (Phase L queue)

1. STRUCT READS RETURN `{}` (P1). `read_property CollisionCapsule.RelativeLocation`
   returns `{}`. The `Actor.GetActorLocation` native executor returns a correct
   `{x,y,z}`, so FVector serialization itself works; the defect is confined to
   the generic reflection read path (`ReadActorPropertyJson` C++ / runtime
   serializer).
2. STRUCTURED WRITES MANGLED (P1, same family). `set_property RelativeLocation`
   with `{"x":10,"y":20,"z":112}` fails with `value must be an object` (thrown
   by the compiled runtime validator, generated from puerts-runtime/src).
   `set_property Tags ["a","b"]` reaches C++ as a non-array:
   `LogJson: JsonValueToUProperty - Attempted to import TArray from non-array JSON key`.
   Net effect with (1): the reflection lane is scalars-only in both directions.
3. call_function requires QUALIFIED names (`Actor.GetActorLocation`); the bare
   name fails with `Function is not approved.` Undocumented. Default allowlist
   is 3 functions and each approved function also needs a hand-written native
   executor (`Approved function has no native executor`, service line ~735).
4. Failed and read-only commands still emit transaction ids and undo warnings.
   Read-only calls should not transact.
5. viewport_screenshot rejects full actor paths other tools return; matches
   short names only (task chip filed).
6. Default writable-property allowlist is 8 entries (Actor.bHidden/Tags/
   ActorLabel, SceneComponent.RelativeLocation/Rotation/Scale3D,
   LightComponentBase.Intensity/LightColor). Configurable via
   `[MCPPuerTSBridge]` ini keys `AllowedFunctions` / `AllowedWritableProperties`;
   neither surface is documented in docs/PUERTS.md.
7. No native map-load tool: the 17-tool catalog cannot open a different level.
   Blocks titled-map save probes and Phase F work on a persistent map.

## Untested

Non-empty array reads after (2) is fixed; map/set/FText/FName reads;
sky_shader_create rerun behavior; physics_build/observe; pie_start/stop round
trip; get_logs bounds; find_assets path/name filters; undo stack depth;
two-editor pipe isolation.
