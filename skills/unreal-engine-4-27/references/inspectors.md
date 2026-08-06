# Inspectors

Every `*_inspect` tool, plus `puerts_nav_query` and `puerts_job_cancel`.

An inspector is the independent read half of a builder. It exists so a desired
state can be compared against actual state without a human opening the editor,
which is what makes a build verifiable rather than merely reported.

## The contract every inspector shares

State it once here so no tool description has to repeat it:

- **Read only.** No transaction is opened, the response carries no transaction
  id, nothing is compiled and nothing is saved.
- **The dirty claim is checkable.** The asset's package dirty flag is read before
  and after the work and reported as `package_dirty_before` /
  `package_dirty_after`, so a caller can verify that reading did not write rather
  than take it on trust.
- **Reading is allowed anywhere under `/Game` and `/Engine`**, unlike authoring,
  which is limited to `/Game/MCPGenerated/`.
- **Arrays are canonically ordered**, so two reads of an unchanged asset produce
  the same content and the same `structure_hash_sha1`. Object key order is the
  JSON serializer's and is not canonical; hash a key-sorted form if you are
  comparing runs byte for byte.
- **`unsupported_fields`** carries anything reflection could not express, rather
  than dropping it.

## Identity kinds

Every inspector reports an `identity_kind`. It is the answer to "can I match this
back to the spec that made it", and it differs per asset type because UE4.27
differs per asset type.

| `identity_kind` | Means | Inspectors |
|---|---|---|
| `observed` | The engine stores a stable id and the inspector reports it | `graph_inspect` (object name + NodeGuid), `scene_inspect` (actor object name), `material_inspect` (expression UObject name), `sequence_inspect` (the FGuid UMovieScene stores) |
| `derived` | No stable id exists, so the node is addressed by its traversal path | `widget_inspect`, `behavior_tree_inspect`, `anim_blueprint_inspect`, `eqs_inspect` (by index) |
| `authored_name` | The authored name **is** the binding key | `blackboard_inspect` |

For a `derived` identity, a renamed or reordered node is a different identity on
purpose. For every kind, the id a build spec wrote is not persisted, so an
inspected node cannot be matched back to the spec line that made it.

## `graph_inspect`

Reads a Blueprint back: parent class, SimpleConstructionScript components, member
variables, implemented interfaces, user functions, the graph list, and one graph
described in the shape `puerts_blueprint_build` writes.

Each graph node reports its builder node type (`BeginPlay`, `CallFunction`,
`Operator`, `VariableSet` and the rest of the vocabulary), or `null` with
`node_class` when the builder has no word for it, in which case the node is also
listed under `graph.unmapped_nodes`.

`params` holds the routing keys the node type keeps as fields, plus every input
pin default the caller authored, in the JSON shape a spec writes it. A struct pin
default other than vector, rotator or linear color is reported as its raw pin text
and named in `graph.lossy_pin_defaults`.

Canonical ordering here: nodes by NodeGuid, pins by direction then PinId,
connections by their endpoint identities, and components, variables, functions and
graphs by name.

It also returns `member_structure_hash_sha1`, the hash
`puerts_blueprint_member_patch` reports as `pre_member_hash` / `post_member_hash`.

## `widget_inspect`

Returns `parent_class`, `generated_class_path`, the root widget as a nested
hierarchy with `child_index` order, and per widget: name, class, `class_path`,
`is_variable`, editable properties and its slot.

A `CanvasPanelSlot` reports anchors, offsets, alignment, `z_order` and
`auto_size`, plus the position/size pair `puerts_widget_build`'s own report uses so
the two can be compared field for field.

`named_slots` carries content held by `INamedSlotInterface` hosts, which is **not**
reachable through panel children.

Also returns exposed variables, bindings, animations and `structure_hash_sha1`, a
canonical hash of identity/class/name/variable-flag in traversal order, so a text
edit does not read as a reshape.

## `material_inspect`

One tool answers for both Materials and Material Instances, because a caller
holding an asset path usually does not know which it has. `asset_kind` says which
one answered and the field names are the same either way. Full field list:
`material-tools.md`.

`structure_hash_sha1` excludes parameter **values** on purpose, so retinting an
instance does not read as a reshape of the material.

## `sequence_inspect`

Returns display rate, tick resolution, playback range, every possessable and
spawnable binding, every master and object track with its sections, and every
keyframe on every channel with its time, value and interpolation.

Frames are reported in **display-rate** frames, the numbers Sequencer shows and
the numbers `sequence_build` takes, with the raw tick values beside them under
`start_tick` / `end_tick` / `tick`. A caller never has to know the 60000-per-second
tick resolution to compare a read against a spec.

Canonical ordering: bindings by name then guid, tracks by binding then type then
property, sections by start frame, channels in engine channel order, keys by time.

`structure_hash_sha1` covers keyframe **values** as well as structure, because for
a sequence a key value is the content: a camera that stops moving is a different
sequence, not the same sequence with different text. This is the opposite choice
from `material_inspect`, deliberately.

A possessable resolves to a level actor by name when the editor world holds one,
and reports `resolved: false` with the stored binding reference when it does not,
rather than omitting it.

## `audio_inspect`

Reports the common `USoundBase` fields and every `EditAnywhere` property by
reflected name, so a Sound Wave's `NumChannels`, `SampleRate`, `SoundGroup`,
`CompressionQuality`, `bLooping` and `bStreaming` come back without the command
carrying a field list that would go stale. Sound Cue graph detail:
`ai-input-audio-tools.md`.

## `anim_blend_space_inspect`

Returns the target skeleton, `blend_space_class` (which is what tells 1D from 2D,
since UE4.27 exposes no dimension count), all three axes with `display_name`,
`min`, `max` and `grid_divisions`, and every sample with its animation, x/y/z
position and `rate_scale`.

Samples are **sorted** by position and animation rather than reported in array
order, because a blend space's array order carries no meaning. A sample with no
animation is reported as a warning rather than a silent row: it contributes
nothing at runtime.

`puerts_anim_blend_space_build` creates new Blend Spaces. There is no
update-in-place counterpart because UE4.27 rebuilds the triangulation from the
whole sample set, and an update tool would need an atomic sample-set replacement
UE4.27 does not offer.

## `ai_controller_inspect`

Returns parent class, generated class path, every `AIPerceptionComponent` the
Blueprint declares with each configured sense, the dominant sense class, and every
`RunBehaviorTree` call site in its graphs with the Behavior Tree and that tree's
Blackboard.

### Why call sites

UE4.27 has no data-driven field pointing a controller at a tree. A controller
starts one by calling `AAIController::RunBehaviorTree`, so the wiring lives in a
graph node.

A call site whose `BTAsset` pin carries a literal reports that asset. One whose
pin is wired from a variable resolves to nothing at edit time and is listed under
`dynamic_behavior_tree_call_sites` rather than guessed at. A controller with no
call site at all is reported as a warning naming `puerts_blueprint_build` and the
exact node to author.

Only components this Blueprint declares in its SimpleConstructionScript are
visible. One inherited from a native C++ parent lives on the parent CDO, which
this reader does not walk, and the response says so.

## `eqs_inspect`

Returns query name, options in evaluation order, each option's generator class
with its properties, and each test with its class and every scoring and filtering
property (`TestPurpose`, `ScoringEquation`, `ScoringFactor`, `FilterType` and the
rest).

### There is no `eqs_build`, and that is a decision rather than a gap

`UEnvironmentQueryGraph::UpdateAsset` opens with
`Query->GetOptionsMutable().Reset()` and rebuilds `Options` entirely from the
editor graph, which makes `Options` a **compiled artifact** rather than the source
of truth. A builder that wrote `Options` without also authoring the matching
`UEdGraph` would pass its own read-back and then be silently wiped the next time a
human opened the asset in the EQS editor, failing convergence and independent
verification at once. The response repeats this in `build_unsupported_reason`.

To use EQS from the bridge, author the query by hand in the editor and run it from
a Behavior Tree with the `RunEQS` service, which `puerts_behavior_tree_build`
already supports.

## `cloth_inspect`

Returns every render section with whether it has cloth bound and which clothing
asset, every clothing asset with its GUID, topology content hash and Physics
Asset, per-LOD physical vertex, triangle and fixed-vertex counts, the authoring
masks with their targets and value ranges, and the full NvCloth config (solver and
stiffness frequency, collision thickness, friction, self-collision radius,
stiffness and cull scale, tether stiffness and limit).

A re-front of `MCPBridgeClothOptimizer`, which was already compiled into the
plugin and reachable only through the legacy Python listener. Straight
pass-through: the snapshot is `UClothOptimizerLibrary::InspectClothAsset`'s,
unchanged, and the module's own in-editor panel reads the same function, so an MCP
read and what a human sees in that panel are the same output rather than two
implementations that can disagree.

### The read half only, deliberately

The module's three writers (`cloth_apply_fabric_profile`,
`cloth_smooth_max_distance`, `cloth_apply_lower_leg_gradient`) open a UE4
transaction and do not cancel it on the failure path, and what they mutate is a
skeletal mesh's cloth **paint**: authored data that a re-run cannot regenerate,
unlike a navmesh or a shader. A half-applied mask is lost work, not lost time.
They ship when a failed apply provably restores the mask; until then they are
reachable only through their legacy names, which carry a `confirm` parameter for
the same reason. The response repeats this in `write_unsupported_reason`.

A mesh with no clothing assets is answered, not refused, with a warning naming it
as a mesh with no cloth rather than a failed read.

## `nav_inspect`

Returns whether a navigation system exists at all, how many build tasks remain,
every `ANavigationData` actor with its agent config and generation settings
(`CellSize`, `CellHeight`, `AgentRadius`, `AgentHeight`, `AgentMaxSlope`,
`AgentMaxStepHeight`, `TileSizeUU` and the rest, read by reflected name so a
non-Recast nav data reports whatever it has), the navigation system's supported
agents, every `NavMeshBoundsVolume` and `NavModifierVolume` with its world-space
min/max box and area class, and the bounds the navigation system actually
**registered**.

The registered bounds are deliberately a separate list from the volumes, because
they disagree in the case that matters: a `NavMeshBoundsVolume` in an unloaded or
hidden sublevel is present as an actor and absent from the registered bounds,
which is the usual reason a level that looks like it has a navmesh does not.

A level with no bounds volume at all is reported as a warning naming that as a
level authoring gap rather than a query failure.

It reads the editor world, so it refuses during Play In Editor like every other
editor-only command.

## `nav_query`

Answers a batch of navigation queries against the editor world's navmesh in one
round trip.

| `kind` | Answers |
|---|---|
| `project` | Snaps a point onto the navmesh; reports the projected point and the distance it moved |
| `path` | Whether the end is reachable from the start, the path length, the path cost, and the straight-line distance for comparison |
| `raycast` | Walks the navmesh in a straight line; reports whether it was blocked and where |
| `random_point` | Picks a navigable point within a radius |

`reachable` is true only for `ENavigationQueryResult::Success`, and the raw result
word is also returned, because `Fail` ("there is no path") and `Error` or
`Invalid` ("the query could not be answered") are different problems. Collapsing
them into `false` would report a configuration fault as a level layout fault.

Batched on purpose: deciding where to place a patrol point or a spawn needs
several of these at once, and one call per point is the interface this bridge
exists to avoid. The whole batch is validated before the first query runs, so a
bad entry is a refusal rather than partial answers.

The response warns when the navmesh is still building, because those answers
describe a partial navmesh, and warns that `random_point` is not deterministic and
must not be used in a comparison.

Every kind is a const query on `UNavigationSystemV1`. Requires a navigation
system; run `nav_inspect` first if this refuses.

## `job_cancel`

Cancellation is not uniform and this tool does not pretend it is. Every answer
reports `cancel_effect` for that job:

| `cancel_effect` | Job | Mechanism |
|---|---|---|
| `immediate` | sequence render | it runs in a second process and the OS can kill it |
| `deferred` | lighting build | `GEditor->SetMapBuildCancelled` is a flag Lightmass reads at its next checkpoint |
| `deferred` | navigation build | `UNavigationSystemV1::CancelBuild` unwinds the Recast tile tasks already queued |

A deferred cancel does **not** interrupt an engine call already in progress, and
`stopped_now` in the response says which of the two happened.

It refuses rather than lying: `job_not_running` if the job already finished,
`cancel_unsupported` if that kind of work exposes no abort in UE4.27, and
`cancel_target_gone` if the thing being tracked is no longer reachable.

Nothing is rolled back. A cancelled navmesh is partial and the recovery is another
build; a cancelled render leaves the frames it already wrote on disk.

There is no cancel for work that blocks the game thread inside one engine call.
`puerts_nav_build` with `wait: true` is exactly that, which is why it returns no
`job_id`.
