# Material Tools

Detail for `puerts_material_build`, `puerts_material_instance_build`,
`puerts_material_inspect` and `puerts_texture_import`.

The chain is: `texture_import` makes a texture, `material_build` authors the
parent material, `material_instance_build` overrides its parameters,
`material_inspect` reads either one back.

## `material_build`

Authors a UMaterial graph from one desired-state spec: expression nodes, named
scalar, vector, texture and static switch parameters, the links between the
nodes, and the links into `BaseColor`, `EmissiveColor`, `Roughness`, `Normal` and
the rest.

### The spec is the whole graph

A build aimed at an existing material replaces the graph it had, which is what
makes a rerun of the same spec converge on the same result.

### Why it is safe to mutate

UE4.27's `UMaterialEditingLibrary` graph mutators do not call `Modify()`, but the
engine's own material editor does not rely on them to: it opens a transaction and
calls `Material->Modify()` itself. This command does the same, checks the return
value, and refuses before writing anything if it comes back false. One `Modify()`
covers the whole build because every expression this command connects is one it
created in the same transaction, so the UMaterial is the only pre-existing object
it mutates.

On failure the transaction is cancelled, the rollback boundary runs, and whether
the material actually came back is decided by re-reading it: the Asset Registry on
a create, the structure hash on a replace.

The compile result is in the response (`compile.succeeded`, `compile.errors`)
rather than assumed, and the save happens only after an independent read-back
through `material_inspect` agrees that every requested link, material output and
parameter landed.

### Node ids

Node ids are yours and expression names are the engine's, so the response carries
a `nodes` array mapping each spec id to the `expression_id` `material_inspect`
will report.

### Spec shape

- `parameters` is sugar over `expressions`. A scalar becomes a
  `MaterialExpressionScalarParameter`; a switch becomes a
  `StaticSwitchParameter`, which also has `A` and `B` inputs. All of them share
  one id namespace with expressions. A parameter's `name` is also its node id
  unless `id` is given.
- A texture parameter's `default` is required: a texture sample with no texture
  does not compile. `puerts_texture_import` can generate one.
- `expressions[].type` is an expression class, short or full: `Multiply`,
  `Constant3Vector`, `TextureSample`, or `MaterialExpressionMultiply`. It must
  resolve to a concrete `UMaterialExpression` subclass.
- `expressions[].params` is property name to value on that node, for example
  `{"ConstA": 2.0}` on a Multiply or
  `{"Texture": "/Game/MCPGenerated/T_Grid"}` on a TextureSample. An unknown
  property is refused with the node's editable property names; an asset path that
  loads nothing is an error rather than a silent null.
- `connections[].from` is `"nodeId"` for its first output, or `"nodeId.RGB"`.
  `connections[].to` is `"nodeId.InputName"`, for example `"Glow.A"`. A wrong name
  is refused with the node's real input names.
- `outputs` maps a material property to a node id:
  `{"EmissiveColor": "Glow", "Roughness": "Rough"}`. Accepts `BaseColor`,
  `Metallic`, `Specular`, `Roughness`, `Anisotropy`, `EmissiveColor`, `Opacity`,
  `OpacityMask`, `Normal`, `Tangent`, `WorldPositionOffset`, `SubsurfaceColor`,
  `AmbientOcclusion`, `Refraction` and `PixelDepthOffset`, the same names
  `material_inspect` reports.
- `shading_model` defaults to `DefaultLit`. `Unlit` is the one an emissive-only
  material wants.

## `material_instance_build`

Creates or updates a `UMaterialInstanceConstant` and sets its scalar, vector,
texture and static switch parameters from one desired-state spec.

Rerunning the same spec converges: a parameter already at the requested value and
already overridden is reported unchanged and not rewritten, so a second run
dirties nothing.

Every parameter is resolved against the parent and validated before the asset is
created or touched, so a name the parent does not publish is refused with the
closest matching names rather than silently dropped.

On any failure the transaction is cancelled, the rollback boundary runs, and
whether the parameters actually came back is decided by reading them again rather
than by trusting the undo. The compile result is in the response
(`compile.succeeded`, `compile.errors`, and `instance_own_resource`, which is
false when the errors belong to the parent material and were not caused by this
request). The save happens only after an independent read-back agrees with every
requested value.

`parent_path` is required when the instance does not exist yet. On an existing
instance, supplying a different one reparents it.

`clear_unlisted` (default false) makes the spec the whole desired state:
overrides this instance carries that the spec does not mention are dropped. The
response reports `unlisted_overrides` either way, so a caller can see what this
would remove first.

A static switch change recompiles the instance's shader permutation, so the whole
batch is applied at once.

## `texture_import`

Generates a `UTexture2D` so a texture parameter has something to point at.
Nothing else in the catalog could produce a texture.

Generated, not imported from disk, and the name is the only part of that which is
historical. A solid colour or a checker needs no asset on disk and is
reproducible from the spec, which is what makes the command convergent: the same
spec produces identical source bytes, so a second run compares equal, reports
unchanged, writes nothing and reports no changed asset.

`source_file` is refused with a reason rather than quietly ignored. Reading an
arbitrary path off disk needs an allowed import root this bridge does not define,
and guessing one would be a filesystem escape. Use `pattern`, `color` and
`color_b`.

`width` and `height` are powers of two, 4 to 2048, default 256 (height defaults
to width). The cap is deliberate: generation runs on the game thread and a
placeholder does not need to stall the editor.

`srgb` defaults from `compression`: true for `Default`, false for `Normalmap`,
`Masks` and `Grayscale`. It changes the bytes written, not just a flag, so the
encoding and the setting agree.

The write is wrapped in a transaction, `Modify()` is called and its return value
checked, and the source pixels are read back and compared byte for byte before
anything is saved.

## `material_inspect`

Reads a Material or a Material Instance back as JSON. One tool answers for both
kinds because a caller holding an asset path usually does not know which it has.
`asset_kind` says which one answered and the field names are the same either way.

Always returns `parameters`: every scalar, vector, texture and static switch the
asset or its parent chain publishes, with type, effective value, and (for an
instance) whether the instance overrides it rather than inheriting it.

For a master material it also returns `domain`, `blend_mode`, `two_sided`, the
expression nodes with `class_path`, editor position and per-input connection
state, the connections between them, and `material_inputs`: which expression
drives `BaseColor`, `Metallic`, `Roughness`, `Normal` and the rest. For an
instance those arrays are present and empty, plus `parent_path` and
`base_material_path`.

`structure_hash_sha1` is a canonical hash of the parameter set, override flags,
expressions and links. Parameter values are excluded from it on purpose, so
retinting an instance does not read as a reshape of the material.

Expression identity is observed (`identity_kind: "observed"`): a material
expression's UObject name is unique in its package and is serialized, unlike a
UMG widget or a Behavior Tree node.

Read only: no transaction, nothing compiled or saved, and the package dirty flag
is reported before and after the read. Reading is allowed anywhere under `/Game`
and `/Engine`.
