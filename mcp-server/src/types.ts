/**
 * Shared type definitions for the MCP server.
 */

import { z } from "zod";
import type { ToolAnnotations } from "@modelcontextprotocol/sdk/types.js";

export interface ToolDefinition {
  name: string;
  description: string;
  inputSchema: z.ZodType;
  /** Optional per-tool MCP annotations. Tools without this field fall back
      to the central map in annotations.ts, which covers every tool. */
  annotations?: ToolAnnotations;
  handler: (params: Record<string, unknown>) => Promise<{
    content: Array<{ type: "text"; text: string }>;
  }>;
}
