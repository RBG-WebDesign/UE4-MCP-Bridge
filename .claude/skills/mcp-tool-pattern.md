---
name: mcp-tool-pattern
description: >
  Template and conventions for adding new MCP tools to the Unreal Bridge.
  Use whenever creating a new tool, modifying tool schemas, or adding
  handler endpoints.
---

# Adding a New Tool End-to-End

## Step 1: TypeScript Tool Definition (`mcp-server/src/tools/<category>.ts`)

```typescript
import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const MyToolInputSchema = z.object({
  myParam: z.string().describe("Description of the parameter"),
  optionalParam: z.number().optional().describe("Optional numeric param"),
});

export function createMyTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "my_tool_name",
      description: "What this tool does in one sentence.",
      inputSchema: MyToolInputSchema,
      handler: async (params: z.infer<typeof MyToolInputSchema>) => {
        const result = await client.sendCommand("my_command", {
          my_param: params.myParam,
          optional_param: params.optionalParam,
        });
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
  ];
}
```

## Step 2: Register in index.ts

```typescript
import { createMyTools } from "./tools/my-category.js";

// Inside the tool registration block:
const myTools = createMyTools(client);
allTools.push(...myTools);
```

## Step 3: Python Router Entry (`Plugins/MCPBridge/Content/Python/mcp_bridge/router.py`)

```python
from mcp_bridge.handlers.my_category import handle_my_command

COMMAND_ROUTES = {
    # ... existing routes ...
    "my_command": handle_my_command,
}
```

## Step 4: Python Handler (`Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/my_category.py`)

```python
from typing import Any, Dict

def handle_my_command(params: Dict[str, Any]) -> Dict[str, Any]:
    """Does the thing the command is supposed to do.
    
    Args:
        params: Dict with keys 'my_param' (str) and optional 'optional_param' (int).
    
    Returns:
        Standard response dict: {success: bool, data: dict, error?: str}
    """
    try:
        my_param = params.get("my_param", "")
        
        # Do the unreal work here
        import unreal
        # ... unreal API calls ...
        
        return {
            "success": True,
            "data": {
                "result": "whatever the result is"
            }
        }
    except Exception as e:
        return {
            "success": False,
            "data": {},
            "error": str(e)
        }
```

## Naming Conventions
- TypeScript tool names: `snake_case` (e.g., `actor_spawn`, `viewport_screenshot`)
- Python command names: same `snake_case` as the tool name
- Python handler functions: `handle_<command_name>`
- TypeScript Zod schemas: `PascalCase` + `InputSchema` suffix

## Response Format
Every handler returns:
```json
{
  "success": true,
  "data": { ... },
  "error": "only present if success is false"
}
```
