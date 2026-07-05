# PromptBrush

Generate complete Unreal Engine 4.27 gameplay systems from natural language prompts.

## What it does

Type a prompt like "Make me gameplay like Puzzle Fighter" and PromptBrush automatically creates:

- Blueprint classes: GameMode, GameState, PlayerController, Pawn, AI, managers, actors
- Widget Blueprints: main menu, HUD, pause screen, game over, score display, combo popup
- Materials: flat-color placeholders ready for art replacement
- Data assets, curves (CurveFloat stubs)
- Maps with placed directional lights, player starts, and post process volumes
- DefaultInput.ini action/axis mappings
- A JSON build spec and manifest of everything created

## Prerequisites

1. UE4.27 with the Python Editor Script Plugin enabled
2. `BlueprintGraphBuilder` plugin compiled and enabled in your project (see `Plugins/MCPBridge/Source/`)
3. UE Bridge Python listener running (`Plugins/MCPBridge/Content/Python/startup.py`)
4. MCP bridge running: `npm run build` then ensure `.mcp.json` is configured

## Plugin setup

1. Copy `d:/Unreal Projects/CodePlayground/Plugins/PromptBrush/` to your project's `Plugins/` folder
2. Enable in UE4 editor: Edit > Plugins > PromptBrush > Enable
3. Restart the editor
4. Open via: Window > Developer Tools > PromptBrush

## Using the editor tab

1. Open the PromptBrush tab (Window > Developer Tools > PromptBrush)
2. Type your prompt in the text box
3. Click **Run Generation** to create all assets
4. Click **Dry Run** to preview the build spec without creating anything
5. Watch the log output for progress and results

The tab sends requests to the Python listener on `localhost:8080`. UE4 must be running with the bridge active.

## Using from Claude Code (MCP tools)

```
prompt_status()
# Check that BlueprintGraphBuilder and WidgetBlueprintBuilder are loaded

prompt_generate(prompt="Make me gameplay like Puzzle Fighter")
# Creates 19 BPs, 10 Widget BPs, 6 materials, 16 data assets, 3 maps

prompt_generate(prompt="Create a main menu, HUD, pause screen, and game over flow")
# Creates 3 BPs, 5 Widget BPs, 1 map

prompt_generate(prompt="Make me gameplay like Puzzle Fighter", dry_run=true)
# Returns the build spec JSON without creating any assets

prompt_spec_list()
# List all previously generated specs on disk
```

## Supported genres

| Prompt keywords | Genre | Assets created |
|---|---|---|
| puzzle fighter, falling blocks, tetris, combo | puzzle_fighter | 19 BPs, 10 Widgets, 6 Materials, 16 Data Assets, 3 Maps |
| main menu, pause, hud, game over, ui flow | menu_system | 3 BPs, 5 Widgets, 1 Map |
| anything else | generic | 2 BPs, 1 Map |

## Output location

- Generated UE4 assets: `/Game/Generated/<FeatureName>/`
- Build specs (JSON): `<ProjectDir>/PromptBrushOutput/spec_<run_id>.json`
- Manifests (JSON): `<ProjectDir>/PromptBrushOutput/manifest_<run_id>.json`

## Architecture

```
Editor Tab (C++ Slate)
    |
    | HTTP POST /
    v
Python Bridge Listener (localhost:8080)
    |
    v
handlers/promptbrush.py
    |
    +-> generation/prompt_to_spec.py      # NL -> BuildSpec
    +-> generation/blueprint_generator.py # Creates BP assets
    +-> generation/widget_generator.py    # Creates WBP assets (via C++ library)
    +-> generation/asset_generator.py     # Creates materials, data assets
    +-> generation/level_generator.py     # Creates maps, places actors
    +-> generation/cpp_generator.py       # Writes .h/.cpp to disk
    +-> generation/compile_loop.py        # Compiles BPs with retry
    +-> generation/manifest.py            # Writes JSON manifest
```

## Adding a new genre

Edit `Plugins/MCPBridge/Content/Python/mcp_bridge/generation/prompt_to_spec.py`:

1. Add keywords to `GENRE_KEYWORDS`:
```python
"horror": ["horror", "pt ", "hallway trigger", "jump scare"],
```

2. Write the spec function:
```python
def _horror_spec(prompt: str) -> BuildSpec:
    spec = BuildSpec(feature_name="Horror", genre="horror", description=prompt)
    spec.blueprints = [
        BlueprintSpec("BP_HorrorGameMode", "GameModeBase", "/Game/Generated/Horror/Gameplay"),
        BlueprintSpec("BP_HorrorTrigger", "Actor", "/Game/Generated/Horror/Gameplay",
                      components=[{"name": "TriggerVolume", "class": "BoxComponent"}]),
    ]
    # ... add widgets, levels, etc.
    return spec
```

3. Register it:
```python
if genre == "horror":
    return _horror_spec(prompt)
```

## Files in this repo

```
Plugins/MCPBridge/Content/Python/mcp_bridge/
  handlers/promptbrush.py          -- prompt_generate, prompt_status, prompt_spec_list
  generation/
    spec_schema.py                 -- BuildSpec dataclasses
    prompt_to_spec.py              -- NL -> BuildSpec converter
    blueprint_generator.py         -- Blueprint asset creation
    widget_generator.py            -- Widget Blueprint creation
    asset_generator.py             -- Materials and data assets
    level_generator.py             -- Maps and actor placement
    cpp_generator.py               -- C++ file writer
    compile_loop.py                -- Blueprint compile and retry
    manifest.py                    -- Disk manifest writer
mcp-server/src/tools/promptbrush.ts -- MCP tool definitions
agents/                            -- Agent role files
skills/                            -- Reusable procedure files
```
