/**
 * Engine source navigation tools.
 *
 * These run entirely inside the MCP server process: they search and read
 * the local UE4.27 engine C++ source on disk. No HTTP call to the Python
 * listener is made, so they work while the editor is closed -- which is
 * exactly when C++ iteration happens (new classes need an editor restart
 * to load).
 *
 * They complement the reflection-based unreal-api lookup: that database
 * covers UCLASS/USTRUCT/UENUM types but misses plain C++ symbols -- Slate
 * widgets (SCompoundWidget), utility classes (FEditorFileUtils), low-level
 * types (FRunnable, FScreenshotRequest), and macros. These tools grep the
 * real headers, so any symbol that compiles can be found.
 *
 * Engine root resolution order:
 *   1. UE_ENGINE_ROOT env var (explicit config wins; fails loud if wrong)
 *   2. EngineAssociation from the .uproject in the server's cwd, resolved
 *      through the Windows registry (launcher installs under HKLM, source
 *      builds under HKCU\...\Builds) or used directly if it is a path
 */

import { execFileSync } from "child_process";
import { existsSync, readFileSync, readdirSync, statSync, Stats } from "fs";
import { join, relative, resolve, sep } from "path";
import { z } from "zod";
import type { ToolDefinition } from "../types.js";

const SCOPE_DIRS: Record<string, string[]> = {
  Runtime: [join("Source", "Runtime")],
  Editor: [join("Source", "Editor")],
  Developer: [join("Source", "Developer")],
  Plugins: ["Plugins"],
  All: [
    join("Source", "Runtime"),
    join("Source", "Editor"),
    join("Source", "Developer"),
    "Plugins",
  ],
};

// Directories that never contain useful C++ declarations (or are huge).
const SKIP_DIRS = new Set([
  "ThirdParty", "Programs", "Intermediate", "Binaries",
  "Content", "Resources", "Shaders",
]);

const MAX_READ_SPAN = 400;

interface EngineMatch {
  file: string;
  line: number;
  text: string;
  context: string[];
  include_hint?: string;
}

// ---- Engine root resolution ----

function queryRegValue(key: string, valueName: string): string | null {
  if (process.platform !== "win32") return null;
  try {
    const out = execFileSync("reg", ["query", key, "/v", valueName], {
      encoding: "utf-8",
      stdio: ["ignore", "pipe", "ignore"],
    });
    for (const line of out.split(/\r?\n/)) {
      const m = line.match(/REG_SZ\s+(.+)$/);
      if (m) return m[1].trim();
    }
  } catch {
    // Key or value not present.
  }
  return null;
}

function readEngineAssociation(): string | null {
  try {
    const uproject = readdirSync(process.cwd()).find((f) => f.endsWith(".uproject"));
    if (!uproject) return null;
    const parsed = JSON.parse(
      readFileSync(join(process.cwd(), uproject), "utf-8")
    ) as { EngineAssociation?: string };
    return parsed.EngineAssociation ?? null;
  } catch {
    return null;
  }
}

/** Exported so setup tooling resolves the engine the same way the tools do.
    Scripts/resolve-engine-root.mjs is the only other caller; do not add a
    second implementation of this in PowerShell or Python. */
export function resolveEngineRoot(): string {
  const envRoot = process.env.UE_ENGINE_ROOT;
  if (envRoot) {
    if (existsSync(join(envRoot, "Engine", "Source"))) return envRoot;
    throw new Error(
      `UE_ENGINE_ROOT is set to "${envRoot}" but "${join(envRoot, "Engine", "Source")}" does not exist. ` +
      "Point UE_ENGINE_ROOT at the engine root (the directory containing Engine/Source)."
    );
  }
  const assoc = readEngineAssociation();
  if (assoc) {
    if (/^\d+\.\d+$/.test(assoc)) {
      const dir = queryRegValue(
        `HKLM\\SOFTWARE\\EpicGames\\Unreal Engine\\${assoc}`,
        "InstalledDirectory"
      );
      if (dir && existsSync(join(dir, "Engine", "Source"))) return dir;
    } else {
      const dir = queryRegValue("HKCU\\Software\\Epic Games\\Unreal Engine\\Builds", assoc);
      if (dir && existsSync(join(dir, "Engine", "Source"))) return dir;
      if (existsSync(join(assoc, "Engine", "Source"))) return assoc;
    }
  }
  throw new Error(
    "Could not locate the UE engine install (no UE_ENGINE_ROOT, and the .uproject " +
    "EngineAssociation did not resolve). Set the UE_ENGINE_ROOT environment variable " +
    "to the engine root (the directory containing Engine/Source)."
  );
}

// ---- File enumeration ----

const fileListCache = new Map<string, string[]>();

function walk(dir: string, exts: string[], out: string[]): void {
  let entries: string[];
  try {
    entries = readdirSync(dir);
  } catch {
    return;
  }
  for (const entry of entries) {
    const full = join(dir, entry);
    let st: Stats;
    try {
      st = statSync(full);
    } catch {
      continue;
    }
    if (st.isDirectory()) {
      if (!SKIP_DIRS.has(entry)) walk(full, exts, out);
    } else if (exts.some((e) => entry.endsWith(e))) {
      out.push(full);
    }
  }
}

function listSourceFiles(root: string, scope: string, includeCpp: boolean): string[] {
  const key = `${root}|${scope}|${includeCpp}`;
  const cached = fileListCache.get(key);
  if (cached) return cached;
  const exts = includeCpp ? [".h", ".inl", ".cpp"] : [".h", ".inl"];
  const files: string[] = [];
  for (const rel of SCOPE_DIRS[scope]) {
    walk(join(root, "Engine", rel), exts, files);
  }
  fileListCache.set(key, files);
  return files;
}

// ---- Search ----

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/** Regexes that match a C++ declaration of the given type/macro name. */
function buildDeclarationRegexes(symbol: string): RegExp[] {
  const sym = escapeRegex(symbol);
  return [
    new RegExp(
      `(?:class|struct|enum(?:\\s+class)?)\\s+(?:[A-Z][A-Za-z0-9_]*_API\\s+)?${sym}\\b`
    ),
    new RegExp(
      `(?:using\\s+${sym}\\s*=|typedef\\s+[^;]*\\b${sym}\\s*;|#\\s*define\\s+${sym}\\b)`
    ),
  ];
}

/** The #include path a consumer would write, when derivable from the file location. */
function includeHint(relFile: string): string | undefined {
  for (const marker of ["/Public/", "/Classes/"]) {
    const idx = relFile.indexOf(marker);
    if (idx >= 0) return relFile.slice(idx + marker.length);
  }
  return undefined;
}

interface SearchOutcome {
  matches: EngineMatch[];
  files_scanned: number;
  truncated: boolean;
}

function searchFiles(
  root: string,
  files: string[],
  moduleFilter: string | undefined,
  regexes: RegExp[],
  literalNeedle: string | undefined,
  maxResults: number,
  contextLines: number
): SearchOutcome {
  const engineDir = join(root, "Engine");
  const matches: EngineMatch[] = [];
  let scanned = 0;
  let truncated = false;
  // Match the module name against the path relative to Engine/, never the
  // absolute path: the engine install root itself contains an "Engine"
  // segment, which would make module filters like "Engine" match every file.
  const moduleNeedle = moduleFilter ? `/${moduleFilter}/` : null;

  outer: for (const file of files) {
    const rel = relative(engineDir, file).split(sep).join("/");
    if (moduleNeedle && !`/${rel}`.includes(moduleNeedle)) continue;
    let content: string;
    try {
      content = readFileSync(file, "utf-8");
    } catch {
      continue;
    }
    scanned += 1;
    // Cheap substring pre-check before line splitting and regex work.
    if (literalNeedle && !content.includes(literalNeedle)) continue;
    const lines = content.split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      if (regexes.some((r) => r.test(lines[i]))) {
        matches.push({
          file: rel,
          line: i + 1,
          text: lines[i].trimEnd(),
          context: lines
            .slice(Math.max(0, i - contextLines), i + contextLines + 1)
            .map((l) => l.trimEnd()),
          include_hint: includeHint(rel),
        });
        if (matches.length >= maxResults) {
          truncated = true;
          break outer;
        }
      }
    }
  }
  return { matches, files_scanned: scanned, truncated };
}

// ---- Tool definitions ----

const EngineSourceSearchInputSchema = z.object({
  symbol: z.string().optional().describe(
    "Type, struct, enum, or macro name to locate the declaration of, e.g. " +
    "'FScreenshotRequest' or 'SCompoundWidget'. Falls back to plain reference " +
    "search if no declaration matches. Provide exactly one of symbol/pattern."
  ),
  pattern: z.string().optional().describe(
    "Regex applied per source line, e.g. 'SaveCurrentLevel' or " +
    "'virtual.*BeginPlay'. Provide exactly one of symbol/pattern."
  ),
  scope: z.enum(["Runtime", "Editor", "Developer", "Plugins", "All"])
    .default("All")
    .describe("Engine source subtree to search. Default All (skips ThirdParty/Programs)."),
  module: z.string().optional().describe(
    "Module directory name filter, e.g. 'Engine', 'SlateCore', 'UnrealEd'. " +
    "Strongly recommended for pattern searches; keeps scans fast."
  ),
  include_cpp: z.boolean().default(false)
    .describe("Also search .cpp files (implementations). Default headers only."),
  ignore_case: z.boolean().default(false).describe("Case-insensitive matching."),
  context_lines: z.number().int().min(0).max(10).default(2)
    .describe("Lines of context around each match."),
  max_results: z.number().int().min(1).max(100).default(20),
});

const EngineSourceReadInputSchema = z.object({
  file: z.string().describe(
    "Path relative to <EngineRoot>/Engine, as returned by engine_source_search, " +
    "e.g. 'Source/Runtime/Engine/Public/UnrealClient.h'."
  ),
  start_line: z.number().int().min(1).default(1),
  end_line: z.number().int().min(1).optional()
    .describe(`Default start_line+119. Span capped at ${MAX_READ_SPAN} lines.`),
});

function errorResult(message: string): { content: Array<{ type: "text"; text: string }> } {
  return {
    content: [{
      type: "text" as const,
      text: JSON.stringify({ success: false, data: {}, error: message }, null, 2),
    }],
  };
}

export function createEngineSourceTools(): ToolDefinition[] {
  return [
    {
      name: "engine_source_search",
      description:
        "Search the local UE4.27 engine C++ source on disk for a symbol declaration " +
        "or regex. Covers plain C++ the reflection database misses (Slate widgets, " +
        "FRunnable, editor utilities, macros). Returns file, line, matched text, and " +
        "the #include path when derivable. Works with the editor closed.",
      inputSchema: EngineSourceSearchInputSchema,
      handler: async (params) => {
        let args: z.infer<typeof EngineSourceSearchInputSchema>;
        try {
          args = EngineSourceSearchInputSchema.parse(params);
        } catch (err) {
          return errorResult(`Invalid parameters: ${(err as Error).message}`);
        }
        if ((args.symbol === undefined) === (args.pattern === undefined)) {
          return errorResult("Provide exactly one of 'symbol' or 'pattern'.");
        }
        let root: string;
        try {
          root = resolveEngineRoot();
        } catch (err) {
          return errorResult((err as Error).message);
        }

        const files = listSourceFiles(root, args.scope, args.include_cpp);
        const flags = args.ignore_case ? "i" : "";
        let note: string | undefined;
        let outcome: SearchOutcome;

        try {
          if (args.symbol !== undefined) {
            const declRegexes = buildDeclarationRegexes(args.symbol).map(
              (r) => new RegExp(r.source, flags)
            );
            outcome = searchFiles(
              root, files, args.module, declRegexes,
              args.ignore_case ? undefined : args.symbol,
              args.max_results, args.context_lines
            );
            if (outcome.matches.length === 0) {
              const refRegex = new RegExp(`\\b${escapeRegex(args.symbol)}\\b`, flags);
              outcome = searchFiles(
                root, files, args.module, [refRegex],
                args.ignore_case ? undefined : args.symbol,
                args.max_results, args.context_lines
              );
              if (outcome.matches.length > 0) {
                note = "No declaration matched; showing plain references instead.";
              }
            }
          } else {
            const regex = new RegExp(args.pattern as string, flags);
            outcome = searchFiles(
              root, files, args.module, [regex], undefined,
              args.max_results, args.context_lines
            );
          }
        } catch (err) {
          return errorResult(`Search failed: ${(err as Error).message}`);
        }

        return {
          content: [{
            type: "text" as const,
            text: JSON.stringify({
              success: true,
              data: {
                engine_root: root,
                scope: args.scope,
                module: args.module ?? null,
                files_scanned: outcome.files_scanned,
                truncated: outcome.truncated,
                note: note ?? null,
                matches: outcome.matches,
              },
            }, null, 2),
          }],
        };
      },
    },
    {
      name: "engine_source_read",
      description:
        "Read a line range from a UE4.27 engine source file on disk (path as returned " +
        "by engine_source_search, relative to <EngineRoot>/Engine). Use to see full " +
        "declarations, parameter lists, and comments. Works with the editor closed.",
      inputSchema: EngineSourceReadInputSchema,
      handler: async (params) => {
        let args: z.infer<typeof EngineSourceReadInputSchema>;
        try {
          args = EngineSourceReadInputSchema.parse(params);
        } catch (err) {
          return errorResult(`Invalid parameters: ${(err as Error).message}`);
        }
        let root: string;
        try {
          root = resolveEngineRoot();
        } catch (err) {
          return errorResult((err as Error).message);
        }

        const engineDir = resolve(join(root, "Engine"));
        const target = resolve(join(engineDir, args.file));
        if (!target.startsWith(engineDir + sep)) {
          return errorResult(
            `Path "${args.file}" resolves outside the engine directory; refusing to read it.`
          );
        }
        if (!existsSync(target)) {
          return errorResult(`File not found: ${args.file}`);
        }

        let lines: string[];
        try {
          lines = readFileSync(target, "utf-8").split(/\r?\n/);
        } catch (err) {
          return errorResult(`Read failed: ${(err as Error).message}`);
        }

        const start = args.start_line;
        let end = args.end_line ?? start + 119;
        if (end < start) end = start;
        if (end - start + 1 > MAX_READ_SPAN) end = start + MAX_READ_SPAN - 1;
        const slice = lines.slice(start - 1, end);
        const numbered = slice.map((l, i) => `${start + i}\t${l.trimEnd()}`);

        return {
          content: [{
            type: "text" as const,
            text: JSON.stringify({
              success: true,
              data: {
                file: args.file,
                start_line: start,
                end_line: start + slice.length - 1,
                total_lines: lines.length,
                text: numbered.join("\n"),
              },
            }, null, 2),
          }],
        };
      },
    },
  ];
}
