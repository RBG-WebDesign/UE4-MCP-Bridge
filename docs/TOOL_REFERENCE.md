# Tool Reference

## UE Bridge MCP Testing Workflow

For authoring tasks, use bridge tools to make the requested editor change, then
stop after lightweight editor-side sanity checks. Let the user test the result
in Unreal before starting PIE. Do not run PIE, gameplay telemetry, or runtime
acceptance commands unless the user explicitly asks for runtime testing or the
task is specifically about PIE behavior.

## System Tools

### test_connection
Pings the Python listener and returns engine/project metadata.

**Parameters:** None

**Returns:**
```json
{
  "success": true,
  "data": {
    "status": "connected",
    "engine_version": "4.27.2",
    "project": "MyProject",
    "project_dir": "/path/to/project",
    "content_dir": "/path/to/project/Content",
    "platform": "UserName"
  }
}
```

### python_proxy
Executes arbitrary Python code inside the UE4 editor.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| code | string | yes | Python code to execute |

**Returns:**
```json
{
  "success": true,
  "data": {
    "result": "repr() of the return value",
    "stdout": "any print() output"
  }
}
```

### help
Returns documentation for all tools or a specific tool.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| tool_name | string | no | Specific tool to get help for |

---

## Project Tools

### project_info
Returns current UE project info.

### asset_list
Lists assets with optional filters.

### asset_info
Returns detailed info for a single asset.

---

## Actor Tools

All actor tools that create or move actors support a `validate` parameter (default `true`). When enabled, the tool reads the actor's actual transform after the operation and compares it against the requested values using tolerance thresholds:
- Location: 0.1 units
- Rotation: 0.1 degrees
- Scale: 0.001

Validation results appear in the response under `data.validation`.

### actor_spawn
Spawn an actor from an asset path at a given location/rotation/scale.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| asset_path | string | yes | Asset path (must start with `/`, e.g., `/Game/Meshes/SM_Cube`) |
| location | {x, y, z} | no | World location (default 0,0,0) |
| rotation | {pitch, yaw, roll} | no | Rotation in degrees (default 0,0,0) |
| scale | {x, y, z} | no | Scale (default 1,1,1). Zero components are rejected. |
| name | string | no | Actor label. Special characters `/ \ : * ? " < > |` are replaced with `_`. |
| folder | string | no | World Outliner folder path |
| validate | boolean | no | Validate resulting transform (default true) |

**Request:**
```json
{
  "asset_path": "/Game/StarterContent/Shapes/Shape_Cube",
  "location": {"x": 100, "y": 200, "z": 0},
  "rotation": {"pitch": 0, "yaw": 45, "roll": 0},
  "scale": {"x": 2, "y": 2, "z": 2},
  "name": "MyCube",
  "folder": "Environment/Props"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "MyCube",
    "class": "StaticMeshActor",
    "location": {"x": 100.0, "y": 200.0, "z": 0.0},
    "rotation": {"pitch": 0.0, "yaw": 45.0, "roll": 0.0},
    "scale": {"x": 2.0, "y": 2.0, "z": 2.0},
    "folder": "Environment/Props",
    "validation": {
      "valid": true,
      "errors": []
    }
  }
}
```

**Error cases:**
- Missing or empty `asset_path`
- `asset_path` that doesn't start with `/`
- Asset not found at the given path
- Scale with a zero component

---

### actor_modify
Change an actor's location, rotation, scale, mesh, or visibility.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actor_name | string | yes | Label of the actor to modify |
| location | {x, y, z} | no | New world location |
| rotation | {pitch, yaw, roll} | no | New rotation in degrees |
| scale | {x, y, z} | no | New scale. Zero components are rejected. |
| visible | boolean | no | Set editor visibility |
| mesh | string | no | Asset path for a new StaticMesh |
| validate | boolean | no | Validate resulting transform (default true) |

At least one property must be provided. Fails with an error if called with no changes.

**Request:**
```json
{
  "actor_name": "MyCube",
  "location": {"x": 500, "y": 300, "z": 100},
  "scale": {"x": 3, "y": 3, "z": 3}
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "modified_properties": ["location", "scale"],
    "actor": {
      "name": "MyCube",
      "class": "StaticMeshActor",
      "location": {"x": 500.0, "y": 300.0, "z": 100.0},
      "rotation": {"pitch": 0.0, "yaw": 45.0, "roll": 0.0},
      "scale": {"x": 3.0, "y": 3.0, "z": 3.0},
      "folder": "Environment/Props"
    },
    "validation": {
      "valid": true,
      "errors": []
    }
  }
}
```

**Error cases:**
- Actor not found
- No properties provided
- Zero scale component
- Mesh path doesn't start with `/`
- Mesh asset not found or not a StaticMesh
- Actor has no StaticMeshComponent (when changing mesh)

---

### actor_delete
Delete actors by name or wildcard pattern.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actor_name | string | yes | Exact name or pattern with `*`, `?`, `[]` wildcards (uses fnmatch) |

**Request:**
```json
{
  "actor_name": "Wall_*"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "deleted_count": 3,
    "deleted_actors": ["Wall_01", "Wall_02", "Wall_03"]
  }
}
```

**Error cases:**
- Missing `actor_name`
- No actors match the name/pattern

---

### actor_duplicate
Duplicate an existing actor with an optional positional offset.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actor_name | string | yes | Label of the actor to duplicate |
| offset | {x, y, z} | no | Offset from original position |
| new_name | string | no | Label for the duplicate (defaults to `<name>_copy`) |
| validate | boolean | no | Validate resulting transform (default true) |

**Request:**
```json
{
  "actor_name": "MyCube",
  "offset": {"x": 200, "y": 0, "z": 0},
  "new_name": "MyCube_Copy"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "MyCube_Copy",
    "class": "StaticMeshActor",
    "location": {"x": 700.0, "y": 300.0, "z": 100.0},
    "rotation": {"pitch": 0.0, "yaw": 45.0, "roll": 0.0},
    "scale": {"x": 3.0, "y": 3.0, "z": 3.0},
    "folder": "Environment/Props",
    "validation": {
      "valid": true,
      "errors": []
    }
  }
}
```

**Error cases:**
- Actor not found
- Duplicate operation failed (no actor selected after duplication)

---

### actor_organize
Move actors into World Outliner folders.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actors | string[] | yes | Actor labels to move |
| folder | string | yes | Target folder path (e.g., `Environment/Walls`) |

**Request:**
```json
{
  "actors": ["Wall_01", "Wall_02", "Floor_01"],
  "folder": "Environment/Structure"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "moved": ["Wall_01", "Wall_02", "Floor_01"],
    "not_found": [],
    "folder": "Environment/Structure"
  }
}
```

Actors that don't exist are reported in `not_found` but do not cause failure.

---

### actor_snap_to_socket
Snap one actor to another actor's named socket. Moves and rotates the actor to match the socket's world transform.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actor_name | string | yes | Actor to move |
| target_actor | string | yes | Actor that owns the socket |
| socket_name | string | yes | Name of the socket on the target's mesh |

**Request:**
```json
{
  "actor_name": "Torch",
  "target_actor": "Wall_Segment",
  "socket_name": "TorchMount"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "Torch",
    "class": "StaticMeshActor",
    "location": {"x": 150.0, "y": 0.0, "z": 200.0},
    "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0},
    "scale": {"x": 1.0, "y": 1.0, "z": 1.0},
    "folder": ""
  }
}
```

**Error cases:**
- Actor or target actor not found
- Target has no mesh components (StaticMesh or SkeletalMesh)
- Socket name doesn't exist on the target mesh

---

### batch_spawn
Spawn multiple actors in one call. Wrapped in a single UE4 transaction for undo support.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| spawns | array | yes | Array of spawn definitions (same format as `actor_spawn` params) |

Each spawn definition accepts: `asset_path`, `location`, `rotation`, `scale`, `name`, `folder`, `validate`.

**Request:**
```json
{
  "spawns": [
    {"asset_path": "/Game/Meshes/SM_Cube", "name": "Cube_01", "location": {"x": 0, "y": 0, "z": 0}},
    {"asset_path": "/Game/Meshes/SM_Cube", "name": "Cube_02", "location": {"x": 200, "y": 0, "z": 0}},
    {"asset_path": "/Game/Meshes/SM_Sphere", "name": "Sphere_01", "location": {"x": 400, "y": 0, "z": 0}}
  ]
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "total": 3,
    "succeeded": 3,
    "failed": 0,
    "results": [
      {"index": 0, "success": true, "data": {"name": "Cube_01", "..."}, "error": null},
      {"index": 1, "success": true, "data": {"name": "Cube_02", "..."}, "error": null},
      {"index": 2, "success": true, "data": {"name": "Sphere_01", "..."}, "error": null}
    ]
  }
}
```

Individual spawn failures don't fail the batch. Check each result's `success` field. The overall response `success` is always `true` if the batch ran (even if individual spawns failed).

**Error cases:**
- `spawns` is not a list
- `spawns` array is empty
- Individual spawn definitions are validated per `actor_spawn` rules

---

### placement_validate
Check actors for placement issues: overlaps (actors too close) and gaps (actors too far apart based on bounding boxes).

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| actors | string[] | yes | Actor labels to check |
| check_gaps | boolean | no | Check for gaps between bounding boxes (default true) |
| check_overlaps | boolean | no | Check for actors at the same position (default true) |
| gap_threshold | float | no | Max acceptable gap between bounding box edges, in units (default 1.0) |
| overlap_threshold | float | no | Min distance between actor origins for overlap detection (default 1.0) |

**Request:**
```json
{
  "actors": ["Wall_01", "Wall_02", "Wall_03"],
  "check_gaps": true,
  "check_overlaps": true,
  "gap_threshold": 2.0
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "actors_checked": 3,
    "not_found": [],
    "issues": [
      {
        "type": "overlap",
        "actors": ["Wall_01", "Wall_02"],
        "distance": 0.5
      },
      {
        "type": "gap",
        "actors": ["Wall_02", "Wall_03"],
        "gap": 15.3,
        "threshold": 2.0
      }
    ],
    "issue_count": 2
  }
}
```

Needs at least 2 actors for pairwise checks. Actors not found are listed in `not_found`.

---

## Level Tools

### level_actors
List actors in the current level with optional filters.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| class_filter | string | no | | Filter by class name (supports `*` and `?` wildcards) |
| folder_filter | string | no | | Filter by World Outliner folder prefix |
| name_filter | string | no | | Filter by actor label (supports `*` and `?` wildcards) |
| include_transforms | boolean | no | true | Include location/rotation/scale in results |
| include_components | boolean | no | false | Include component list for each actor |
| limit | integer | no | 500 | Max actors to return. Large levels can have thousands. |

**Request:**
```json
{
  "name_filter": "Wall_*",
  "folder_filter": "Environment",
  "include_components": false,
  "limit": 100
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "count": 3,
    "total_in_level": 847,
    "actors": [
      {
        "name": "Wall_01",
        "class": "StaticMeshActor",
        "folder": "Environment/Walls",
        "location": {"x": 0.0, "y": 0.0, "z": 0.0},
        "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
        "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
      }
    ]
  }
}
```

Results are sorted by folder path then name. If the result count equals the limit, `truncated: true` and `limit` are included. If retrieval takes over 2 seconds, a `warning` field suggests using filters.

**Error cases:**
- Exception during actor iteration (corrupted level)

---

### level_save
Save the current level and optionally all dirty assets.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| save_all | boolean | no | false | Also save all modified assets |

**Request:**
```json
{
  "save_all": true
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "level_saved": "MyLevel",
    "assets_saved_count": 12,
    "save_all": true
  }
}
```

**Error cases:**
- No level is currently open
- Level file is read-only or locked by source control
- Level saves but asset save fails (partial success with `warning` field)

---

### level_outliner
Return the World Outliner folder tree structure with actor counts.

UE4 has no "get all folders" API. This builds the tree by iterating all actors and collecting unique folder paths.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| root_folder | string | no | | Optional subtree root to filter by |

**Request:**
```json
{
  "root_folder": "Environment"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "folders": [
      {"path": "Environment/Walls", "actor_count": 15, "children": []},
      {"path": "Environment/Floors", "actor_count": 8, "children": []}
    ],
    "folder_count": 2,
    "total_actors": 847,
    "unfoldered_actor_count": 12,
    "root_folder": "Environment"
  }
}
```

**Error cases:**
- Exception during actor iteration

---

## Viewport Tools

Viewport state changes (camera moves, mode switches, render mode) are NOT transactable in UE4's undo system. These operations cannot be undone with `undo`.

### viewport_screenshot
Capture the active viewport to a PNG file. Returns the absolute filesystem path so Claude Code can read the image.

Screenshots are saved to `{ProjectDir}/Saved/Screenshots/MCPBridge/`.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| filename | string | no | auto-generated | Output filename |
| resolution | {width, height} | no | 1920x1080 | Screenshot resolution in pixels |
| show_ui | boolean | no | false | Include editor UI (reserved for future use) |

**Request:**
```json
{
  "filename": "my_screenshot.png",
  "resolution": {"width": 3840, "height": 2160}
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "filepath": "D:/Projects/MyGame/Saved/Screenshots/MCPBridge/my_screenshot.png",
    "filename": "my_screenshot.png",
    "resolution": {"width": 3840, "height": 2160},
    "file_size_bytes": 2456789,
    "capture_method": "AutomationLibrary.take_high_res_screenshot",
    "camera_location": {"x": 100.0, "y": 200.0, "z": 300.0},
    "camera_rotation": {"pitch": -15.0, "yaw": 45.0, "roll": 0.0}
  }
}
```

**Error cases:**
- No active viewport (no level open in the editor)
- Cannot create screenshot directory (permissions)
- Screenshot capture failed (both primary and fallback methods)
- Resolution width or height is not a positive integer

---

### viewport_camera
Set viewport camera position, rotation, and/or FOV. Parameters not provided are preserved at their current values.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| location | [x, y, z] | no | current | Camera position as 3-number array |
| rotation | [pitch, yaw, roll] | no | current | Camera rotation as 3-number array |
| fov | number | no | current | Field of view in degrees (1-170) |

At least one parameter must be provided.

**Request:**
```json
{
  "location": [500, -200, 300],
  "rotation": [-20, 135, 0]
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "location": {"x": 500.0, "y": -200.0, "z": 300.0},
    "rotation": {"pitch": -20.0, "yaw": 135.0, "roll": 0.0}
  }
}
```

The response contains the actual camera state after setting (read back, not echoed input).

**Error cases:**
- No parameters provided (rejected by Zod)
- FOV out of range (1-170)

---

### viewport_mode
Switch to a standard view preset.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| mode | enum | yes | | `"perspective"`, `"top"`, `"bottom"`, `"front"`, `"back"`, `"left"`, `"right"` |

**Request:**
```json
{
  "mode": "top"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "location": {"x": 0.0, "y": 0.0, "z": 5000.0},
    "rotation": {"pitch": -90.0, "yaw": 0.0, "roll": 0.0},
    "mode": "top"
  }
}
```

Switching between perspective and ortho views repositions the camera. The new position is included in the response.

**Error cases:**
- Invalid mode string (rejected by Zod enum)

---

### viewport_focus
Focus the viewport camera on a named actor. Uses the actor's bounding box center as the target.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| actor_name | string | yes | | Label of the actor to focus on |
| distance | number | no | 500 | Distance from actor (must be positive) |

**Request:**
```json
{
  "actor_name": "MyCube",
  "distance": 800
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "focused_on": "MyCube",
    "camera_location": {"x": -60.0, "y": -60.0, "z": 500.0},
    "camera_rotation": {"pitch": -25.0, "yaw": 45.0, "roll": 0.0},
    "actor_bounds": {
      "min": {"x": -50.0, "y": -50.0, "z": -50.0},
      "max": {"x": 50.0, "y": 50.0, "z": 50.0}
    }
  }
}
```

**Error cases:**
- Missing `actor_name`
- Actor not found (error suggests using `level_actors`)

---

### viewport_render_mode
Change the viewport render mode.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| mode | enum | yes | | `"lit"`, `"unlit"`, `"wireframe"`, `"detail_lighting"`, `"lighting_only"`, `"light_complexity"`, `"shader_complexity"`, `"collision"` |

**Request:**
```json
{
  "mode": "wireframe"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "render_mode": "wireframe"
  }
}
```

**Error cases:**
- Invalid mode string (rejected by Zod enum)
- No level is currently open

---

### viewport_bounds
Return current viewport camera state. Read-only query, no parameters required.

**Parameters:** None

**Response:**
```json
{
  "success": true,
  "data": {
    "camera_location": {"x": 500.0, "y": -200.0, "z": 300.0},
    "camera_rotation": {"pitch": -20.0, "yaw": 135.0, "roll": 0.0},
    "is_perspective": true
  }
}
```

---

### viewport_fit
Fit actors into the viewport frame. Computes the combined bounding box and positions the camera to see all targets.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| actor_names | string[] | no | all actors | Actor labels to fit. Empty or omitted = fit all actors. |
| padding | number | no | 1.2 | Distance multiplier (1.2 = 20% extra space) |

**Request:**
```json
{
  "actor_names": ["Wall_01", "Wall_02", "Floor_01"],
  "padding": 1.5
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "fitted_actors": ["Wall_01", "Wall_02", "Floor_01"],
    "fitted_count": 3,
    "camera_location": {"x": -800.0, "y": -800.0, "z": 600.0},
    "camera_rotation": {"pitch": -30.0, "yaw": 45.0, "roll": 0.0},
    "combined_bounds": {
      "min": {"x": -200.0, "y": -100.0, "z": 0.0},
      "max": {"x": 600.0, "y": 400.0, "z": 300.0}
    },
    "padding": 1.5
  }
}
```

**Error cases:**
- Named actors not found (returns error with not-found list)
- No actors in the level when fitting all

---

### viewport_look_at
Point the camera at a target without moving the camera position. Only changes rotation.

If both `actor_name` and `location` are provided, `actor_name` takes priority. When targeting an actor, uses the bounding box center (not the actor's origin, which may be at its feet).

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| actor_name | string | no | | Actor label to look at |
| location | [x, y, z] | no | | World coordinates to look at |

At least one of `actor_name` or `location` must be provided.

**Request:**
```json
{
  "actor_name": "MyCube"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "camera_location": {"x": 500.0, "y": -200.0, "z": 300.0},
    "camera_rotation": {"pitch": -15.0, "yaw": 120.0, "roll": 0.0},
    "target_location": {"x": 100.0, "y": 200.0, "z": 50.0}
  }
}
```

**Error cases:**
- Neither `actor_name` nor `location` provided (rejected by Zod)
- Actor not found (error suggests using `level_actors`)

---

## Visual Feedback Workflow

The recommended pattern for spatial operations:

1. Spawn or modify actors
2. `viewport_focus` on the actor you changed
3. `viewport_screenshot` to see the result
4. If something looks wrong, adjust with `actor_modify` and repeat
5. `viewport_mode` "top" for a spatial overview when placing multiple actors
6. `viewport_screenshot` for a top-down reference
7. `viewport_fit` to frame all relevant actors when checking overall layout
8. `level_save` when satisfied

This visual feedback loop catches placement errors, scale issues, and spatial relationships that are hard to verify from data alone.

---

## Material Tools

### material_list
List materials in the project with optional filtering.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| path_filter | string | no | /Game/ | Content path prefix filter |
| name_filter | string | no | | Filter by name (supports fnmatch wildcards `*`, `?`) |
| type_filter | enum | no | all | `"material"`, `"instance"`, or `"all"` |
| limit | integer | no | 200 | Max results (1-2000) |

**Request:**
```json
{
  "type_filter": "instance",
  "name_filter": "MI_Brick*",
  "limit": 50
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "count": 3,
    "truncated": false,
    "materials": [
      {
        "name": "MI_BrickWall_Red",
        "path": "/Game/Materials/Instances/MI_BrickWall_Red",
        "type": "instance",
        "parent": "/Game/Materials/M_BrickWall"
      }
    ]
  }
}
```

**Error cases:**
- Exception during asset iteration

---

### material_info
Get detailed information about a material or material instance: parameters, parent chain.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| material_path | string | yes | Asset path (must start with `/`) |

**Request:**
```json
{
  "material_path": "/Game/Materials/Instances/MI_BrickWall_Red"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "MI_BrickWall_Red",
    "path": "/Game/Materials/Instances/MI_BrickWall_Red",
    "type": "instance",
    "parent": "/Game/Materials/M_BrickWall",
    "parent_chain": ["/Game/Materials/M_BrickWall"],
    "parameters": {
      "scalar": [{"name": "Roughness", "value": 0.7}],
      "vector": [{"name": "BaseColor", "value": [0.9, 0.1, 0.1, 1.0]}],
      "texture": [{"name": "DiffuseTexture", "value": "/Game/Textures/T_Brick_D"}]
    }
  }
}
```

**Error cases:**
- Path doesn't start with `/`
- Asset not found at path
- Asset is not a Material or MaterialInstanceConstant

---

### material_create
Create a new material or material instance. For instances, provide a parent material and optionally set initial parameters.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| name | string | yes | Material name (e.g., `MI_Wall_Custom`) |
| path | string | yes | Content directory (e.g., `/Game/Materials/`) |
| type | enum | yes | `"material"` or `"instance"` |
| parent | string | if instance | Parent material asset path |
| parameters | object | no | Initial parameter values (instances only) |

The `parameters` object structure:
```json
{
  "scalar": {"Roughness": 0.5, "Metallic": 0.0},
  "vector": {"BaseColor": [1.0, 0.0, 0.0, 1.0]},
  "texture": {"DiffuseTexture": "/Game/Textures/T_MyTexture"}
}
```

**Request:**
```json
{
  "name": "MI_Wall_Custom",
  "path": "/Game/Materials",
  "type": "instance",
  "parent": "/Game/Materials/M_BrickWall",
  "parameters": {
    "scalar": {"Roughness": 0.5},
    "vector": {"BaseColor": [1.0, 0.0, 0.0, 1.0]}
  }
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "MI_Wall_Custom",
    "path": "/Game/Materials/MI_Wall_Custom",
    "type": "instance",
    "parent": "/Game/Materials/M_BrickWall",
    "parameters_set": {
      "scalar": ["Roughness"],
      "vector": ["BaseColor"],
      "texture": []
    }
  }
}
```

Parameters that fail to set (due to MaterialEditingLibrary limitations in 4.27) are silently skipped. Check `parameters_set` to confirm which parameters were applied.

**Error cases:**
- Missing `name`
- Path doesn't start with `/`
- Type is `"instance"` but no `parent` provided
- Parent material not found
- Parent is not a Material or MaterialInstanceConstant
- Asset already exists at target path

---

### material_apply
Apply a material to an actor's mesh component.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| actor_name | string | yes | | Label of the target actor |
| material_path | string | yes | | Asset path of the material to apply |
| slot_index | integer | no | 0 | Material slot index |
| slot_name | string | no | | Material slot name (overrides slot_index) |
| component_name | string | no | first mesh | Specific mesh component name |

**Request:**
```json
{
  "actor_name": "Wall_01",
  "material_path": "/Game/Materials/MI_BrickWall_Red",
  "slot_index": 0
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "actor": "Wall_01",
    "component": "StaticMeshComponent0",
    "slot_index": 0,
    "slot_name": null,
    "material_applied": "/Game/Materials/MI_BrickWall_Red",
    "previous_material": "/Game/Materials/M_Default",
    "total_slots": 3
  }
}
```

The response includes `previous_material` so you can undo manually if needed, and `material_applied` which is read back from the component to verify the operation.

**Error cases:**
- Actor not found
- Actor has no mesh components
- Named component not found on actor
- Material asset not found or not a Material/MaterialInstanceConstant
- Slot index out of range (error includes total slot count)
- Slot name not found (error includes available slot names)

---

### Material Application Workflow

For applying materials to actors:

1. `material_list` to see what's available
2. `material_info` to inspect parameters of a candidate material
3. `material_create` if a new instance is needed (with custom parameters)
4. `material_apply` to the target actor
5. `viewport_focus` and `viewport_screenshot` to verify the result
6. Adjust parameters with `material_create` (new instance) if needed
7. `level_save` when satisfied

Note: To change parameters on an existing material instance, create a new
instance with the updated values rather than modifying the existing one.
Modifying existing material parameters in-place is a future enhancement.

---

## Blueprint Tools

### blueprint_list
List Blueprint assets in the project.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| path_filter | string | no | /Game/ | Content path prefix |
| name_filter | string | no | | Filter by name (supports fnmatch wildcards) |
| parent_class_filter | string | no | | Filter by parent class name (e.g., `Actor`, `Pawn`) |
| limit | integer | no | 200 | Max results (1-2000) |

**Request:**
```json
{
  "parent_class_filter": "Actor",
  "name_filter": "BP_Enemy_*"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "count": 3,
    "truncated": false,
    "blueprints": [
      {
        "name": "BP_Enemy_Melee",
        "path": "/Game/Blueprints/BP_Enemy_Melee",
        "parent_class": "Actor",
        "is_compiled": true
      }
    ]
  }
}
```

**Error cases:**
- Exception during asset iteration

---

### blueprint_info
Get detailed Blueprint structure: components, variables, functions, event graphs, parent chain.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| blueprint_path | string | yes | Asset path (must start with `/`) |

**Request:**
```json
{
  "blueprint_path": "/Game/Blueprints/BP_MasterTrigger"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "BP_MasterTrigger",
    "path": "/Game/Blueprints/BP_MasterTrigger",
    "parent_class": "Actor",
    "parent_chain": ["Actor", "Object"],
    "is_compiled": true,
    "components": [
      {"name": "DefaultSceneRoot", "class": "SceneComponent", "parent": null, "is_root": true},
      {"name": "TriggerBox", "class": "BoxComponent", "parent": "DefaultSceneRoot", "is_root": false}
    ],
    "variables": [
      {"name": "TriggerEnabled", "type": "bool", "category": "Conditions", "is_editable": true, "tooltip": "Whether this trigger is active"}
    ],
    "functions": [
      {"name": "ActivateTrigger", "inputs": [], "outputs": [], "is_pure": false}
    ],
    "event_graphs": ["EventGraph"],
    "component_count": 2,
    "variable_count": 1,
    "function_count": 1
  }
}
```

**Error cases:**
- Path doesn't start with `/`
- Asset not found
- Asset is not a Blueprint

---

### blueprint_create
Create a new Blueprint class with optional components and variables.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| name | string | yes | | Blueprint name |
| path | string | yes | /Game/Blueprints | Content directory |
| parent_class | string | yes | Actor | Parent class: `Actor`, `Pawn`, `Character`, `PlayerController`, `GameModeBase`, `ActorComponent`, `SceneComponent` |
| components | array | no | | Components to add (see below) |
| variables | array | no | | Variables to add (see below, may be skipped in 4.27) |

Component definition:
```json
{"name": "TriggerBox", "class": "BoxComponent", "attach_to": "DefaultSceneRoot"}
```

Variable definition:
```json
{"name": "Health", "type": "Float", "default_value": 100.0, "category": "Stats", "editable": true, "tooltip": "Current health"}
```

**Request:**
```json
{
  "name": "BP_MasterTrigger",
  "path": "/Game/Blueprints",
  "parent_class": "Actor",
  "components": [
    {"name": "DefaultSceneRoot", "class": "SceneComponent"},
    {"name": "TriggerBox", "class": "BoxComponent", "attach_to": "DefaultSceneRoot"}
  ],
  "variables": [
    {"name": "TriggerEnabled", "type": "Boolean", "category": "Conditions"}
  ]
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "BP_MasterTrigger",
    "path": "/Game/Blueprints/BP_MasterTrigger",
    "parent_class": "Actor",
    "components_added": ["DefaultSceneRoot", "TriggerBox"],
    "components_failed": [],
    "variables_added": ["TriggerEnabled"],
    "variables_failed": [],
    "variables_skipped_reason": null,
    "compiled": true,
    "compile_errors": []
  }
}
```

Variable creation may not work in UE4.27's Python API. If it fails, `variables_skipped_reason` explains why and `variables_failed` lists the names. The Blueprint and its components are still created.

**Error cases:**
- Missing `name`
- Path doesn't start with `/`
- Unknown parent class (error lists valid classes)
- Asset already exists at target path
- Component class not found
- Compile failed (reported in `compile_errors` but Blueprint is still created)

---

### blueprint_compile
Compile a Blueprint and return the result. `success: true` means the compile operation ran; check `compiled` and `had_errors` for the actual compile outcome.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| blueprint_path | string | yes | Asset path (must start with `/`) |

**Request:**
```json
{
  "blueprint_path": "/Game/Blueprints/BP_MasterTrigger"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "BP_MasterTrigger",
    "path": "/Game/Blueprints/BP_MasterTrigger",
    "compiled": true,
    "had_errors": false,
    "errors": [],
    "warnings": []
  }
}
```

**Error cases:**
- Asset not found
- Asset is not a Blueprint
- Compile crashes (caught and reported, won't kill the listener)

---

### blueprint_document
Generate a human-readable text summary of a Blueprint's structure.

**Parameters:**
| Name | Type | Required | Default | Description |
|------|------|----------|---------|-------------|
| blueprint_path | string | yes | | Asset path (must start with `/`) |
| detail_level | enum | no | standard | `"minimal"`, `"standard"`, or `"detailed"` |

Detail levels:
- **minimal**: Name, parent, compiled status, component/variable/function counts
- **standard**: Above plus component list, variable list with types and categories, function names, event graphs
- **detailed**: Above plus variable tooltips and defaults, function parameters, inheritance chain

**Request:**
```json
{
  "blueprint_path": "/Game/Blueprints/BP_MasterTrigger",
  "detail_level": "standard"
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "name": "BP_MasterTrigger",
    "path": "/Game/Blueprints/BP_MasterTrigger",
    "documentation": "# BP_MasterTrigger\nParent: Actor\nCompiled: Yes\n\n## Components (2)\n- DefaultSceneRoot (SceneComponent) [ROOT]\n  - TriggerBox (BoxComponent)\n\n## Variables (1)\n- TriggerEnabled : bool [Category: Conditions]\n\n## Event Graphs\n- EventGraph"
  }
}
```

**Error cases:** Same as `blueprint_info`.

---

### Blueprint Development Workflow

For creating new gameplay systems through the bridge:

1. `blueprint_create` with parent class, components, and variables
2. `blueprint_compile` to verify clean compilation
3. `blueprint_info` to confirm structure matches intent
4. `actor_spawn` to place an instance in the level
5. `viewport_focus` and `viewport_screenshot` to see it
6. `material_create` and `material_apply` if visual materials are needed
7. `viewport_screenshot` to verify final appearance
8. `level_save` when satisfied

For inspecting existing Blueprints:
1. `blueprint_list` to find what exists
2. `blueprint_info` for structural details
3. `blueprint_document` for a readable summary

---

## Operation Tools

### undo
Undo the last N operations.

### redo
Redo previously undone operations.

### history_list
Show operation history with timestamps and parameters.

### checkpoint_create
Create a named save point (level save + metadata snapshot).

### checkpoint_restore
Restore to a named checkpoint by undoing operations since it was created.

### batch_operations
Execute multiple tool calls in a single request with shared transaction.

## Blueprint Graph Editing Tools (2026-07-04)

Bridge to the C++ UBlueprintInspectorLibrary / UBlueprintMutatorLibrary.
Every pin connection is validated by UEdGraphSchema_K2::CanCreateConnection
before linking. Every mutation runs in a UE4 transaction, then compiles and
saves; compile failure returns success=false with compiler errors.

| Tool | Purpose | Key params |
|---|---|---|
| blueprint_inspect | Read graphs, members, components, nodes, pins | blueprint_path, action (graphs/functions/variables/macros/interfaces/event_dispatchers/components/nodes/node_detail/find_nodes), graph_name, node_guid, query |
| blueprint_add_variable | Add member variable | name, type (TypeDescriptor: {category, sub_category?, sub_category_object?, container_type?}), default_value, category |
| blueprint_remove_variable | Remove member variable | name |
| blueprint_set_variable_default | Set variable default | name, default_value |
| blueprint_add_function | Create user function graph | name, inputs [{name, type}], outputs |
| blueprint_remove_function | Remove function graph | name |
| blueprint_add_event_dispatcher | Create dispatcher | name, signature {parameters} |
| blueprint_remove_event_dispatcher | Remove dispatcher | name |
| blueprint_add_interface / blueprint_remove_interface | Interface implementation | interface_class (/Script/... or /Game/....BPI_X_C) |
| blueprint_component_remove / blueprint_component_rename | SCS component ops | component_name / old_name+new_name |
| blueprint_node_add | Add graph node, returns GUID | graph_name, node_type (CallFunction, Event, CustomEvent, VariableGet/Set, Branch, Sequence, Cast, SpawnActor, switches, input nodes, delegates, ...), position, config (camelCase keys: targetClass, functionName, eventName, varName, actorClass, structType, ...) |
| blueprint_node_delete / blueprint_node_move / blueprint_node_set_enabled | Node ops by GUID | graph_name, node_guid |
| blueprint_pins_connect | Schema-validated pin connection | graph_name, source_node_guid, source_pin, target_node_guid, target_pin |
| blueprint_pins_break | Break all links on a pin | graph_name, node_guid, pin_name |

## C++ and Job Tools (2026-07-04)

| Tool | Purpose | Key params |
|---|---|---|
| cpp_class_create | Generate UCLASS .h/.cpp in a game module (17 parent classes) | name (no prefix), parent_class, module, subdir, uclass_specifiers |
| cpp_build | UnrealBuildTool build as background job with parsed errors | target, platform, configuration, extra_args |
| job_status | Poll a job: status, parsed result, output tail | job_id, include_output |
| job_list | List session jobs | - |
| job_cancel | Cancel running job | job_id |

Notes: cpp_build reports compile errors normally while the editor is open;
link may fail with LNK1104 (locked DLLs, flagged distinctly). New classes
require an editor restart to load. Jobs do not survive editor restarts.

## Game Dev Workflow Tools (2026-07-04)

| Tool | Purpose | Key params |
|---|---|---|
| input_mapping_add / input_mapping_remove | Action/axis mappings (C++ helper; FKey not constructible from Python) | kind, name, key (FKey names: W, SpaceBar, MouseX, ...), scale, modifiers |
| input_preset_apply | Standard control schemes | preset (first_person/third_person/top_down/tank) |
| gameplay_framework_create | Linked GameMode/Character/PC/HUD/... Blueprints with GameMode defaults wired | path, base_name, pieces |
| project_settings_maps | Default maps + game mode (CDO + DefaultEngine.ini) | game_default_map, editor_startup_map, global_default_game_mode |
| camera_rig_create | SpringArm+Camera preset rigs on a Pawn/Character BP | blueprint_path, preset (first_person/third_person/top_down/side_scroller) |
| blackboard_create | BlackboardData asset with typed keys | path, name, keys [{name, type, base_class?}] |
| behavior_tree_create | BehaviorTree bound to a blackboard, graph from JSON (26 node types) | path, name, blackboard_path, keys, tree |
| ai_nav_rebuild | RebuildNavigation console command | - |

## Project Intelligence Tools (2026-07-04)

| Tool | Purpose |
|---|---|
| project_index_rebuild / project_index_query | File-backed searchable index of assets, Blueprints, materials, widgets, level actors under Saved/MCP/index |
| project_semantic_diff | Before/after semantic comparison of indexed categories |
| gameplay_pattern_search | Heuristic search for overlap logic, key-door flows, prompt widgets, objective chains, interactables |

## Content Tools: Materials, Data, Audio, Maps, Templates (2026-07-05)

| Tool | Purpose | Key params |
|---|---|---|
| material_instance_create | MaterialInstanceConstant from a parent with param overrides | path, name, parent_material, scalar_params {name:float}, vector_params {name:[r,g,b,a?]}, texture_params {name:path} |
| material_instance_set_params | Set params on an existing instance | material_instance, scalar_params, vector_params, texture_params |
| data_table_create | DataTable for a row struct, optional row fill | path, name, row_struct, rows_json |
| data_table_fill_from_json | Replace an existing DataTable's rows | data_table, rows_json |
| audio_component_add | AudioComponent on a Blueprint (+ optional SoundBase) | blueprint_path, component_name, sound, attach_to, auto_activate |
| level_new | Create + load + save a new level (switches loaded level; refuses if unsaved) | path, template |
| game_template_create | Playable skeleton: framework + camera rig + input + optional map/default GM | path, base_name, template (first_person/third_person/top_down/side_scroller), create_map, set_as_default |

Note: DataTable row filling uses the C++ `UMCPBridgeDataLibrary` wrapper around
`UDataTable::CreateTableFromJSONString`, NOT
`DataTableFunctionLibrary.fill_data_table_from_json_string`. The latter shows a
modal summary dialog on the game thread that hangs the bridge; the C++ path
returns import problems as structured data with no dialog.
