# AI, Input and Audio Tools

Detail for `puerts_behavior_tree_build`, `puerts_blackboard_build`,
`puerts_ai_perception_build`, `puerts_input_mapping_patch` and
`puerts_audio_build`.

## `behavior_tree_build`

Creates or updates a BehaviorTree asset with its Blackboard from one JSON spec in
one transaction: blackboard keys, blackboard assignment, and the full node graph,
then a compile-free save (Behavior Trees have no compile step).

The graph replaces the tree's root only when every node builds, so a failed spec
leaves an existing tree untouched, and a rerun of the same spec converges.

### Node structure

`{id, type, name?, params?, children?, decorators?, services?}`

| Kind | Types |
|---|---|
| Composites | `Selector`, `Sequence`, `SimpleParallel` |
| Tasks | `MoveTo`, `Wait`, `WaitBlackboardTime`, `RotateToFaceBBEntry`, `PlayAnimation`, `MakeNoise`, `RunBehavior`, `PlaySound`, `FinishWithResult`, `SetTagCooldown` |
| Decorators | `Blackboard`, `ForceSuccess`, `Loop`, `TimeLimit`, `Cooldown`, `CompareBBEntries`, `IsAtLocation`, `DoesPathExist`, `TagCooldown`, `ConditionalLoop`, `KeepInCone`, `IsBBEntryOfClass` |
| Services | `DefaultFocus`, `RunEQS` |

Unknown types are rejected before the asset is touched.

### `params`

Keys are snake_case: `blackboard_key`, `wait_time`, `acceptable_radius`,
`random_deviation`. **An unknown `params` key is currently dropped without a
warning**, so verify with `puerts_behavior_tree_inspect`.

Values are strings (`"5.0"`, `"TargetActor"`). A `params` key naming a blackboard
key is validated against the keys that exist.

### Blackboard

`blackboard_path` defaults to `<asset_path>_BB`. Point several trees at one path
to share a blackboard. `keys` are add-only here: existing keys with the same name
are left alone. Use `blackboard_build` to update or remove a key.

`root` is required: a Behavior Tree without nodes does nothing in PIE.

The response reports the keys actually on the blackboard asset, read back rather
than echoed.

### `behavior_tree_inspect`

Node identity is **derived** (`identity_kind: "derived"`): UE4.27 BT nodes carry
no GUIDs, so a node is addressed by its traversal path
(`parent/childIndex:Class:Name`). A renamed or reordered node is a different
identity on purpose. Unknown node classes are reported with their `class_path` and
properties rather than dropped.

It returns the tree class, blackboard path, the root composite as a nested
structure (`kind`: composite/task/decorator/service), `child_index` order,
decorators attached to the child they guard, `root_decorators`, per-node
`class_path` and name, editor-visible node properties, blackboard keys referenced
by nodes (from `FBlackboardKeySelector` fields), the blackboard's own key names
and types, counts per kind, `unsupported_fields` for anything reflection could not
express, and `structure_hash_sha1`.

## `blackboard_build`

Reconciles a whole Blackboard asset from one desired-state spec: keys with their
types, per-key instance sync, editor description and category, and the parent
blackboard.

`behavior_tree_build` already creates a blackboard and **adds** keys to it, and
that path is still the right one for a tree plus its blackboard. This command
owns the blackboard as an asset in its own right: it can update a key, remove
one, and set the parent, none of which add-only key creation can express.

### No key default value

UE4.27 has no such thing. `FBlackboardEntry` holds a name, an instanced key type,
an instance-sync flag and two editor-only strings. A key's value exists only on a
running `UBlackboardComponent`.

### Refusals

- A key that already exists under a **different** type is an error naming both
  types, never a silent retype, because retyping would drop every Behavior Tree
  selector bound to it.
- A key name owned by the parent chain is refused as protected:
  `UBlackboardData::IsValid` treats a name that collides with the parent as a
  broken asset.

An existing key of the same type is updated in the fields the spec names and left
alone in the fields it does not, so a partial spec is a partial update rather
than a reset.

`remove_unlisted` is opt-in and off by default. When set, keys not in the spec are
removed and the response warns that any Behavior Tree node selecting them is now
dangling, with `behavior_tree_inspect` named as the way to find them. Inherited
keys are never removed: they are not this asset's to remove.

`plan_only` returns `keys_to_add` / `keys_to_update` / `keys_to_remove` /
`unchanged_keys` / `protected_keys` and `expected_change_count` without creating
the asset even when it does not exist yet.

Applying runs in one transaction, reads every key back off the asset rather than
trusting the write, and saves only after that passes. Any failure cancels the
transaction and rolls an asset this command created back out of the Asset Registry
and off disk.

### `blackboard_inspect`

Key identity is **authored** (`identity_kind: "authored_name"`), which is the
opposite of `behavior_tree_inspect`. A blackboard key's name is what every
`FBlackboardKeySelector` binds to, so renaming a key is a different key and every
selector pointing at the old name is now dangling.

`key_count` is what this asset declares; `total_key_count` includes the parent
chain.

## `ai_perception_build`

Reconciles the whole `AIPerceptionComponent` configuration on an existing
AIController Blueprint in one call: which senses it has, each sense's properties,
and the dominant sense. The component is created if it is missing.

Desired-state rather than a set of setters, because a perception config is only
coherent as a whole. A `dominant_sense` that `senses` does not configure is
refused by name, not written: a dominant sense that is not configured never
reports anything.

A listed sense is replaced wholesale, so what lands is the spec plus that config
class's defaults and never the residue of an earlier spec. Every field other than
`sense` is checked against the config class by reflected name **before** anything
is written, so a misspelled property is a refusal rather than a value that
silently never applied.

### Sense properties

UE4.27 has six usable sense config classes: `Sight`, `Hearing`, `Damage`,
`Touch`, `Team` and `Prediction`. A Blueprint sense is deliberately not accepted,
because its user sense class cannot be validated here.

- `Sight` takes `SightRadius`, `LoseSightRadius`,
  `PeripheralVisionAngleDegrees`, `AutoSuccessRangeFromLastSeenLocation`,
  `PointOfViewBackwardOffset`, `NearClippingRadius` and
  `DetectionByAffiliation`.
- `Hearing` takes `HearingRange`, `LoSHearingRange` and
  `DetectionByAffiliation`.
- All senses take `MaxAge` and `bStartsEnabled`.
- `DetectionByAffiliation` is
  `{"bDetectEnemies":true,"bDetectNeutrals":false,"bDetectFriendlies":false}`.

The Blueprint must already exist and must derive from AIController: this command
configures a controller, it does not invent one. Create it with
`puerts_blueprint_build` first.

`remove_unlisted` is opt-in and off by default. `plan_only` is read-only and
returns `current_senses`, `senses_to_add` / `update` / `remove`,
`unchanged_senses`, `dominant_sense_changes` and `expected_change_count`.

## `input_mapping_patch`

Reconciles the project's input mappings against a desired set in one call. The
legacy lane spent one round trip per binding across three tools; a control scheme
is eleven bindings.

The whole desired set travels once, is classified against the mappings that exist
before anything is written, and a binding already present is reported in
`unchanged_operations` rather than reapplied, so a rerun applies nothing and
reports converged.

`preset` expands into the same actions and axes a caller could write by hand
(`first_person`, `third_person`, `top_down`, `tank`), so a preset can never mean
something the explicit form cannot express. Explicit entries are appended to the
preset's.

`remove_actions` and `remove_axes` name mappings to delete: `{name}` alone removes
every key bound to that name, `{name, key}` removes one.

`remove_unlisted` additionally prunes anything not in the desired set, but only
within the halves the request actually states, and it is refused outright when
neither `actions` nor `axes` is given, because a bare `remove_unlisted` would
erase the project's whole input configuration from a request that named nothing.

An unresolvable key name is refused by name before any mapping is written. Key
names are FKey string form: `W`, `SpaceBar`, `LeftMouseButton`, `MouseX`,
`Gamepad_LeftTrigger`. An axis `scale` defaults to 1.0; use -1.0 for the opposite
direction.

`plan_only` is read-only and returns `mappings_to_add`, `mappings_to_remove`,
`unchanged_operations`, `expected_change_count` and `pre_mapping_hash_sha1`.

### There is no transaction here

These are ini writes (`Config/DefaultInput.ini`), not asset writes, so there is no
UE4 transaction and no undo to trust. The boundary is a snapshot instead: both
mapping arrays are copied before the first write, restored exactly on any failure,
and `rollback_succeeded` reports whether a re-read of the mappings matches the
pre-patch hash.

`input_mapping_info` is the independent read half. Both arrays are canonically
sorted and `mapping_hash_sha1` is the same digest the patch reports as
`pre_mapping_hash_sha1` / `post_mapping_hash_sha1`, so a patch can be verified
against a read that did not perform it. The hash always covers the whole mapping
set, never the filtered view, and `action_total` / `axis_total` say how much a
filtered read did not show. Filters are exact matches, not substrings.

## `audio_build`

Creates or reconciles a Sound Cue as one desired-state node tree under
`/Game/MCPGenerated`. Supports Wave Player, Mixer, Random, Modulator, Delay,
Looping and Concatenator nodes, ordered child links and reflected editable
properties.

`first_node` and `children` are the playable truth. The native command calls
`LinkGraphNodesFromSoundNodes` to derive the editor graph after authoring.

The whole request is validated before mutation, including node ids, types, arity,
references, reachability, cycles, wave assets and property conversion.

Existing cues must be saved and clean so a package-file snapshot can restore a
failed replacement. New cues use the shared asset rollback boundary. Every
successful write is independently read back through `audio_inspect` before save.
`plan_only` mutates nothing, and a repeated matching request is a no-op. It does
not start PIE or play audio.

### `audio_inspect`

Reports the common `USoundBase` fields (duration, sound class, attenuation
settings) and every `EditAnywhere` property by reflected name, so a Sound Wave's
`NumChannels`, `SampleRate`, `SoundGroup`, `CompressionQuality`, `bLooping` and
`bStreaming` come back without this command carrying a field list that would go
stale.

For a Sound Cue it walks the whole node graph from `FirstNode` and returns each
node's id, class, title, ordered children and properties. The node array is
canonically sorted so `structure_hash_sha1` is stable across reads of an unchanged
cue. Each node's **children are deliberately not sorted**, because for a Mixer, a
Random or a Concatenator the child index is the meaning and sorting it away would
canonicalise out what the graph encodes.

Two things a caller cannot get any other way: a cue with no `FirstNode` is warned
about, because it plays nothing while looking like a valid asset; and nodes present
in `AllNodes` but not reachable from `FirstNode` are counted as orphans, because
they are stored in the asset, never play, and are invisible from either list on its
own.
