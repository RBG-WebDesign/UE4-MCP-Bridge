# WBP Glitch Effect (per-widget VHS glitch)

Status: Verified 2026-07-06
Engine: UE4.27
Origin: Sinfeld_Demo, `M_TitleGlitch_UI` + `ASitcomTitleController` glitch layer

## 1. System Design Intent

Apply a VHS signal-dropout glitch (scanline tears, chromatic split, tracking
noise, grain) to individual UMG widgets - live TextBlocks and Borders, not
textures - with a fully transparent background so only the letterforms
distort. The widget is wrapped in a `URetainerBox` at runtime; the retainer
renders its children to a render target and draws it through a UI-domain
material whose custom HLSL displaces the widget's own pixels and derives
output alpha from the displaced samples. A controller drives the material's
scalar parameters per frame along an envelope, so the glitch bursts while
the widget is still fully visible and holds through its fade.

## 2. Dependencies

- Content: `M_TitleGlitch_UI` (Material, Domain=UI, Blend=Translucent,
  one Custom HLSL node; buildable headlessly via MaterialEditingLibrary)
- Engine APIs (verified in 4.27):
  - `URetainerBox` (Components/RetainerBox.h): `SetEffectMaterial`,
    `SetTextureParameter`, `GetEffectMaterial` (returns the MID),
    defaults bRetainRender=true / RenderOnPhase=true / Phase 0/1
    (renders every frame)
  - `UMaterialInstanceDynamic::SetScalarParameterValue`
  - `MaterialEditingLibrary` (python): create_material_expression,
    connect_material_expressions, connect_material_property,
    recompile_material
- Material parameter contract (names the controller writes):
  `Signal Distortion Intensity`, `Tracking Noise Level`,
  `Random Horizontal Offset Strength`, `Chromatic Distance`,
  `White noise intensity`, `Seed (0-255)`, texture param `Display Input`

## 3. How-To Graph Logic

Material (single Custom node, output float4, inputs Tex/UV/Distortion/
Tracking/HOffset/Chroma/Noise/Seed):

```
d = saturate(Distortion * 0.04)
per-scanline hashes h1,h2 from floor(uv.y * 96) + Seed
uv.x += (h1-0.5) * HOffset * 0.3                  subtle ever-present jitter
tearGate = step(0.97 - d*0.05, h2)                ~3-8% of lines tear
uv.x += (h2-0.5) * HOffset * 6 * tearGate * d     violent tears
band = tracking roll band; uv.x += band jitter
sample R at uv+Chroma, G at uv, B at uv-Chroma    chromatic split
alpha = max(alpha of the three taps)              <- transparency preserved
col += grain * Noise * alpha                      grain on glyphs only
return float4(col, alpha)
```

All params at 0 = exact passthrough (widget renders pixel-identical).

Controller envelope (per cue/widget):

```
GlitchStart = fadeoutStart - GlitchLeadFrames (default 12)
amount: attack 0->1 across the lead frames (widget still fully opaque),
        hold at 1.0 through the fadeout
flicker: amount *= 0.55 + 0.45 * frac(sin(frame * 12.9898 + phase) * 43758.5453)
per tick: MID.SetScalar(param_i, strength_i * amount)
          MID.SetScalar("Seed (0-255)", (frame * 37) % 256)   fresh noise/frame
```

Critical design rule: peak glitch must land at FULL opacity. An envelope
that ramps across the fadeout puts maximum distortion at near-zero alpha,
which reads as nothing in motion (verified failure mode).

Runtime wrap (from the title controller, reusable for any widget):

```
capture canvas slot layout -> Canvas->RemoveChildAt(idx)
Retainer = WidgetTree->ConstructWidget<URetainerBox>(name)
Retainer->SetEffectMaterial(GlitchMaterial)
Retainer->SetTextureParameter("Display Input")
Retainer->SetContent(originalWidget)
NewSlot = Canvas->AddChildToCanvas(Retainer); restore layout on NewSlot
hide with Retainer->SetVisibility(Collapsed)  (skips the RT pass entirely)
```

## 4. Replication Steps

1. Build `M_TitleGlitch_UI` via python (MaterialEditingLibrary): UI domain,
   Translucent, TextureObjectParameter "Display Input", six scalar params
   (default 0), Custom node with the HLSL above, ComponentMask rgb ->
   EmissiveColor, ComponentMask a -> Opacity, recompile, save.
2. In the owning C++ (or a widget BP), wrap target widgets in RetainerBoxes
   at BeginPlay using the RemoveChildAt/AddChildToCanvas sequence.
3. Drive the MID each tick from your envelope. Keep a
   `TMap<FName, URetainerBox*>` (UPROPERTY Transient) for GC safety.
4. Expose tuning as EditAnywhere: master intensity, lead frames, flicker
   toggle, per-parameter strengths, optional per-item overrides
   (negative = inherit).
5. Reference strengths that read well in motion: SignalDistortion 25,
   TrackingNoise 0.8, HorizontalOffset 0.08, ChromaticDistance 0.008,
   WhiteNoise 0.35.

## 5. UE4.27 Legacy Gotchas

- Do NOT repurpose a full CRT-simulation UI material (e.g. M_CRT_UI) as the
  retainer effect: its opacity output covers the whole rect, producing a
  dark band behind the widget. Alpha must come from the displaced texture
  samples. Building a purpose-made material is cheaper than neutralizing 47
  foreign parameters.
- `URetainerBox::GetEffectMaterial` returns null until the Slate widget is
  constructed. A retainer that has been Collapsed since creation and is made
  visible in the same tick you query it will report no MID and render
  nothing in a same-frame screenshot. Give it one tick.
- Tune tear density conservatively. Displacing every scanline shreds text
  into unreadable confetti at full burst (verified failure mode); most
  lines must hold with only rare violent tears.
- MCP tool params typed loosely arrive as strings; `json.dumps("42.5")`
  produces a quoted literal and `FFloatProperty::ImportText` parses the
  quote as 0.0 while reporting success. The bridge unquotes JSON scalars
  for non-text properties (BPVariableOps.cpp) - keep that behavior.
- MaterialEditingLibrary setter return values lie in 4.27
  (`set_material_instance_scalar_parameter_value` returns False on
  success); verify by reading values back, and check untouched params
  return parent defaults to confirm the readback path itself works.

## 6. Verification

- Freeze at fadeout start (full opacity, peak glitch) using the title
  controller freeze recipe; `Shot SHOWUI`; confirm letterforms tear with
  chromatic fringing and the background stays transparent (scene visible
  between glyphs).
- Read the MID live:
  `retainer.call_method("GetEffectMaterial")` then
  `mid.get_scalar_parameter_value("Signal Distortion Intensity")` should
  equal strength * envelope(frame) within flicker bounds.
- At rest (before the lead window) the widget must be pixel-identical to
  the unwrapped version; any residue means a material param default is
  nonzero.
- Failure signatures: dark band = wrong material/alpha path; nothing
  happens = Slate reparent no-op or params never driven; confetti =
  tear constants too hot; glitch invisible in motion but present in
  stills = envelope peaks during fade instead of before it.
