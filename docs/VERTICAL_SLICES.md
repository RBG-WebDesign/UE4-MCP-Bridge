# Vertical slices

Seven one-prompt acceptance harnesses, one per authoring domain. Each asks the
same question in its own domain: can an agent go from a single prompt to a
finished, verified Unreal artifact without a human opening the editor?

The release-candidate gate in `docs/PROJECT_FINISH_SCOREBOARD.json` lists
"one-prompt vertical slices for gameplay, UI, AI, animation, materials and level
authoring: not attempted" as a NOT MET item. These are that item, plus
cinematics, which the gate did not list and which turns out to be the emptiest
domain of the seven.

**Every slice is expected to fail today, and the failures are the deliverable.**
A slice that passed would mean its domain was finished. What each failing run
returns instead is the exact list of primitives its domain is waiting on, named
precisely enough that a lane can go build one.

## Three verdicts, not one

A plain acceptance script has two outcomes and both of them are called "failed".
That is useless here, because the reason matters more than the result:

| Verdict | Exit | Means | Who acts |
|---|---|---|---|
| `PASS` | 0 | every step ran and every check held | nobody |
| `PRESENT_BUT_FAILING` | 1 | a registered tool was called and did not do what it says | the lane that owns the tool: this is a bug in shipped code |
| `BLOCKED_MISSING_PRIMITIVE` | 2 | a tool the slice needs is not in the catalog, or exists only behind the legacy HTTP opt-in | a lane has to build it: no editor run can change this |
| `UNPROVEN_NO_EDITOR` | 3 | the tools are registered and nothing called them, because no editor was addressable or the install gate refused | the integrator, with an editor |

Every step carries its own status, so one run reports the whole domain rather
than dying at the first undefined tool. A harness that stops at
`puerts_sequence_build is not a function` reports one gap per run; these report
all of them.

Missing primitives are further split, because writing a capability and porting
one that already runs are different sizes of job:

| State | Gap kind | Means |
|---|---|---|
| `MISSING` | `NEW` | nothing in either catalog has ever done this |
| `LEGACY_ONLY` | `PORT` | the capability runs today behind a human opt-in (`MCP_ENABLE_LEGACY_HTTP=1`, or `MCP_COMPAT_ALIASES=1`) and has no native front |
| `NOT_REGISTERED` | `REGISTRATION_BUG` | `docs/TOOL_INVENTORY.json` calls it native and `tools/list` does not carry it |

A slice claims a legacy equivalent by naming it; the harness does not take the
claim on trust. It looks the name up in the inventory, and a claim that does not
check out is reported as `legacy_claim_unverified` rather than believed. Two
details matter for that lookup and both were wrong in the first draft:

- **`tools/list` is the only authority on PRESENT.** The inventory is a document
  and can be stale; a tool a client cannot see is not usable regardless.
- **A name is not unique in the inventory.** `actor_spawn` appears twice, once as
  the legacy handler and once as the compat alias. The alias adds no capability
  (its own description says scale, name and folder "are refused rather than
  dropped"), so the legacy entry is what counts as evidence a capability exists.

Six of the sixteen gaps turn out to be ports. That is the most actionable
distinction in this document, and the reason for the `scene` group in particular
is worth stating plainly: **the native migration lost capability.** `actor_spawn`
(legacy) takes `name`, `folder` and `scale`. `puerts_spawn_actor` takes none of
them. `level_actors` (legacy) takes `include_transforms` and `folder_filter`.
`puerts_find_actors` takes neither. Four slices are blocked on a regression, not
on an unbuilt feature.

## Running them

```bash
npm run build                        # the harnesses drive mcp-server/dist
node Scripts/slice-gameplay.mjs      # warm phase
node Scripts/slice-gameplay.mjs --phase=cold   # after an editor restart
```

or `npm run slice:gameplay`, `npm run slice:ui`, and so on. There is no
run-them-all script on purpose: all seven currently exit non-zero, and the cold
phase needs an editor restart between the two runs.

After a run, `npm run slice:summary` derives the consolidated gap list from the
seven evidence files into `docs/evidence/slice-summary.json`. The ranked table at
the bottom of this document is that script's output pasted in, so the table and
the runs cannot drift apart. Do not hand-edit it; regenerate it.

Each run writes `docs/evidence/slice-<id>.json` (and `-cold.json`), in the same
shape as the other evidence files, with a `verdict`, a `steps` array, a
`missing_primitives` array carrying a proposed schema per request, and a `blocks`
list the scoreboard can consume without parsing prose.

The install gate is not optional. `requireCurrentInstall()` runs before anything
touches an editor, and a refusal keeps the run catalog-only rather than producing
evidence that looks live and is not. A catalog-only run is still useful: the
missing-primitive list is determined entirely from `tools/list` and the
inventory, with no editor involved.

The warm phase seals each artifact it produced (asset path, inspector, structure
hash, file sha256). The cold phase re-reads every sealed artifact after a restart
and requires the same hash and the same bytes on disk, following
`Scripts/bp-graph-patch-acceptance.mjs`.

## What no slice attempts

Every slice stops short of runtime observation and says so with a
`NOT_ATTEMPTED_BY_POLICY` step. AGENTS.md reserves PIE for the user to ask for,
and there is no primitive that observes a Blueprint executing, a Behavior Tree
ticking, an Animation Blueprint blending or a UMG widget drawing without entering
play. Every slice therefore proves authoring, and none of them proves behavior.
That is a platform statement, not a harness limitation, and it is the same gap in
all seven.

---

## 1. Gameplay

**Prompt.** "Make a beacon actor that counts how many times something walks into
it, lights up on begin play after a delay, and prove it works."

**Proves.** A Blueprint actor with two components, three member variables and a
fifteen-node event graph can be authored, compiled, read back by an independent
inspector, converged on a rerun, and placed in the level.

**Harness.** `Scripts/slice-gameplay.mjs`, asset `/Game/MCPGenerated/BP_SliceBeacon`.

Steps: build from one spec; inspect and compare components, variables, node count
and unmapped nodes; rerun the identical spec and require the structure hash and
the bytes on disk to be unmoved; place the beacon with a stable label; screenshot;
source-control quiescence via `p4 opened` before and after.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.** The Blueprint half is the closest
thing to green in the whole program: `puerts_blueprint_build` and
`puerts_graph_inspect` are both live_verified. Placement is what blocks it.

Missing: `puerts_scene_batch` (PORT).

Two things this slice is watching for that are not tool gaps. First, the one
connection whose pin role the builder vocabulary does not document is
`wait.Completed -> light.exec`: a latent Delay names its output exec pin
`Completed` and the vocabulary documents `then` as always Then. If exactly that
pair appears in `graph.unresolved_connections`, the gap is the vocabulary, not the
fixture. Second, step 3 checks that a converged rerun writes no new bytes;
`puerts_blueprint_build` has no documented no-save-on-converge behaviour the way
`blueprint_graph_patch` does, so a failure there is a real finding about
re-saving an unchanged asset.

## 2. UI

**Prompt.** "Build a beacon status HUD with a title, a charge bar and a toggle
button, bind the bar to the beacon's charge, and show it on screen."

**Proves.** A seven-widget UMG tree with canvas and box slots can be authored,
read back field for field, converged, bound to data, and displayed.

**Harness.** `Scripts/slice-ui.mjs`, assets `/Game/MCPGenerated/WBP_SliceHUD` and
`BP_SliceHUDDriver`.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.**

Missing: `puerts_widget_bind` (NEW).

Three findings this slice carries beyond the tool gap:

- **AGENTS.md is stale.** It states twice that `widget_build` has no inspector.
  `puerts_widget_inspect` is registered, classified live_verified, and has
  evidence at `docs/evidence/widget-inspect-2026-08-02.json`. The layout half of
  this slice is real today.
- **The read side of bindings exists and the write side does not.**
  `puerts_widget_inspect` returns a `bindings` array and an `is_variable` flag per
  widget. `puerts_widget_build` has no field that produces either. Every widget
  this bridge can author is therefore static and unreachable from a graph by
  `BindWidget`. The fix may be a new tool or two new fields on the existing
  builder; the harness proposes both.
- **The widget event graph is unmeasured.** Step 4 probes whether
  `puerts_graph_inspect` accepts a Widget Blueprint. If it does, button handlers
  are already authorable through `blueprint_graph_patch` and no new primitive is
  needed. If it refuses, a widget event graph needs its own patch primitive. This
  cannot be settled without an editor and it changes the size of the UI gap
  materially.

The slice also authors a driver Blueprint that calls
`WidgetBlueprintLibrary::Create` and `UserWidget::AddToViewport`, which is
authorable with tools that exist and proves the two builders compose.

## 3. AI

**Prompt.** "Give me a guard that patrols, chases whatever is in its TargetActor
blackboard key, and prove it is really wired up."

**Proves.** A Behavior Tree with three composites, four tasks and a decorator, a
Blackboard with three typed keys, an AIController that runs the tree, a pawn that
uses that controller, and navigation the MoveTo tasks can path on.

**Harness.** `Scripts/slice-ai.mjs`, assets `BT_SliceGuard`, `BB_SliceGuard`,
`BP_SliceGuardAI`, `BP_SliceGuardPawn`.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.**

Missing: `puerts_class_defaults_patch` (NEW), `puerts_scene_batch` (PORT),
`puerts_nav_build` (PORT of `ai_nav_rebuild`).

The tree and blackboard halves are live_verified, and lane K confirmed that
nothing in the AI lane authors the controller wiring:
`AAIController::RunBehaviorTree` is a plain graph call, so `puerts_blueprint_build`
is the authoring path and `puerts_graph_inspect` reading the `BTAsset` pin default
back off the controller is the verification. Lane K's
`puerts_ai_controller_inspect` is a purpose-built version of that same read; the
slice notes it and does not block on it.

`puerts_class_defaults_patch` is the interesting one. `AIControllerClass` and
`AutoPossessAI` are class defaults on the actor CDO. `puerts_blueprint_build`
writes component template properties and member variable defaults and has no
section for the CDO, so an authored pawn can never be possessed by an authored
controller. This is the narrowest missing primitive on the list and it is the one
that turns four working tools into a working feature. It may be a new tool or a
`defaults:` section on the existing builder.

`puerts_nav_build` is genuinely unowned: `ai_nav_rebuild` is legacy-only, and lane
K's `puerts_nav_inspect` and `puerts_nav_query` are read-only by design.

## 4. Animation

**Prompt.** "Give the guard an Animation Blueprint that blends idle, walk and run
off a Speed variable, and put it on the pawn's mesh."

**Proves.** A three-state machine with four transitions can be authored against a
real skeleton, read back, and attached to a SkeletalMeshComponent.

**Harness.** `Scripts/slice-animation.mjs`, asset `/Game/MCPGenerated/ABP_SliceGuard`.

The slice discovers the skeleton and the animations with `puerts_find_assets`
rather than hard-coding them, so a live run also reports whether the target
project has assets to build this from at all.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.**

Missing: `puerts_anim_blueprint_build` (PORT of `anim_blueprint_build_from_json`,
which the inventory's own notes call the highest-value refront in the catalog),
`puerts_anim_blueprint_inspect` (NEW), `puerts_anim_blueprint_patch` (NEW).

`UAnimBlueprintBuilderLibrary::BuildAnimBlueprintFromJSON` exists in C++ and
`anim_blueprint_build_from_json` exists behind the legacy HTTP opt-in, so the
capability is real and has no native front. Lane J owns the first two names and
has them settled; the third is a gap lane J is explicit about not filling. The
builder is create-only, so rerunning it against its own output is a refusal
rather than a no-op, and a second prompt about the same asset has nowhere to go.
That breaks the convergence property every other builder here has, and the harness
does not test for convergence because testing for it would be testing a promise
nobody made.

Attaching the result is authorable today: `AnimClass` is a component template
property and `puerts_blueprint_build` writes those.

## 5. Materials

**Prompt.** "Make an amber emissive beacon material with tunable colour and glow
strength, and put it on the beacon mesh."

**Proves.** A master material with named parameters, an instance that overrides
them, a read-back that agrees field for field, and the material on a mesh.

**Harness.** `Scripts/slice-materials.mjs`, assets `M_SliceBeacon`,
`MI_SliceBeacon`, `BP_SliceMaterialProbe`.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.**

Missing: `puerts_material_build` (NEW), `puerts_material_instance_build` (PORT of
`material_instance_create` plus `material_instance_set_params`),
`puerts_material_inspect` (PORT of `material_info`), `puerts_texture_import` (NEW).

`material_create` is deliberately NOT counted as an equivalent for
`puerts_material_build`. It creates a simple opaque surface from a fixed template
and authors no graph, and the inventory's own note on it records a generic
material graph builder as a known gap. Calling it a port would understate this
domain by a whole primitive.

Lane I owns the middle two and has them settled. `puerts_material_build` is
**unowned and stated as out of scope**: lane I will not author a master material
graph, lane B already recorded material graph structure as a platform gap with no
native command, and the nearest registered tool,
`puerts_sky_shader_create`, builds one specific hard-coded sky. Without it there is
no parent to instance, so the slice cannot start from the prompt. It degrades by
looking for any existing material to use as a parent, and records that this
proves the instance tool rather than the prompt: a parent nobody authored has no
guarantee of carrying any parameters to override.

`puerts_texture_import` follows from lane I's own schema: the instance builder
accepts a `textures` map and nothing in the catalog can produce a texture to put
in it. `docs/GAP_AUDIT-2026-07-29.md` already recorded "no texture tools at all".

Application is authorable today: `OverrideMaterials` is a component template
property that takes an array of asset paths.

Also recorded, not as a missing tool: shader compilation is not observable. No
tool reports whether the shader compiler has finished, so a screenshot taken
right after a material change can legitimately show the default checkerboard.

## 6. Levels

**Prompt.** "Lay out a lit courtyard: four pillars in a square, a directional
light, a sky light, a warm point light in the middle and a trigger volume by the
entrance, all filed under a Courtyard folder, then save the level."

**Proves.** A scene can be described as desired state, placed in one transactional
call, read back with transforms and folders by an independent inspector, and
saved.

**Harness.** `Scripts/slice-level.mjs`, eight actors in one batch.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE.**

Missing: `puerts_scene_batch` (PORT), `puerts_scene_inspect` (PORT of
`level_actors` plus `level_outliner`), `puerts_lighting_build` (NEW).

Lane L owns the first two and has them settled. The size of the gap is worth
stating plainly: `puerts_spawn_actor` takes a class path, a location and a
rotation, and nothing else. It cannot label an actor, cannot scale it, cannot file
it in an outliner folder, cannot set a volume's extent and cannot batch. Eight
separate spawns with no way to name what they produced is not a scene-authoring
primitive, and `puerts_find_actors` returns names and classes with no transform,
no folder, no bounds and no structure hash, so no placement claim can be verified
from it. The harness runs that degraded read anyway and records exactly what it
could not answer.

`puerts_lighting_build` is unowned and absent from both catalogs. Nothing triggers
a lighting build, so a screenshot of an authored scene shows unbuilt lighting.

The AGENTS.md trigger-volume rule is part of the slice rather than an
afterthought: step 1 finds the PlayerStart with `puerts_find_actors`, reads its
location with `puerts_read_property`, and requires the planned trigger position to
clear it by 1.5x the volume's extent. That check runs on registered tools and is
the one part of this slice a live editor could grade today.

This slice seals no artifact for the cold phase. The generic cold check re-reads
by `asset_path` and `puerts_scene_inspect` addresses the loaded level; proving a
level survives a restart needs a cold check written against `level_path`.

## 7. Cinematics

**Prompt.** "Make a three second intro: the camera pushes in on the beacon while
it fades up, then cut to black."

**Proves.** A LevelSequence with bindings, transform and float tracks, five
keyframes and a camera cut can be authored, read back, placed in the level and
rendered.

**Harness.** `Scripts/slice-cinematics.mjs`, asset `/Game/MCPGenerated/LS_SliceIntro`.

**Expected verdict: BLOCKED_MISSING_PRIMITIVE, and it is the emptiest of the
seven.** Nothing in the catalog touches Sequencer, and no lane in this program
owns it.

Missing: `puerts_sequence_build` (NEW), `puerts_sequence_inspect` (NEW),
`puerts_sequence_render` (NEW), `puerts_scene_batch` (PORT).

What exists in the repository and why none of it counts:

- `Plugins/MCPBridge/Content/Python/mcp_bridge/generation/sequence_generator.py`
  creates an **empty** LevelSequence and sets its playback range. No track, no
  section, no key, no binding. It is reachable only through the legacy
  `prompt_generate` pipeline behind `MCP_ENABLE_LEGACY_HTTP=1`.
- `title_sequence_bind` (legacy_http) writes a JSON scaffold file to disk and says
  so itself, at `handlers/titles.py:460`: "full event-track binding needs a
  dedicated Sequencer bridge tool".

The harness still writes out the full intended call, with two bindings, two
tracks, five keys and a camera cut track, so the shape of the slice is reviewable
now and executable the day the primitives land.

---

## Consolidated missing primitives

Sixteen distinct primitives across the seven slices, ranked by how many slices
each one blocks. Generated by `npm run slice:summary` from the evidence files.

| Slices blocked | Primitive | Gap | Owner | Blocks |
|---|---|---|---|---|
| 4 | `puerts_scene_batch` | PORT (actor_spawn, batch_spawn, actor_organize) | lane L (settled, implemented_unverified) | ai, cinematics, gameplay, level |
| 1 | `puerts_anim_blueprint_build` | PORT (anim_blueprint_build_from_json) | lane J (settled, C++ uncompiled) | animation |
| 1 | `puerts_anim_blueprint_inspect` | NEW | lane J (settled, ships first, C++ uncompiled) | animation |
| 1 | `puerts_anim_blueprint_patch` | NEW | UNOWNED | animation |
| 1 | `puerts_class_defaults_patch` | NEW | UNOWNED | ai |
| 1 | `puerts_lighting_build` | NEW | UNOWNED | level |
| 1 | `puerts_material_build` | NEW | UNOWNED. Lane I ships the instance and the inspector only | materials |
| 1 | `puerts_material_inspect` | PORT (material_info) | lane I (settled, implementation in progress) | materials |
| 1 | `puerts_material_instance_build` | PORT (material_instance_create, material_instance_set_params) | lane I (settled, implementation in progress) | materials |
| 1 | `puerts_nav_build` | PORT (ai_nav_rebuild) | UNOWNED | ai |
| 1 | `puerts_scene_inspect` | PORT (level_actors, level_outliner) | lane L (settled, implemented_unverified) | level |
| 1 | `puerts_sequence_build` | NEW | UNOWNED. No lane in this program covers Sequencer | cinematics |
| 1 | `puerts_sequence_inspect` | NEW | UNOWNED | cinematics |
| 1 | `puerts_sequence_render` | NEW | UNOWNED | cinematics |
| 1 | `puerts_texture_import` | NEW | UNOWNED | materials |
| 1 | `puerts_widget_bind` | NEW | UNOWNED | ui |

Six ports, ten new. Ten of the sixteen are unowned.

Reading that table by count alone understates it. Four groupings matter more
than the ranking:

1. **`puerts_scene_batch` is the single highest-value primitive in the program,
   and it is a port.** It is the only one four slices need, and every one of
   those four needs it for the same reason: an authored asset has to end up in a
   level with a name before anything can be verified or looked at. The
   capability shipped in `actor_spawn` and `batch_spawn` and did not survive the
   native migration. Lane L has it settled and uncompiled.

2. **The six ports are the cheap half of the roadmap.** `puerts_scene_batch`,
   `puerts_scene_inspect`, `puerts_anim_blueprint_build`,
   `puerts_material_instance_build`, `puerts_material_inspect` and
   `puerts_nav_build` all have working Python behind the HTTP opt-in. The
   inventory calls `anim_blueprint_build_from_json` the "highest-value REFRONT"
   in its own notes. None of these needs a capability designed, only fronted.

3. **Cinematics is the only domain that is entirely new work and entirely
   unowned.** Three of its four primitives exist nowhere, and the fourth is the
   scene port. It is also the only domain with no lane.

4. **`puerts_class_defaults_patch` is the cheapest new tool on the list.** It may
   be a `defaults:` section on a builder that already exists rather than a new
   tool, and without it the AI slice's four otherwise-working tools cannot be
   assembled into a pawn that a controller possesses.

Two of the sixteen are declined by the lane closest to them, which makes them
policy decisions rather than backlog: `puerts_material_build` (lane I ships the
instance and the inspector only) and `puerts_anim_blueprint_patch` (blocked on the
`UAnimBlueprintBuilderLibrary` rebuild path appending instead of replacing, the
same failure-atomicity problem `BPMutatorHelpers` is being fixed for). Both should
be recorded as accepted gaps or reassigned, not left implicit.

## What a live editor is needed to settle

The missing-primitive lists above are complete and final without an editor: they
come from `tools/list` and `docs/TOOL_INVENTORY.json`. Everything below is
`UNPROVEN` in the current evidence and cannot be graded from a catalog.

- **Whether the registered builders actually satisfy their slices.** Gameplay, UI
  and AI each have a majority of steps against live_verified tools that have never
  been run in this combination. `PRESENT_BUT_FAILING` is a verdict none of the
  seven has produced yet, because nothing has been called.
- **The Delay output pin role** (`wait.Completed` vs `wait.then`) in the gameplay
  fixture. This decides whether latent nodes are wirable by the documented
  vocabulary.
- **Whether `puerts_graph_inspect` accepts a Widget Blueprint.** This changes the
  size of the UI gap by a whole primitive.
- **Whether `puerts_blueprint_build` re-saves a converged asset.** The rerun check
  compares bytes on disk; nothing has measured it.
- **Whether the target project contains a Skeleton and three AnimSequences.** The
  animation slice cannot distinguish a missing tool from an empty project without
  running its discovery steps.
- **Source-control quiescence.** `p4` was unavailable in every run so far, so
  every `p4 opened` comparison is recorded as not measured rather than as passing.
- **Every cold phase.** No warm run has sealed an artifact, so no cold run has had
  anything to compare against.
