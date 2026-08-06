# Widget Tools

Detail for `puerts_widget_build`, `puerts_widget_bind` and
`puerts_widget_inspect`.

## `widget_build`

Creates or replaces a compiled UMG Widget Blueprint from one JSON widget tree.
The tree is a hierarchy of typed, named widgets. Each carries optional
widget-intrinsic properties and an optional `slot` object holding the layout its
parent owns.

The whole tree is validated before any asset is touched, so an unsupported widget
type, a duplicate name, a property the type does not have, or a child under a
leaf widget is a rejection rather than a half-built asset.

### The spec is the whole tree

Rerunning a spec converges by replacing the tree of the asset already at that
path. Unlike a Blueprint's components and variables, a widget tree has no
per-widget identity to merge against, so there is no partial update: the spec is
the whole tree.

Only `root` is required. The native builder also accepts an `animations` array,
which is not schema-checked at the client. The root widget's own `slot` is
ignored: it has no parent.

The response reports the hierarchy read back out of the built asset, not the
request, along with `generated_class_path`, which is the class a graph needs to
create the widget at runtime.

### Widget `properties` by type

All types take `visibility` (`Visible`, `Hidden`, `Collapsed`,
`HitTestInvisible`, `SelfHitTestInvisible`), `renderOpacity` and `isEnabled`.

| Type | Additional properties |
|---|---|
| `TextBlock` | `text`, `justification` (`Left`, `Center`, `Right`), `color` `{r,g,b,a}` in 0..1 |
| `RichTextBlock` | `text`, `justification` |
| `ProgressBar` | `percent` (0..1), `fillColorAndOpacity`, `barFillType`, `isMarquee` |
| `Slider` | `value`, `minValue`, `maxValue`, `stepSize`, `orientation` |
| `CheckBox` | `isChecked` |
| `EditableTextBox` | `text`, `hintText`, `isReadOnly` |
| `ScaleBox` | `stretch`, `stretchDirection`, `userSpecifiedScale` |

A name the widget type does not support rejects the whole spec.

`name` is unique across the whole tree. It is the widget's name in the asset, so
it is what a read-back and a `BindWidget` both address.

`children`: panel types take any number, content types take at most one, leaf
types take none. Array order is tree order.

## `widget_bind`

Binds widget properties to Blueprint functions or variables, and exposes widgets
as members of the generated class. This is what makes a UMG widget show live
data. A tree from `widget_build` displays the constants its spec gave it and
cannot be reached from a graph by name, because the Blueprint holds no bindings
and no widget is a variable.

### The delegate naming rule

UE4.27 drives the delegate named `<property>Delegate`, falling back to the bare
name for an event delegate. Write `Percent` on a ProgressBar and the engine binds
`PercentDelegate`. The response names the delegate each entry resolved to.

`property` is the property to bind, without the `Delegate` suffix: `Percent` on a
ProgressBar, `Text` on a TextBlock, `Visibility`, `IsEnabled`, `ToolTipText`. An
unbindable name is refused with the bindable ones on that widget class listed.

### Preconditions

- A property binding may only target a pure (`BlueprintPure` or const) function.
- The source function or variable must already exist on the Widget Blueprint.
  Author it with `puerts_blueprint_graph_patch` or
  `puerts_blueprint_member_patch` first.
- One entry per `(widget, property)`. UE4.27 allows a property to be bound once,
  so two entries for the same pair are refused rather than silently resolved.
- Either `bindings` or `expose_as_variable` must be present. A request that
  states no desired state has nothing to converge on, and with `remove_unlisted`
  it is the shape that means "take everything away".

### Binding implies exposing

The runtime resolves a binding's widget against the generated class members, so
binding a widget implies exposing it. The response lists which exposures that
rule added. `expose_as_variable` publishes widget names as members of the
generated class, which is what `BindWidget` and a graph reference by name both
need.

### Convergence and rollback

Desired state, not a sequence of edits: rerunning an identical spec applies
nothing, compiles nothing, saves nothing and reports converged.

Every entry is validated by UE4.27's own binding validator before anything is
written, so an unknown widget, an unbindable property, a missing source or an
incompatible type is a refusal with the closest matching names rather than a
half-bound asset. A compile that comes back `Error` restores the previous
bindings and variable flags and recompiles.

`remove_unlisted` (default false) converges downward as well as upward: it drops
bindings not listed and clears the variable flag on widgets not listed. It
applies only inside the sections the request actually stated, so a spec with
`bindings` and no `expose_as_variable` never unexposes anything. Clearing a
variable flag breaks every graph reference to that widget.

Verify with `puerts_widget_inspect`, which reports bindings and variables read off
the asset. It is the independent read half of `widget_build`, opens no
transaction, and uses the same field names as `widget_build`'s own report so the
two can be compared field for field.
