# spec_schema.py -- BuildSpec dataclass definitions for PromptBrush generation pipeline.
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


@dataclass
class CppClassSpec:
    name: str                        # e.g. "GridManager" -- used as filename
    parent_class: str                # e.g. "Actor", "ActorComponent"
    content_path: str                # Blueprint lives here e.g. /Game/Generated/Gameplay
    source_path: str                 # relative to project Source/ e.g. Generated/
    properties: List[Dict[str, Any]] = field(default_factory=list)
    functions: List[str] = field(default_factory=list)
    generate_cpp: bool = False       # if True, write .h/.cpp; if False, Blueprint only


@dataclass
class BlueprintSpec:
    name: str                        # asset name e.g. BP_GridManager
    parent_class: str                # e.g. Actor, GameModeBase
    content_path: str                # e.g. /Game/Generated/Gameplay
    components: List[Dict[str, Any]] = field(default_factory=list)
    variables: List[Dict[str, Any]] = field(default_factory=list)
    graph_json: Optional[Dict[str, Any]] = None  # passed to BlueprintGraphBuilderLibrary


@dataclass
class WidgetSpec:
    name: str                        # asset name e.g. WBP_HUD
    content_path: str                # e.g. /Game/Generated/UI
    root_widget: Dict[str, Any] = field(default_factory=dict)  # widget tree JSON


@dataclass
class MaterialSpec:
    name: str
    content_path: str
    base_color: Optional[List[float]] = None  # [r, g, b, a]
    is_instance: bool = False
    parent_material_path: Optional[str] = None


@dataclass
class DataAssetSpec:
    name: str
    content_path: str
    asset_type: str   # "DataAsset", "DataTable", "CurveFloat", "Enum", "Struct"
    row_struct: Optional[str] = None
    rows: List[Dict[str, Any]] = field(default_factory=list)


@dataclass
class LevelSpec:
    name: str                        # e.g. Map_Gameplay
    content_path: str                # e.g. /Game/Generated/Maps
    actors: List[Dict[str, Any]] = field(default_factory=list)  # actor placement defs
    camera_location: Optional[List[float]] = None
    lighting: str = "default"        # "default" | "dark" | "bright"


@dataclass
class InputMappingSpec:
    action_mappings: List[Dict[str, Any]] = field(default_factory=list)
    axis_mappings: List[Dict[str, Any]] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Animation
# ---------------------------------------------------------------------------

@dataclass
class AnimBlueprintSpec:
    name: str                        # e.g. ABP_Character
    content_path: str                # e.g. /Game/Generated/Animation
    skeleton_path: str               # content path to target USkeleton asset
    state_machines: List[Dict[str, Any]] = field(default_factory=list)
    # Each state machine: {"name": str, "states": [{"name", "anim_sequence_path"}],
    #                      "transitions": [{"from", "to", "condition"}]}


@dataclass
class BlendSpaceSpec:
    name: str
    content_path: str
    skeleton_path: str
    axis_x: Dict[str, Any] = field(default_factory=dict)   # {"name", "min", "max"}
    axis_y: Optional[Dict[str, Any]] = None                # None = 1D blend space
    samples: List[Dict[str, Any]] = field(default_factory=list)
    # Each sample: {"anim_path", "x", "y"}


@dataclass
class AnimMontageSpec:
    name: str
    content_path: str
    skeleton_path: str
    anim_sequence_path: str
    notifies: List[Dict[str, Any]] = field(default_factory=list)
    # Each notify: {"name", "time", "type"}  e.g. type "Sound", "Effect"


# ---------------------------------------------------------------------------
# AI
# ---------------------------------------------------------------------------

@dataclass
class BlackboardSpec:
    name: str
    content_path: str
    keys: List[Dict[str, Any]] = field(default_factory=list)
    # Each key: {"name": str, "type": "Bool"|"Float"|"Int"|"Name"|"Object"|"Vector"|"Enum"}


@dataclass
class BehaviorTreeSpec:
    name: str
    content_path: str
    blackboard_path: str             # content path to associated Blackboard asset
    # Root node as nested dict tree matching the BehaviorTreeBuilder JSON schema.
    root: Dict[str, Any] = field(default_factory=dict)
    # Each node: {"id": str, "type": str, "name": str (optional),
    #  "params": {...}, "children": [...], "decorators": [...], "services": [...]}
    # Composites: Selector, Sequence, SimpleParallel
    # Tasks: MoveTo, Wait, WaitBlackboardTime, RotateToFaceBBEntry, PlayAnimation,
    #        MakeNoise, RunBehavior, PlaySound (sound_to_play),
    #        FinishWithResult (result: Succeeded/Failed/Aborted),
    #        SetTagCooldown (cooldown_tag, cooldown_duration, add_to_existing)
    # Decorators: Blackboard (IsSet/IsNotSet/Equal/NotEqual/Less/LessOrEqual/Greater/
    #             GreaterOrEqual), ForceSuccess, Loop, TimeLimit, Cooldown, CompareBBEntries,
    #             IsAtLocation (blackboard_key, acceptable_radius, inverse_condition),
    #             DoesPathExist (blackboard_key_a, blackboard_key_b, path_query_type),
    #             TagCooldown (cooldown_tag, cool_down_time, add_to_existing),
    #             ConditionalLoop (same params as Blackboard),
    #             KeepInCone (cone_half_angle, cone_origin, observed),
    #             IsBBEntryOfClass (blackboard_key, test_class)
    # Services: DefaultFocus


@dataclass
class EQSQuerySpec:
    name: str
    content_path: str
    generator_type: str = "ActorsOfClass"  # ActorsOfClass | Donut | SimpleGrid | Cone
    tests: List[Dict[str, Any]] = field(default_factory=list)
    # Each test: {"type": "Distance"|"Dot"|"Trace"|"GameplayTag", "scoring": "..."}


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------

@dataclass
class SoundAttenuationSpec:
    name: str
    content_path: str
    radius_min: float = 100.0
    radius_max: float = 2000.0
    falloff_model: str = "Logarithmic"  # Logarithmic | Linear | Inverse | LogReverse
    occlusion_enabled: bool = False
    spatialization_enabled: bool = True


@dataclass
class SoundClassSpec:
    name: str
    content_path: str
    volume: float = 1.0
    pitch: float = 1.0
    parent_class_path: Optional[str] = None


@dataclass
class SoundMixSpec:
    name: str
    content_path: str
    fade_in_time: float = 0.1
    fade_out_time: float = 0.1
    # Modifiers per sound class
    modifiers: List[Dict[str, Any]] = field(default_factory=list)
    # Each modifier: {"sound_class_path": str, "volume": float, "pitch": float}


# ---------------------------------------------------------------------------
# Sequencer
# ---------------------------------------------------------------------------

@dataclass
class LevelSequenceSpec:
    name: str
    content_path: str
    duration_seconds: float = 5.0
    # Tracks to add: camera cuts, actor transform, event tracks, audio
    tracks: List[Dict[str, Any]] = field(default_factory=list)
    # Each track: {"type": "CameraCut"|"Transform"|"Event"|"Audio",
    #              "actor_path": str, "keyframes": [...]}


# ---------------------------------------------------------------------------
# Localization
# ---------------------------------------------------------------------------

@dataclass
class StringTableSpec:
    name: str
    content_path: str
    namespace: str                   # e.g. "PuzzleFighter"
    entries: List[Dict[str, str]] = field(default_factory=list)
    # Each entry: {"key": str, "value": str}


# ---------------------------------------------------------------------------
# Asset Manager / cook
# ---------------------------------------------------------------------------

@dataclass
class PrimaryAssetLabelSpec:
    name: str
    content_path: str
    priority: int = 1
    chunk_id: int = 0
    # Paths to include in this label's cook chunk
    include_paths: List[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Top-level BuildSpec
# ---------------------------------------------------------------------------

@dataclass
class BuildSpec:
    feature_name: str
    genre: str                       # e.g. "puzzle_fighter", "horror", "platformer"
    description: str                 # original user prompt
    blueprints: List[BlueprintSpec] = field(default_factory=list)
    widgets: List[WidgetSpec] = field(default_factory=list)
    cpp_classes: List[CppClassSpec] = field(default_factory=list)
    materials: List[MaterialSpec] = field(default_factory=list)
    data_assets: List[DataAssetSpec] = field(default_factory=list)
    levels: List[LevelSpec] = field(default_factory=list)
    input_mappings: InputMappingSpec = field(default_factory=InputMappingSpec)

    # Animation
    anim_blueprints: List[AnimBlueprintSpec] = field(default_factory=list)
    blend_spaces: List[BlendSpaceSpec] = field(default_factory=list)
    anim_montages: List[AnimMontageSpec] = field(default_factory=list)

    # AI
    blackboards: List[BlackboardSpec] = field(default_factory=list)
    behavior_trees: List[BehaviorTreeSpec] = field(default_factory=list)
    eqs_queries: List[EQSQuerySpec] = field(default_factory=list)

    # Audio
    sound_attenuations: List[SoundAttenuationSpec] = field(default_factory=list)
    sound_classes: List[SoundClassSpec] = field(default_factory=list)
    sound_mixes: List[SoundMixSpec] = field(default_factory=list)

    # Sequencer
    level_sequences: List[LevelSequenceSpec] = field(default_factory=list)

    # Localization
    string_tables: List[StringTableSpec] = field(default_factory=list)

    # Asset Manager / cook
    primary_asset_labels: List[PrimaryAssetLabelSpec] = field(default_factory=list)

    placeholder_policy: str = "generate"  # "generate" | "skip" | "stub_only"
    acceptance_tests: List[str] = field(default_factory=list)


def spec_to_dict(spec: BuildSpec) -> Dict[str, Any]:
    """Recursively convert a BuildSpec to a plain dict for JSON serialization."""
    import dataclasses

    def _convert(obj: Any) -> Any:
        if dataclasses.is_dataclass(obj):
            return {k: _convert(v) for k, v in dataclasses.asdict(obj).items()}
        if isinstance(obj, list):
            return [_convert(i) for i in obj]
        if isinstance(obj, dict):
            return {k: _convert(v) for k, v in obj.items()}
        return obj

    return _convert(spec)


# ---------------------------------------------------------------------------
# PIE Harness / Telemetry
# ---------------------------------------------------------------------------

@dataclass
class PIETestSpec:
    """A single acceptance test predicate for a PIE session."""
    predicate: str               # "pawn_possessed" | "widget_visible" | "log_contains"
                                 # | "ai_state" | "survive"
    target: Optional[str] = None     # class name, widget name, log string, or actor name
    expected: Optional[str] = None   # expected state value (for ai_state predicate)
    timeout_seconds: float = 5.0

    @staticmethod
    def from_string(s: str) -> "PIETestSpec":
        """Parse shorthand string into PIETestSpec.

        Examples:
            "pawn_possessed:BP_Character"  -> PIETestSpec("pawn_possessed", "BP_Character")
            "widget_visible:WBP_HUD"       -> PIETestSpec("widget_visible", "WBP_HUD")
            "log_contains:GameStarted"     -> PIETestSpec("log_contains", "GameStarted")
            "ai_state:BP_Enemy:Patrol"     -> PIETestSpec("ai_state", "BP_Enemy", "Patrol")
            "survive:5"                    -> PIETestSpec("survive", timeout_seconds=5.0)
        """
        parts = s.split(":", 2)
        predicate = parts[0]
        if predicate == "survive":
            timeout = float(parts[1]) if len(parts) > 1 else 5.0
            return PIETestSpec(predicate=predicate, timeout_seconds=timeout)
        target = parts[1] if len(parts) > 1 else None
        expected = parts[2] if len(parts) > 2 else None
        return PIETestSpec(predicate=predicate, target=target, expected=expected)


@dataclass
class AssertionResult:
    """Result of evaluating one PIETestSpec."""
    predicate: str
    passed: bool
    observed: str                # human-readable observed value
    target: Optional[str] = None  # class name, widget name, log string, or actor name
    raw: str = ""                # original shorthand test string (for debugging)


@dataclass
class TelemetryFrame:
    """Runtime state captured from one PIE snapshot."""
    log_lines_since_last: List[str]       # new log lines since last snapshot
    possessed_pawn_class: Optional[str]   # class name of possessed pawn, or None
    ai_controller_states: Dict[str, str]  # actor name -> state string
    visible_widgets: List[str]            # widget class names currently visible
    fps: float
    pie_world_name: Optional[str] = None  # world name, useful when multiple PIE worlds exist


@dataclass
class ClassResolutionCache:
    """Maps asset short names to resolved content paths.

    Built during generation and reused by pie_harness, repair_engine,
    and reference_validator. Avoids repeated load_object calls during PIE.
    """
    class_paths: Dict[str, str] = field(default_factory=dict)
    # e.g. {"BP_Character": "/Game/Generated/Gameplay/BP_Character"}


# ---------------------------------------------------------------------------
# Phase 2: Prompt Interpreter
# ---------------------------------------------------------------------------

@dataclass
class ActorIntent:
    """An actor/entity extracted from the user's prompt."""
    name: str                    # e.g. "player", "enemy", "door", "coin"
    role: str                    # "player" | "enemy" | "interactable" | "collectible" | "environment"
    qualifiers: List[str] = field(default_factory=list)
    # e.g. ["flying", "patrolling", "locked"]


@dataclass
class MechanicIntent:
    """A gameplay mechanic extracted from the prompt."""
    name: str                    # matches a key in MECHANIC_REGISTRY
    # e.g. "player_movement", "collect_item", "door_trigger", "enemy_patrol"
    params: Dict[str, Any] = field(default_factory=dict)
    # mechanic-specific params, e.g. {"item_name": "coin", "count": 5}


@dataclass
class RelationshipIntent:
    """A causal relationship between game elements."""
    subject: str                 # e.g. "door"
    verb: str                    # e.g. "opens", "spawns", "destroys", "activates"
    trigger: str                 # e.g. "all coins collected", "player enters", "timer expires"
    params: Dict[str, Any] = field(default_factory=dict)


@dataclass
class IntentMap:
    """Structured representation of what the user wants to build."""
    genre: str                   # detected genre
    feature_name: str            # derived from prompt or genre
    description: str             # original prompt
    actors: List[ActorIntent] = field(default_factory=list)
    mechanics: List[MechanicIntent] = field(default_factory=list)
    relationships: List[RelationshipIntent] = field(default_factory=list)
    ui_requests: List[str] = field(default_factory=list)
    # e.g. ["main_menu", "hud", "game_over", "pause_menu"]
