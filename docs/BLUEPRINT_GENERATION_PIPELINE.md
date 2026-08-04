# Blueprint Compiler & Generation Pipeline Specification

## 1. Overview

This document specifies the deterministic **Blueprint Compiler Pipeline** for Unreal Engine 4.27 in the MCP Bridge. The pipeline replaces ad-hoc, node-by-node editing with a two-stage compiler architecture:

1. **Logical Planning & Partitioning** (Model/AI): Intent analysis, architecture partitioning (C++ vs. Blueprint), intermediate representation (IR) construction, and semantic node identification.
2. **Deterministic Construction & Layout** (Compiler/Bridge Engine): Topological depth assignment, vertical lane routing, two-pass Blueprint compilation, transactional patching, automatic comment bounding, and linter metrics.

---

## 2. Blueprint Intermediate Representation (IR)

The Intermediate Representation defines feature behavior, assets, variables, functions, and logical graph connections **without hardcoded 2D pixel coordinates**.

### Schema Definition

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "BlueprintIR",
  "type": "object",
  "required": ["schema_version", "feature_id", "intent", "architecture_signals", "blueprints"],
  "properties": {
    "schema_version": { "type": "integer", "enum": [1] },
    "feature_id": { "type": "string", "pattern": "^[a-z][a-z0-9_]*$" },
    "intent": { "type": "string" },
    "constraints": {
      "type": "object",
      "properties": {
        "ue_version": { "type": "string", "enum": ["4.27"] },
        "authoring_root": { "type": "string", "default": "/Game/MCPGenerated" },
        "networked": { "type": "boolean" },
        "designer_editable": { "type": "boolean" }
      }
    },
    "architecture_signals": {
      "type": "object",
      "properties": {
        "reusable_runtime_system": { "type": "boolean" },
        "complex_algorithm": { "type": "boolean" },
        "performance_sensitive": { "type": "boolean" },
        "replicated_or_authoritative": { "type": "boolean" },
        "large_data_transform": { "type": "boolean" },
        "shared_base_class": { "type": "boolean" },
        "strict_runtime_tests": { "type": "boolean" },
        "asset_composition": { "type": "boolean" },
        "designer_settings": { "type": "boolean" },
        "event_wiring": { "type": "boolean" },
        "visual_extension_points": { "type": "boolean" }
      }
    },
    "cpp": {
      "type": "object",
      "properties": {
        "classes": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["name", "header", "source", "responsibility"],
            "properties": {
              "name": { "type": "string" },
              "header": { "type": "string" },
              "source": { "type": "string" },
              "responsibility": { "type": "string" }
            }
          }
        }
      }
    },
    "blueprints": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["asset_path", "parent_class", "components", "variables", "functions", "graphs"],
        "properties": {
          "asset_path": { "type": "string" },
          "parent_class": { "type": "string" },
          "depends_on": { "type": "array", "items": { "type": "string" } },
          "components": { "type": "array" },
          "variables": { "type": "array" },
          "functions": { "type": "array" },
          "macros": { "type": "array" },
          "dispatchers": { "type": "array" },
          "interfaces": { "type": "array", "items": { "type": "string" } },
          "graphs": {
            "type": "array",
            "items": {
              "type": "object",
              "required": ["name", "kind", "nodes", "connections"],
              "properties": {
                "name": { "type": "string" },
                "kind": { "type": "string", "enum": ["event", "function", "macro", "construction"] },
                "nodes": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "required": ["id", "type"],
                    "properties": {
                      "id": { "type": "string", "description": "Semantic ID, e.g. interaction.input.grab" },
                      "type": { "type": "string" },
                      "params": { "type": "object" },
                      "functional": { "type": "boolean" },
                      "branch_depth": { "type": "integer" },
                      "branch_lane": { "type": "integer" },
                      "region": { "type": "string" }
                    }
                  }
                },
                "connections": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "required": ["from", "from_pin", "to", "to_pin"],
                    "properties": {
                      "from": { "type": "string" },
                      "from_pin": { "type": "string" },
                      "to": { "type": "string" },
                      "to_pin": { "type": "string" }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
```

---

## 3. Architecture Partitioner (C++ vs. Blueprint)

The planner partitions responsibilities based on execution requirements:

| Decision Domain | Placement | Placement Rationale |
|---|---|---|
| Reusable Runtime Systems & Math | **C++ Native Component** | High performance, memory efficiency, unit testability, zero GC overhead |
| State Machines & Heavy Loops | **C++ Native Class** | Prevents visual graph clutter and deep nested branch execution |
| Network Replication & Physics | **C++ Native Class** | Deterministic server authority, low bandwidth serialization |
| Component Hierarchy & Asset Refs | **Blueprint Child** | Visual editor composition, easy designer tweaks |
| Input Wiring & Audio/VFX Hooks | **Blueprint Child** | Easy binding to project Input Settings and Particle Systems |
| UI & Widget Presentation | **Widget Blueprint** | Direct Slate UI layout, designer visual binding |

### Standard Architecture Hierarchy

```text
UUTelekineticInteractionComponent (C++ Native)
       ↓
BPC_TelekineticInteraction (Blueprint Component)
       ↓
BP_FirstPersonCharacter (Blueprint Owner Actor)
```

---

## 4. Code-First Generation Sequence

To avoid unnecessary editor restarts and unresolvable symbol references:

1. **Feature Planning & IR Construction**: Build complete `FeaturePlan` specifying C++ headers, sources, and Blueprint desired states.
2. **C++ Native Generation**: Write header files (`.h`) and implementation files (`.cpp`) under source control.
3. **Target Compilation**: Build the Unreal Editor target executable once (`npm run install:sync`).
4. **Unreal Editor Session Initialization**: Open UE4.27 to load newly compiled C++ types into the Unreal Class Default Object (CDO) registry.
5. **Pass 1 - Structural Blueprint Construction**: Create Blueprint child classes, inherit parent classes, declare components, variables, functions, and dispatchers. Compile structure.
6. **Pass 2 - Behavioral Graph & Layout Construction**: Apply deterministic node layout, place nodes, route execution wires, assign comment regions, and create reroutes. Compile behavior.
7. **Verification & Snapshot Hash Comparison**: Perform independent graph inspection (`puerts_graph_inspect`), compute structural SHA-1 hash, verify 0 dropped connections, and record cold reload snapshot.

---

## 5. Deterministic Layout Engine & Named Layout Lanes

Nodes are positioned automatically by a 10-step layout algorithm:

### Layout Engine Steps

1. **Topological Execution Graph Build**: Calculate node dependencies and indegrees.
2. **Horizontal Depth Assignment**: Assign column index `X = depth * horizontal_gap` (default 320px).
3. **Vertical Branch Lane Assignment**: Map parallel branch paths to vertical lanes (`Y = lane * vertical_gap` - default 180px).
4. **Consumer Alignment**: Position data-only nodes directly adjacent to consuming functional nodes.
5. **Comment Region Bounding**: Compute minimum/maximum X and Y bounds of all region members and apply 80px/100px padding.
6. **Reroute Insertion**: Automatically insert reroute (`Knot`) nodes whenever a connection spans > 2 horizontal columns or crosses vertical lanes.
7. **Grid Alignment**: Snap all coordinates to a 16px grid.
8. **Crossing Reduction**: Re-order vertical offsets to minimize intersecting execution wires.
9. **Layout Hash Generation**: Calculate SHA-1 digest over final `(X, Y)` coordinates and comment bounds.

### Layout Lanes

```text
Lane 0 (Input)        → Lane 1 (Validation) → Lane 2 (Native Call) → Lane 3 (State Update) → Lane 4 (VFX/Events)
----------------------------------------------------------------------------------------------------------------
InputKey (Grab)       → Branch (bCanGrab)   → TryGrabObject()      → Set HeldObject      → PlaySoundAtLocation
```

---

## 6. Blueprint Complexity Budgets & Linter Metrics

### Configurable Complexity Thresholds

- **Max Functional Nodes**: 25 nodes per graph
- **Max Branch Depth**: 3 nested conditional branches
- **Max Loop Iterations**: 100 iterations (suggests C++ extraction)
- **Max Wire Crossings**: 4 intersecting connections per graph

### Automatic C++ Refactoring Triggers

When graph inspection detects threshold violations, the planner emits structured refactoring recommendations:

```json
{
  "code": "FUNCTIONAL_NODE_LIMIT",
  "message": "32 functional nodes exceed threshold 25. Recommendation: Extract repeated math into UFUNCTION(BlueprintCallable) UTelekineticLibrary::ProcessPickupMath().",
  "asset_path": "/Game/MCPGenerated/BP_TelekineticCharacter",
  "graph": "EventGraph"
}
```

---

## 7. Telekinetic Interaction Capstone Suite

The Telekinetic Capstone integrates all compiler pipeline stages into a complete test suite:

- **C++ Class**: `UTelekineticInteractionComponent`
- **Blueprint Component**: `/Game/MCPGenerated/BPC_TelekineticInteraction`
- **Blueprint Character**: `/Game/MCPGenerated/BP_TelekineticCharacter`
- **Widget HUD**: `/Game/MCPGenerated/WBP_TelekineticHUD`
- **Capabilities Verified**: Structural two-pass compilation, deterministic grid layout, 0 wire crossings, independent inspection hash agreement, transactional failure rollback, and cold reload snapshot parity.
