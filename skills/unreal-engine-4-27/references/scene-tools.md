# Scene, Level and Job Tools

Detail for `puerts_scene_batch`, `puerts_scene_inspect`,
`puerts_lighting_build`, `puerts_nav_build` and the `puerts_job_*` family.

## `scene_batch`

Applies a desired-state description of many actors to the editor's current level
in one transaction. A scene that would take fifty `puerts_spawn_actor` and
`puerts_set_property` calls is one call.

### Operation grammar

Exactly two op kinds, because a desired-state description of a level needs two:

```
{op:"upsert_actor", select?:{name|path|label}, label?, class?, location?,
    rotation?, scale?, folder?, tags?, attach_to?, properties?, components?}
{op:"delete_actor", select:{name|path|label}}
```

Lights, post-process volumes, trigger volumes, blocking volumes and nav mesh
bounds volumes are all `upsert_actor` with a class path and properties. There is
no separate lighting or volume tool.

- `class` is an actor class path, limited to `/Game/`, `/Script/Engine.`,
  `/Script/NavigationSystem.`, `/Script/CinematicCamera.CineCameraActor` and
  `/Script/LevelSequence.LevelSequenceActor`. It is required when the operation
  has to spawn, and an existing actor of a different class is a refusal, never a
  silent replacement.
- `location` and `scale` are `{x,y,z}`; `rotation` is `{pitch,yaw,roll}`. All
  world space. An omitted component keeps its current value.
- `folder` is an Outliner path such as `"Lighting/Interior"`.
- `tags` replaces the whole tag array.
- `attach_to` names a parent actor, or `""` to detach. An attachment cycle is
  refused.
- `properties` is `{propertyName: value}` on the actor. `components` is
  `{componentName: {propertyName: value}}` on components the actor **already**
  has: this command adds no components.
- Every property write is gated by the same `AllowedWritableProperties` allowlist
  `puerts_set_property` uses. One that is not on it is a refusal before anything
  is spawned.
- A `delete_actor` whose actor is already gone is satisfied, not an error.

### Identity

An actor is addressed by `select {name}` (the object name, unique within a
level), `{path}`, or `{label}`. A selector matching more than one actor is a
refusal that names the matches with their classes, never a guess.

An upsert with no `select` uses its `label` as the identity and spawns when no
actor has it, so one operation covers spawn and modify.

Two operations on the same actor in one batch is a refusal: there is no single
desired state to converge on.

### Ordering

The whole batch resolves and refuses before the first mutation. Each operation's
satisfied-ness is then re-evaluated immediately before it runs rather than read
from the plan, because a batch is ordered and an earlier operation may have moved
the state a later one depends on.

### The PlayerStart rule is automatic

A trigger volume that **contains** a PlayerStart refuses the whole batch, because
`OnBeginOverlap` never fires for a player who spawns already inside. One within
1.5x its own extent warns. The check runs against the actor's real bounds after
placement, inside the rollback boundary. `player_starts` is reported either way so
a caller can pre-check.

### Reporting and rollback

`plan_only` is read-only and returns `operations_to_apply`,
`unchanged_operations`, `expected_change_count`, `pre_structure_hash` and
`player_starts`. `predicted_structure_hash` is given only for a no-op batch.

Any failure cancels the transaction, runs the rollback boundary, and decides
whether the level actually came back by hashing it again rather than trusting the
undo; that answer is `rollback_succeeded`. Rerunning the same batch applies
nothing and reports `converged: true`.

`pre_structure_hash` and `post_structure_hash` are the same value
`scene_inspect` returns as `structure_hash_sha1`.

### It never saves

The level is left dirty in the editor and `level_package_dirty` says so. Writing
it to disk is `puerts_save`'s call, and keeping it out of here is what lets a
failed batch leave nothing on disk to clean up.

## `scene_inspect`

Reads the current level as JSON: every actor with its object name, label, class,
world transform, folder, tags, bounds, attachment and components, plus every
PlayerStart and a canonical `structure_hash_sha1`.

Actor identity is observed (`identity_kind: "observed"`): the id is the actor's
object name, which is unique within a level and stable, unlike the label, which is
neither.

Transforms are world transforms, the same space `scene_batch` writes, so the read
and the write cannot disagree about what a location means.

Arrays are canonically ordered (actors by object name, components by component
name), so two reads of an unchanged level produce the same content and the same
hash. The hash always covers the **whole** level: an `actors` filter narrows what
is reported and never what is hashed, because a hash of a filter is a hash of the
request.

`include_properties` reads reflected property names on every reported actor. A
property the actor does not have is absent rather than null, because null would
say it exists and holds nothing.

Read only: no transaction, no transaction id in the response, nothing saved, and
the level package's dirty flag is reported before and after the read.

## `level_path` is an assertion, not a target

On `scene_inspect`, `scene_batch`, `lighting_build` and the rest: it refuses when
the editor has a different level open rather than loading one, because an empty
success against the wrong map cannot be told apart from an empty map.

## `lighting_build`

Builds lighting so placed lights are baked rather than preview-only and the level
stops showing Unreal's "Lighting needs to be rebuilt" banner.

**It does not wait for the build.** A Lightmass build runs for minutes at the
higher quality levels, far past any request budget this bridge allows, so
`action: "start"` starts it and returns. The response carries `waited: false`.
Poll with `action: "status"` until `build_running` is false, and read
`lighting_unbuilt_objects` (the counter behind the editor's own banner) to see
whether the level still needs a rebuild. Screenshotting or saving before
`build_running` goes false captures the state mid-build.

What the start does block on is the scene gather and the Lightmass export, which
is seconds on a small level and longer on a large one.

`started` is read back from the editor rather than assumed. UE4.27 swallows a
refused build, so a start that Lightmass declined answers `started: false` with
the reason. The two common causes (World Settings `bForceNoPrecomputedLighting`,
and `r.AllowStaticLighting=0`) are refused by name before the build is asked for.

`quality` defaults to `Preview`, which is the quality to use when the point is to
see the scene lit at all. `Production` is minutes to hours and is a deliberate
choice, not a default.

Refused during Play In Editor: Lightmass reads `GWorld`, which is the play world
while play is running, so a build started then would gather the wrong scene.

There is no cancel: UE4.27 exposes no public entry point for aborting a build.

## `nav_build`

Rebuilds the editor world's navigation so a placed NavMeshBoundsVolume produces an
actual navmesh, then reads the world back with the same function `nav_inspect`
uses so the answer is not the build's own claim.

### Deliberately not transactional

Navmesh tiles are derived data generated by background tasks into a generator the
transaction buffer does not record, and UE4.27's own Build Paths calls
`GEditor->ResetTransaction("Rebuilding Navigation")` before triggering the same
build, discarding the undo stack outright. A transaction here would record an undo
entry that restores nothing. Nothing authored is at risk either way: a navmesh is
regenerated from the level, so the recovery from a bad build is another build.

### Time is the real decision

`wait` defaults to **false** and starts the rebuild without blocking, calling
`ANavigationData::RebuildAll` on every registered nav data and answering status
`"building"`. Poll `nav_inspect` until `remaining_build_tasks` is zero before
running `nav_query`, because a partial navmesh answers queries wrongly rather than
refusing them.

`wait: true` calls `UNavigationSystemV1::Build`, which is exactly what the editor
does and which blocks the game thread until every nav data finishes. On a large
level that outlasts the 30 second pipe deadline and you get a timeout for work
that is still running and will still finish. Use `wait: true` on small or test
levels, and reconcile with `nav_inspect` if it times out.

Only the blocking path spawns a missing RecastNavMesh actor, because the engine's
`SpawnMissingNavigationData` is protected and unreachable on its own. So
`wait: false` refuses a level that has bounds volumes and no nav data actor, and
names `wait: true` as the fix.

Every other precondition is refused by name before any generator runs: no
navigation system, no NavMeshBoundsVolume, a build lock other than the editor's
auto-update toggle, and `IsThereAnywhereToBuildNavigation` returning false. That
list exists because `UNavigationSystemV1::Build` returns silently when it has no
work, so a command that called it blind would report a successful build over a
level that still has no navmesh.

## Jobs

A job is work the engine advances on its own while the game thread is free: a
Lightmass build (`lighting_build`), a navigation rebuild (`nav_build` with the
default `wait: false`), or a sequence render (`sequence_render_start`). Those
commands return a `job_id`.

`job_status` reports a **stage** and live counters, never a percent, and says so
in the response: no UE4.27 entry point on any of these paths reports a completion
fraction. What you get instead is the counter the engine itself keeps,
`lighting_unbuilt_objects`, `remaining_build_tasks`, `output_file_count`, which
has no denominator.

`state` is `running`, `succeeded`, `failed` or `cancelled`. Every answer also
carries `cancel_supported` and `cancel_effect` for that job, because cancellation
is not uniform. Call with no `job_id` to list every job this editor session holds.

After an editor restart every job is gone and this says so: a job id from a
previous session answers `job_lost_editor_restarted` rather than being reported as
still running. A lighting build and a navigation build died with that editor; a
sequence render did not, because it is a separate process.

`job_result` collects the finished output **once**. The result outlives the
command that produced it: it is kept in the editor beside the job record, so a
client that lost the start command's response can still read what it answered. It
refuses with `job_still_running` if the job has not finished, and
`job_result_consumed` on a second call. The answer carries the job record (final
state, elapsed, live counters) and `start_result`, the body the starting command
returned.
