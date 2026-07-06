# <System Name>

Status: <Draft | Verified YYYY-MM-DD>
Engine: UE4.27
Origin: <project / session where this was solved>

## 1. System Design Intent

<One paragraph: the problem this solves and the shape of the solution.
Write it so an agent can decide from this paragraph alone whether the
playbook applies to the task at hand.>

## 2. Dependencies

- C++ classes: <exact classes, base classes, module>
- Engine APIs: <functions verified against 4.27 headers, with header paths>
- Content assets: <materials, widgets, meshes the system needs>
- Build.cs modules: <modules that must be in the dependency list>

## 3. How-To Graph Logic

<Textual or pseudocode map of the runtime flow. Name real functions and
real widget/actor names. Example form:>

```
BeginPlay
  -> CreateWidget(TitleWidgetClass) -> AddToViewport
  -> EnsureCueWidgetsExist()   (create missing groups at runtime)
  -> WrapCueGroupsInRetainers() (RemoveChildAt + AddChildToCanvas, never ReplaceChildAt)
Tick (auto-drive)
  -> frame = (now - start) * TargetFPS
  -> UpdateTitlesAtFrame(frame)
```

## 4. Replication Steps

<The ordered, executable steps: MCP tool calls, python_proxy snippets,
file edits, build commands. An agent should be able to walk this list
top to bottom in a fresh project.>

## 5. UE4.27 Legacy Gotchas

<Every engine-specific trap hit while building this. Each entry: the
symptom, the cause, the fix. These are the reason the playbook exists.>

## 6. Verification

<The exact checks that prove the system works: what to freeze, what to
screenshot, what property to read, what log line to grep. Include the
failure signature so a broken state is recognizable.>
