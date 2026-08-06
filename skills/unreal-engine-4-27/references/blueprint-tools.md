# Blueprint Tools

Detail for `puerts_blueprint_build`, `puerts_blueprint_graph_patch`,
`puerts_blueprint_member_patch` and `puerts_class_defaults_patch`.

The tool schemas carry the purpose, the required parameters and the hard
constraints. This file carries the node catalogs, the operation grammars and the
convergence and rollback semantics. Read it before authoring a Blueprint of any
size, and before the first `blueprint_member_patch` batch.

## Which tool

| Change | Tool |
|---|---|
| Whole asset from a spec: parent class, components, variables, event graph | `blueprint_build` |
| Individual nodes and pins on a graph that exists | `blueprint_graph_patch` |
| Variables, functions, macros, interfaces, dispatchers, components | `blueprint_member_patch` |
| Inherited class-default (CDO) values such as `AIControllerClass` | `class_defaults_patch` |

`blueprint_build` is desired-state over the whole asset. Changing one pin default
on a graph of forty nodes means restating all forty correctly or losing what was
not mentioned, which is why the two patch tools exist. `blueprint_graph_patch`
owns nodes and pins and cannot reach members. `blueprint_member_patch` owns
members and cannot reach nodes.

## `blueprint_build`

### Graph node types

`BeginPlay`, `Tick`, `ActorBeginOverlap`, `ActorEndOverlap`, `PrintString`,
`CallFunction`, `Operator`, `Delay`, `Branch`, `Sequence`, `Comment`, `Event`,
`CustomEvent`, `VariableGet`, `VariableSet`, `Cast`, `Select`, `Knot`,
`MakeStruct`, `BreakStruct`, `FormatText`, `SpawnActor`, `SwitchInt`,
`SwitchString`, `MultiGate`, `DoOnceMultiInput`, `InputKey`, `InputAction`,
`InputAxisEvent`, `MacroInstance`, `AddDelegate`, `RemoveDelegate`,
`CallDelegate`, `ClearDelegate`, `AssignDelegate`, `CreateDelegate`.

Anything else is rejected before the asset is touched. `blueprint_graph_patch`
adds no node types of its own: `add_node` accepts this same vocabulary.

### Input nodes

- `InputKey` binds a literal FKey through `params.fkey_name`, for example
  `"LeftShift"`, and needs no project input mapping.
- `InputAction` and `InputAxisEvent` require an existing project input mapping
  and are refused when it is missing. Author the mapping with
  `puerts_input_mapping_patch` first.

### Node `params`: routing keys

`params` holds the routing keys a node type needs, plus pin defaults by pin name.
Anything not listed below is treated as a pin default: a number for a float pin,
`true`/`false` for a bool pin, `{"x":0,"y":0,"z":0}` for a vector pin, an object
path string for an object pin.

| Node type | Routing keys |
|---|---|
| `CallFunction` | `class`, `function` |
| `Operator` | `op` (see the operator list below) |
| `VariableGet`, `VariableSet` | `var_name`, plus `scope`: `"self"` (default) or `"target"` with `target_class`. `VariableSet` also takes `value` as an alias for the pin named after the variable |
| `Delay` | the `Duration` pin |
| `Event` | `parent_class`, `event_name` |
| `CustomEvent` | `event_name`, `parameters` |
| `Cast` | `target_class`, `purity` |
| `MakeStruct`, `BreakStruct` | `struct_type` |
| `SpawnActor` | `actor_class` |
| `Sequence` | `num_outputs` |
| `SwitchInt` | `start_index`, `num_cases`, `has_default` |
| `SwitchString` | `case_values`, `has_default`, `is_case_sensitive` |
| `MultiGate` | `num_outputs`, `is_random`, `loop`, `start_index_from_zero` |
| `DoOnceMultiInput` | `num_inputs` |
| `InputKey` | `fkey_name`, `consume_input`, `execute_when_paused`, `override_parent` |
| `InputAction` | `action_name`, `consume_input`, `execute_when_paused`, `override_parent` |
| `InputAxisEvent` | `axis_name`, `consume_input`, `execute_when_paused`, `override_parent` |
| `MacroInstance` | `macro_bp`, `macro_name` |
| `AddDelegate`, `RemoveDelegate`, `CallDelegate`, `ClearDelegate`, `AssignDelegate` | `self_var_name`, or `delegate_owner_class` plus `delegate_name` |
| `CreateDelegate` | `selected_function_name` or `function_name` |
| `Comment` | `text`, `width`, `height` |

### `Operator` ops

```
not_bool, and_bool, or_bool
add_float, subtract_float, multiply_float, divide_float
greater_float, less_float, greater_equal_float, less_equal_float, equal_float
clamp_float, lerp_float
add_int, subtract_int, greater_int, less_int, equal_int
make_vector, add_vector, multiply_vector_float
append_string, vector_to_string, bool_to_string
```

Each is a verified `UKismetMathLibrary` or `UKismetStringLibrary` call. The
authority is `GetSupportedOperators()` in the builder.

### Connection pin roles

A connection is `{from: "nodeId.pinRole", to: "nodeId.pinRole"}`.

`"exec"` is direction-aware (Then on the source, Execute on the target). `"then"`
is always Then. Any other role is a literal pin name, so data pins wire through
the same array.

Literal names worth knowing:

| Node | Pins |
|---|---|
| `Branch` | `Condition`, `then`, `else` |
| `Sequence` | `then_0`, `then_1` (lower case, underscore) |
| `CallFunction` | `self` for its target, `ReturnValue` for its result, the UFUNCTION parameter name for everything else |
| `Operator` | `A` and `B` (or `X`/`Y`/`Z`, `Value`/`Min`/`Max`) and `ReturnValue` |
| `VariableGet`, `VariableSet` | the data pin is named after the variable. With `scope: "target"` both also take `self`, the object the variable lives on |
| `Delay` | `Duration` |
| `Cast` | `Object`, `then`, `CastFailed`, and `AsResult` for the cast result |
| `Knot` | `InputPin`, `OutputPin` |
| `MakeStruct`, `BreakStruct` | the struct pin is named after the struct |
| `MultiGate` | `"Out 0"`, `"Out 1"` |
| `SwitchInt` | `Default` plus one pin per case |
| `InputKey` | `Pressed`, `Released`, `Key` |
| `ActorBeginOverlap`, `ActorEndOverlap` | `OtherActor` |

`AsResult` exists because a Cast node's own pin name is `"As"` plus the target
type's display name, which a caller cannot compute for a Blueprint class. So
`AsResult` asks the node instead.

Two known limits: `DoOnceMultiInput` builds its pin names from localized text, so
it spawns but cannot be wired by name here. `SpawnActor`'s Spawn Transform is a
by-ref pin that needs a wired input.

### Component and variable specs

A `components[]` entry is `{class, name, attach_to?, properties?}`. `class` is a
reflected short name (`"StaticMeshComponent"`) or a full path
(`"/Script/Engine.PointLightComponent"`), and must derive from ActorComponent.
`attach_to` names a component declared earlier in the array or already on the
Blueprint; omit it to add at the root.

`properties` are reflected properties applied to the component template, by
property name:

```json
{"StaticMesh": "/Engine/BasicShapes/Cube.Cube",
 "RelativeScale3D": {"x":2,"y":2,"z":2}}
```

An asset reference is the object path as a string, and an array of them for a
list such as `OverrideMaterials`. `null` clears a reference. A property name the
component class does not have, or an asset path that does not resolve, rejects
the whole spec before the asset is touched.

A `variables[]` entry is `{name, type, default?, container?, category?}`. `type`
is one of `bool`, `byte`, `int`, `int64`, `float`, `string`, `name`, `text`,
`vector`, `vector2d`, `rotator`, `transform`, `linearcolor`, or a prefixed form:

```
object:StaticMeshComponent
class:/Script/Engine.Actor
struct:/Script/Engine.HitResult
enum:/Script/Engine.EComponentMobility
```

A prefixed class takes a reflected short name or a full path. `default` is the
value in the variable's own shape and is not accepted on an array or set
variable. `container` is `none` (default), `array` or `set`.

### Pin and connection rules

- Every connection must resolve to real pins. The response's
  `graph.connection_count` is the number of links actually made. Any shortfall
  against the number requested fails the build, with the dropped pairs named in
  `errors` and in `graph.unresolved_connections`. A graph with a hole in it is
  never saved.
- A pure node has no exec pins. A const `BlueprintCallable` UFUNCTION that
  returns a value is pure.
- A `Cast` result is addressed by the pin role `AsResult`.
- `VariableGet` and `VariableSet` take `scope: "target"` with `target_class` to
  read or write the variable on another object through its self pin. Those two
  are what a save/load round trip needs.
- `MacroInstance` and the delegate nodes expose their routing metadata through
  `puerts_graph_inspect`.

### Parent class

Any class Unreal allows a Blueprint of, including `UObject`, `SaveGame` and
`ActorComponent`, so a data-only Blueprint is authorable. Actor-only capability
is gated rather than the parent being refused: with a non-Actor parent the
`components` array and the `BeginPlay`, `Tick`, `ActorBeginOverlap`,
`ActorEndOverlap`, `InputKey`, `InputAction` and `InputAxisEvent` node types are
rejected by name before the asset exists. Variables and the parent-neutral node
types build normally.

### Convergence

Rerunning the same spec converges. The asset is loaded rather than duplicated, an
existing component or variable of the same name and type is left alone, and the
event graph is rebuilt from the spec. A component or variable that exists under a
different type is an error, never a silent retype.

`remove_unlisted` is opt-in downward convergence, off by default. Nothing is ever
removed unless a scope is set true. `variables` and `components` are implemented;
any other true scope is rejected. Removal only considers members previously
declared by this builder and stamped `MCPManaged`. Inherited, native, generated
and human-authored members are reported as protected. Variable graph references
require `force_remove_referenced`, and every deleted node is reported in
`convergence.removed_reference_nodes`. A component with graph references, bound
events or a retained child is always blocked.

`plan_only` reports current, desired, added, updated, removed, protected,
referenced and blocked state for variables and components, plus
`expected_change_count`. It returns before mutation, so no asset is created and
no package is dirtied.

## `blueprint_graph_patch`

`operations` is an ordered batch of `add_node`, `update_node`, `remove_node`,
`set_pin_default`, `connect_pins`, `disconnect_pins`, `move_node`,
`set_node_enabled` and `break_pin_links`. `set_node_enabled` takes an `enabled`
boolean; `break_pin_links` removes every connection on the named pin.

### Node selectors

Every node is addressed by a selector that must resolve to exactly one node:

- `node_guid`
- a structural combination of `type`, `node_class`, `var_name`, `function` and
  `position`
- `new_id`, naming a node added earlier in the same batch

A bare object `id` is refused. A UObject name changes whenever a node is
recreated, so a patch that targets by it edits whatever holds the name
afterwards.

A selector matching nothing, or more than one node, is a refusal and not a guess.
The whole batch resolves before anything mutates.

### Reporting

`plan_only` is read-only and reports `matched_nodes`, `unmatched_selectors`,
`ambiguous_selectors`, the per-kind change lists and `expected_change_count`.
`predicted_structure_hash` is given only for a no-op batch, where it is the
current hash by definition; `prediction_unavailable_reason` says so otherwise.

Applying runs in one transaction, compiles, reads the asset back independently,
verifies the change is really present, and saves only after that passes. Any
failure or mismatch rolls the whole batch back and leaves the graph as it was
found. Rerunning the same patch is a no-op that reports converged and does not
save.

## `blueprint_member_patch`

### Operation grammar

`operations` is an ordered batch. Each entry is `{op, ...}`:

```
{op:"add_variable", name, type:{category:"float"}, default?, category?}
{op:"remove_variable", name}
{op:"rename_variable", from, to}
{op:"set_variable_default", name, default}
{op:"set_variable_metadata", name, metadata:{category?, tooltip?, editable?,
    private?, expose_on_spawn?, replicated?}}
{op:"add_function", name, inputs?:[{name,type}], outputs?:[{name,type}]}
{op:"remove_function", name}
{op:"rename_function", from, to}
{op:"set_function", name, inputs?:[{name,type,default?,rename_from?}],
    outputs?:[{name,type,default?,rename_from?}], locals?:[{name,type,default?}],
    metadata?:{category?, tooltip?, pure?, const?, access?}}
{op:"add_macro", name, inputs?:[{name,type}], outputs?:[{name,type}]}
{op:"remove_macro", name}
{op:"rename_macro", from, to}
{op:"set_macro", name, inputs?:[{name,type}], outputs?:[{name,type}]}
{op:"add_interface", path}
{op:"remove_interface", path}
{op:"add_event_dispatcher", name, parameters?:[{name,type}]}
{op:"remove_event_dispatcher", name}
{op:"remove_component", name}
{op:"rename_component", from, to}
{op:"reparent_component", name, parent}
```

An empty `parent` on `reparent_component` makes the component root.

`type` is the same Type Descriptor `puerts_graph_inspect` reports for a variable.
A partial descriptor matches a variable whose reported type agrees on the fields
given.

For `set_function`, an omitted list is left unchanged and a supplied list is
authoritative. `rename_from` preserves a parameter rename and its call-site
connections; locals do not support it. `access` is `public`, `protected` or
`private`.

### Refusals

The whole batch is resolved and classified before the first mutation runs,
because each underlying mutator entry point recompiles the Blueprint. All of
these mutate nothing:

- an unloadable interface path
- a default value the variable's type cannot hold
- a rename onto a name already taken
- a `remove_variable` naming an inherited or native property
- two operations on the same member in one batch

There is no change-variable-type primitive, so `add_variable` against an existing
variable of a different type is refused by name rather than silently accepted.

### Reporting

An operation whose result is already present is reported in
`unchanged_operations` and not repeated, so a rerun applies nothing, reports
converged and leaves no dirty package.

`plan_only` returns `operations_to_apply`, `unchanged_operations`,
`expected_change_count` and `pre_member_hash`. `predicted_member_hash` is given
only for a no-op batch. Every `set_function` operation also returns a
`function_plans` entry naming `function_exists`, `signature_changes`,
`metadata_changes`, `locals_to_add`, `locals_to_update` and
`call_sites_affected`, so a signature change can be reviewed before mutation.

Applying runs in one transaction, compiles, re-reads every operation's own
condition from the asset rather than trusting the mutator's report, and saves
only after that passes. Any failure rolls the whole batch back, and whether the
rollback actually restored the members is decided by reading them again and
reported as `rollback_succeeded`.

`pre_member_hash` and `post_member_hash` are the same hash `puerts_graph_inspect`
returns as `member_structure_hash_sha1`, so a caller can verify a patch against
an independent read.

`compile_status` is accompanied by `compile_warnings` and `compile_errors`, the
actual `FCompilerResultsLog` messages. A batch whose operations are all already
satisfied still compiles, so a converged single-operation call is also how you
read any Blueprint's current compiler messages.

## `class_defaults_patch`

Sets inherited class-default (CDO) values as desired state: `AIControllerClass`
and `AutoPossessAI` on a pawn, and anything else on the bridge's writable-property
allowlist. `blueprint_build` writes component template properties and member
variable defaults and has no section for the actor's own class defaults.

A variable the Blueprint itself declares is refused here and pointed at
`blueprint_member_patch`: its default lives on the variable description as well
as on the CDO, and writing only one of the two leaves them disagreeing until the
next compile picks a winner.

### The transaction is deliberately not the rollback

`UObject::Modify` does nothing for an object that is not `RF_Transactional`, and
a class default object is not one, so a cancelled transaction leaves a CDO write
standing. This is measured, not assumed (finding 0r). The command snapshots every
property it will touch, restores them on any failure, decides
`rollback_succeeded` by reading the values again, and reports `Modify()`'s own
return value as `transaction_covers_cdo`. When that is false, editor undo will
not take this change back.

An invalid value is rejected on a scratch copy before the CDO is touched, and an
unknown property name is answered with the closest names on the class.

It writes the class default. Actors already placed in a level keep the value they
were placed with, and `puerts_scene_batch` is what changes those.

### Value shapes

`properties` is `{propertyName: value}`, each value in its own reflected shape. A
class reference is a class object path ending in `_C`, such as
`/Game/MCPGenerated/BP_Guard.BP_Guard_C`. An enum is its entry name, such as
`PlacedInWorldOrSpawned`. Every write is gated by the same
`AllowedWritableProperties` allowlist `puerts_set_property` uses.
