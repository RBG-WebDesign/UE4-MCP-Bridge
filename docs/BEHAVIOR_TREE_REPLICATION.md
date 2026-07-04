# Behavior Tree Replication Workflow

Use this workflow whenever an agent or tool needs to read, inspect, duplicate, replicate, compare, or manipulate Behavior Trees in this UE4.27 project.

## Default Method

Use the editor C++ helper module:

```text
Sinfeld_DemoEditor
USFBehaviorTreeReplicationLibrary
```

This is the default because UE4.27 protects the core `UBehaviorTree` properties from Python:

- `RootNode`
- `BlackboardAsset`
- `RootDecorators`
- `RootDecoratorOps`

Python can see asset references, but it cannot reliably read or rebuild the full tree graph by itself.

## Export Selected Behavior Tree

Select a Behavior Tree in the Content Browser, then run this in Unreal Python:

```python
bt = unreal.SFBehaviorTreeReplicationLibrary.get_first_selected_behavior_tree()
json_text = unreal.SFBehaviorTreeReplicationLibrary.export_behavior_tree_to_json(bt)
print(json_text)
```

Save the selected tree to a JSON file:

```python
bt = unreal.SFBehaviorTreeReplicationLibrary.get_first_selected_behavior_tree()
ok, msg = unreal.SFBehaviorTreeReplicationLibrary.export_behavior_tree_to_json_file(
    bt,
    r"D:\Unreal Projects\MASTER_PROJECT\SF_Repository\Sinfeld_240301\Saved\BehaviorTrees\BT_Selected.json"
)
print(ok, msg)
```

## Duplicate For Replication

Use duplication as the first step when replicating a complex Behavior Tree:

```python
bt = unreal.SFBehaviorTreeReplicationLibrary.get_first_selected_behavior_tree()
new_bt, msg = unreal.SFBehaviorTreeReplicationLibrary.duplicate_behavior_tree_asset(
    bt,
    "/Game/00_SinfeldER/Blueprints/Enemy_AI/Behavior_Tree/Behavior_tree",
    "BT_MouthBreather_Copy",
    True
)
print(new_bt, msg)
```

Pass `True` for `bDuplicateBlackboard` when the copy should have its own blackboard. Pass `False` when the copy should keep using the original shared blackboard.

## Agent Rules

- Prefer the C++ bridge for Behavior Tree work.
- Use Python asset registry only for quick references and dependency scans.
- Do not parse or edit `.uasset` files directly.
- Do not assume the open editor tab exposes unsaved graph edits to Python.
- After C++ bridge changes, rebuild `Sinfeld_DemoEditor` and restart Unreal Editor.

