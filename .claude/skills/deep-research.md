---
name: deep-research
description: >
  Dispatch architecture, API, or design questions to a research agent for thorough
  answers. Use this whenever a superpowers skill asks you a question you cannot
  confidently answer from what's already in context, or when you need to understand
  existing code patterns before implementing something new.
---

# Deep Research Skill

## When to Use

Use this skill when ANY of the following arise during your work:

1. A superpowers skill (brainstorming, writing-plans, debugging, etc.) generates a question
   about how something works in this project
2. You need to understand an existing code pattern before writing new code
3. You're unsure about a UE4.27 API, class hierarchy, or engine behavior
4. You need to check what a spec or plan says about a design decision
5. You need to trace how data flows across the TS/Python/C++ boundary

## How to Use

**Dispatch an Agent tool call** with the question. Use `subagent_type: "general-purpose"`
and `model: "opus"` for the best answers. Include enough context in the prompt for
the agent to know what to search for.

### Prompt Template

```
Research question for the UE_Bridge project:

[THE QUESTION]

Context: [Why you're asking -- what skill/task triggered this question]

Instructions:
- Search the codebase for relevant code, specs, and plans
- Check these locations first: [suggest 2-3 likely files/directories]
- Return a direct answer with file paths and line numbers
- If you find relevant code examples, include them
- Flag any uncertainties with confidence levels
- Do NOT write or modify any code
```

### Routing Hints

Pick the right search focus based on the question type:

| Question Type | Where to Look First |
|---|---|
| UE4 C++ API / class hierarchy | `Plugins/MCPBridge/Source/BlueprintGraphBuilder/` source, then engine headers |
| How a tool works end-to-end | `mcp-server/src/tools/`, `Plugins/MCPBridge/Content/Python/mcp_bridge/.../handlers/`, `router.py` |
| What a spec decided | `docs/superpowers/specs/` |
| What's left in a plan | `docs/superpowers/plans/` |
| Generation pipeline | `Plugins/MCPBridge/Content/Python/mcp_bridge/.../generation/` |
| JSON schema format | Specs first, then `spec_schema.py`, then existing mechanic files |
| Build/module dependencies | `Build.cs`, `package.json` |
| Threading / game thread | `listener.py`, CLAUDE.md threading section |

## Example Usage

During brainstorming, the skill asks: "How does the existing BlueprintGraphBuilder
handle editor graph sync after building runtime nodes?"

Instead of guessing, dispatch:

```
Agent(
  description="Research editor graph sync",
  model="opus",
  prompt="Research question for the UE_Bridge project: How does the existing BlueprintGraphBuilder handle editor graph sync after building runtime nodes? Context: Planning the BehaviorTree builder's editor graph sync phase. Instructions: Search Plugins/MCPBridge/Source/BlueprintGraphBuilder/ for any editor graph reconstruction code. Check specs at docs/superpowers/specs/2026-03-17-blueprint-graph-builder-design.md. Return the pattern with file paths. Do NOT write or modify any code."
)
```

## Rules

- Always dispatch for questions about UE4 internals you haven't seen in this conversation
- Always dispatch when a superpowers skill asks about project-specific patterns
- Do NOT dispatch for simple questions you can answer from CLAUDE.md or files already in context
- The agent is research-only. It should never write or modify code.
- Trust the agent's answer but verify file paths exist before using them
