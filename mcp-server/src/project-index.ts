/**
 * Persistent incremental project intelligence: the editor-free core.
 *
 * Everything here reads a UE4.27 project from disk. No named pipe, no
 * editor, no Python. It is the same class of code as engine-source.ts:
 * server-local, works with Unreal closed, which is exactly when C++ and
 * config iteration happens.
 *
 * What it does NOT do: anything that needs the editor's reflection or asset
 * registry. Blueprint members, material graphs and asset reference edges are
 * designed in docs/PROJECT_INTELLIGENCE.md and supplied by existing native
 * commands (puerts_graph_inspect, puerts_find_assets). This module consumes
 * those; it never reimplements them.
 *
 * The store is a cache. Deleting it costs one full re-index and nothing else.
 */

import {
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  statSync,
  writeFileSync,
} from "fs";
import { basename, dirname, join, relative, resolve, sep } from "path";

/** Bump on any change to the persisted shape. A mismatch forces a full re-index. */
export const PROJECT_INDEX_SCHEMA_VERSION = 1;

// ---- Persisted shape ----

export type FileKind =
  | "cpp_header"
  | "cpp_source"
  | "build_cs"
  | "config_ini"
  | "content_asset";

export type InvalidationReason =
  | "new"
  | "size_changed"
  | "mtime_changed"
  | "schema_version_changed";

export type SymbolKind =
  | "class"
  | "struct"
  | "enum"
  | "interface"
  | "function"
  | "property";

export interface SymbolFact {
  name: string;
  kind: SymbolKind;
  line: number;
  /** Owning UBT module, taken from the nearest ancestor *.Build.cs. */
  module: string;
  /** True when the declaration carried a UCLASS/USTRUCT/UENUM/UINTERFACE macro. */
  reflected: boolean;
  /** First base class, when the declaration had one. */
  base?: string;
  /** Enclosing type for kind "function" and "property". */
  owner?: string;
}

export interface ModuleFact {
  name: string;
  publicDependencies: string[];
  privateDependencies: string[];
}

export interface InputMappingFact {
  kind: "action" | "axis";
  name: string;
  key: string;
  scale?: number;
}

export interface GameplayTagFact {
  tag: string;
  comment?: string;
}

export interface ContentAssetFact {
  /** Long package name, for example /Game/MCPTests/ConsolidatedMilestone. */
  packagePath: string;
  assetKind: "map" | "asset";
}

export interface FileEntry {
  kind: FileKind;
  size: number;
  mtimeMs: number;
  /** ISO timestamp of the last time this entry was actually parsed. */
  indexedAt: string;
  /** Why this entry was last parsed instead of reused. */
  invalidatedBy: InvalidationReason;
  symbols?: SymbolFact[];
  module?: ModuleFact;
  inputMappings?: InputMappingFact[];
  gameplayTags?: GameplayTagFact[];
  asset?: ContentAssetFact;
}

export interface ProjectIndex {
  schemaVersion: number;
  /** Absolute project root the index was built from. A change forces a re-index. */
  projectRoot: string;
  generatedAt: string;
  /** Keyed by project-relative path with forward slashes. */
  files: Record<string, FileEntry>;
}

export interface IndexStats {
  storePath: string;
  scanned: number;
  indexed: number;
  skipped: number;
  removed: string[];
  reasons: Record<InvalidationReason, number>;
  durationMs: number;
}

export interface IndexOptions {
  /** Defaults to <projectRoot>/Saved/BridgeIntelligence/project-index.json. */
  storePath?: string;
  /** Index Plugins/<name>/Source as well as Source. Default true. */
  includePlugins?: boolean;
  /** Set false to compute without persisting. Default true. */
  write?: boolean;
}

export interface IndexResult {
  success: boolean;
  data: { index: ProjectIndex; stats: IndexStats } | null;
  error?: string;
}

// ---- Scanning ----

const SKIP_DIRS = new Set([
  "Intermediate",
  "Binaries",
  "Saved",
  "DerivedDataCache",
  "ThirdParty",
  "node_modules",
  ".git",
  ".vs",
]);

interface ScannedFile {
  rel: string;
  abs: string;
  kind: FileKind;
}

function toPosix(p: string): string {
  return p.split(sep).join("/");
}

function walk(dir: string, onFile: (abs: string) => void): void {
  let entries: string[];
  try {
    entries = readdirSync(dir);
  } catch {
    return;
  }
  for (const name of entries) {
    const abs = join(dir, name);
    let isDir: boolean;
    try {
      isDir = statSync(abs).isDirectory();
    } catch {
      continue;
    }
    if (isDir) {
      if (SKIP_DIRS.has(name)) continue;
      walk(abs, onFile);
    } else {
      onFile(abs);
    }
  }
}

function classifySource(abs: string): FileKind | null {
  const name = basename(abs);
  if (name.endsWith(".Build.cs")) return "build_cs";
  if (name.endsWith(".h") || name.endsWith(".hpp")) return "cpp_header";
  if (name.endsWith(".cpp")) return "cpp_source";
  return null;
}

function collectFiles(root: string, includePlugins: boolean): ScannedFile[] {
  const found: ScannedFile[] = [];
  const push = (abs: string, kind: FileKind): void => {
    found.push({ rel: toPosix(relative(root, abs)), abs, kind });
  };

  const sourceRoots: string[] = [join(root, "Source")];
  if (includePlugins) {
    const pluginsDir = join(root, "Plugins");
    if (existsSync(pluginsDir)) {
      for (const name of readdirSync(pluginsDir)) {
        const src = join(pluginsDir, name, "Source");
        if (existsSync(src)) sourceRoots.push(src);
      }
    }
  }
  for (const src of sourceRoots) {
    if (!existsSync(src)) continue;
    walk(src, (abs) => {
      const kind = classifySource(abs);
      if (kind) push(abs, kind);
    });
  }

  const configDir = join(root, "Config");
  if (existsSync(configDir)) {
    walk(configDir, (abs) => {
      if (abs.endsWith(".ini")) push(abs, "config_ini");
    });
  }

  const contentDir = join(root, "Content");
  if (existsSync(contentDir)) {
    walk(contentDir, (abs) => {
      if (abs.endsWith(".uasset") || abs.endsWith(".umap")) push(abs, "content_asset");
    });
  }

  return found;
}

// ---- Module attribution ----

/** Nearest ancestor directory holding a *.Build.cs names the module. */
function moduleForFile(abs: string, cache: Map<string, string>): string {
  let dir = dirname(abs);
  const seen: string[] = [];
  for (;;) {
    const hit = cache.get(dir);
    if (hit !== undefined) {
      for (const d of seen) cache.set(d, hit);
      return hit;
    }
    seen.push(dir);
    let names: string[] = [];
    try {
      names = readdirSync(dir);
    } catch {
      // Unreadable directory, keep walking up.
    }
    const build = names.find((n) => n.endsWith(".Build.cs"));
    if (build) {
      const moduleName = build.slice(0, -".Build.cs".length);
      for (const d of seen) cache.set(d, moduleName);
      return moduleName;
    }
    const parent = dirname(dir);
    if (parent === dir) {
      for (const d of seen) cache.set(d, "");
      return "";
    }
    dir = parent;
  }
}

// ---- C++ parsing ----
//
// ponytail: line regexes, not a C++ parser. Template declarations, macros that
// hide the class name, and multi-line base lists are missed. Upgrade path is
// libclang or the UnrealHeaderTool manifest if the miss rate ever matters.

const RE_REFLECT = /^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(/;
const RE_CLASS = /^\s*class\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_]\w*)\b\s*(?::\s*public\s+([A-Za-z_]\w*))?/;
const RE_STRUCT = /^\s*struct\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_]\w*)\b\s*(?::\s*public\s+([A-Za-z_]\w*))?/;
const RE_ENUM = /^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*)\b/;
const RE_MEMBER_MACRO = /^\s*(UFUNCTION|UPROPERTY)\s*\(/;
const RE_FUNC_NAME = /\b([A-Za-z_]\w*)\s*\(/;
const RE_PROP_NAME = /\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;\s*$/;

const NON_NAMES = new Set([
  "if", "for", "while", "switch", "return", "sizeof", "static_cast",
  "const_cast", "reinterpret_cast", "explicit", "virtual", "inline",
]);

function parseCpp(text: string, module: string): SymbolFact[] {
  const lines = text.split(/\r?\n/);
  const symbols: SymbolFact[] = [];
  let pendingReflect: string | null = null;
  let pendingMember: string | null = null;
  let currentType = "";

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const trimmed = line.trim();
    if (trimmed === "" || trimmed.startsWith("//") || trimmed.startsWith("*")) continue;

    const reflect = RE_REFLECT.exec(line);
    if (reflect) {
      pendingReflect = reflect[1];
      continue;
    }
    const memberMacro = RE_MEMBER_MACRO.exec(line);
    if (memberMacro) {
      pendingMember = memberMacro[1];
      continue;
    }

    if (pendingMember) {
      const kind: SymbolKind = pendingMember === "UFUNCTION" ? "function" : "property";
      const re = kind === "function" ? RE_FUNC_NAME : RE_PROP_NAME;
      const m = re.exec(trimmed);
      if (m && !NON_NAMES.has(m[1])) {
        symbols.push({
          name: m[1],
          kind,
          line: i + 1,
          module,
          reflected: true,
          owner: currentType || undefined,
        });
        pendingMember = null;
      }
      // A declaration that spans lines keeps pendingMember set for the next line.
      continue;
    }

    // Forward declarations carry no information worth storing.
    const isForward = /;\s*$/.test(trimmed);

    const cls = RE_CLASS.exec(line);
    if (cls && !isForward) {
      currentType = cls[1];
      symbols.push({
        name: cls[1],
        kind: pendingReflect === "UINTERFACE" ? "interface" : "class",
        line: i + 1,
        module,
        reflected: pendingReflect !== null,
        base: cls[2],
      });
      pendingReflect = null;
      continue;
    }
    const str = RE_STRUCT.exec(line);
    if (str && !isForward) {
      currentType = str[1];
      symbols.push({
        name: str[1],
        kind: "struct",
        line: i + 1,
        module,
        reflected: pendingReflect !== null,
        base: str[2],
      });
      pendingReflect = null;
      continue;
    }
    const enm = RE_ENUM.exec(line);
    if (enm && !isForward) {
      symbols.push({
        name: enm[1],
        kind: "enum",
        line: i + 1,
        module,
        reflected: pendingReflect !== null,
      });
      pendingReflect = null;
      continue;
    }
    pendingReflect = null;
  }
  return symbols;
}

// ---- Build.cs parsing ----

const RE_DEPENDENCIES =
  /(Public|Private)DependencyModuleNames\.(?:AddRange\s*\(\s*new\s+string\[\]\s*\{([\s\S]*?)\}|Add\s*\(\s*"([^"]+)"\s*\))/g;

function stripLineComments(text: string): string {
  return text.replace(/\/\/[^\n]*/g, "");
}

function parseBuildCs(text: string, moduleName: string): ModuleFact {
  const fact: ModuleFact = {
    name: moduleName,
    publicDependencies: [],
    privateDependencies: [],
  };
  const clean = stripLineComments(text);
  RE_DEPENDENCIES.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = RE_DEPENDENCIES.exec(clean)) !== null) {
    const target = m[1] === "Public" ? fact.publicDependencies : fact.privateDependencies;
    if (m[3]) {
      target.push(m[3]);
      continue;
    }
    const body = m[2] ?? "";
    for (const q of body.matchAll(/"([^"]+)"/g)) target.push(q[1]);
  }
  return fact;
}

// ---- Config parsing ----

const RE_ACTION = /^\+?ActionMappings\s*=\s*\((.*)\)\s*$/;
const RE_AXIS = /^\+?AxisMappings\s*=\s*\((.*)\)\s*$/;
const RE_TAG = /^\+?GameplayTagList\s*=\s*\((.*)\)\s*$/;

function field(body: string, name: string): string | undefined {
  const m = new RegExp(`${name}\\s*=\\s*"?([^",)]*)"?`).exec(body);
  return m ? m[1] : undefined;
}

interface ConfigFacts {
  inputMappings: InputMappingFact[];
  gameplayTags: GameplayTagFact[];
}

function parseIni(text: string): ConfigFacts {
  const inputMappings: InputMappingFact[] = [];
  const gameplayTags: GameplayTagFact[] = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    const action = RE_ACTION.exec(line);
    if (action) {
      const name = field(action[1], "ActionName");
      const key = field(action[1], "Key");
      if (name) inputMappings.push({ kind: "action", name, key: key ?? "" });
      continue;
    }
    const axis = RE_AXIS.exec(line);
    if (axis) {
      const name = field(axis[1], "AxisName");
      const key = field(axis[1], "Key");
      const scale = field(axis[1], "Scale");
      if (name) {
        inputMappings.push({
          kind: "axis",
          name,
          key: key ?? "",
          scale: scale === undefined ? undefined : Number(scale),
        });
      }
      continue;
    }
    const tag = RE_TAG.exec(line);
    if (tag) {
      const t = field(tag[1], "Tag");
      const comment = field(tag[1], "DevComment");
      if (t) gameplayTags.push({ tag: t, comment: comment || undefined });
    }
  }
  return { inputMappings, gameplayTags };
}

// ---- Content inventory ----

function parseContentAsset(rel: string): ContentAssetFact {
  // rel is Content/<path>/<name>.<ext>
  const withoutRoot = rel.replace(/^Content\//, "");
  const withoutExt = withoutRoot.replace(/\.(uasset|umap)$/, "");
  return {
    packagePath: `/Game/${withoutExt}`,
    assetKind: rel.endsWith(".umap") ? "map" : "asset",
  };
}

// ---- Store ----

export function defaultStorePath(projectRoot: string): string {
  return join(resolve(projectRoot), "Saved", "BridgeIntelligence", "project-index.json");
}

export function loadIndex(storePath: string): ProjectIndex | null {
  if (!existsSync(storePath)) return null;
  try {
    const parsed = JSON.parse(readFileSync(storePath, "utf-8")) as ProjectIndex;
    if (typeof parsed.schemaVersion !== "number" || typeof parsed.files !== "object") {
      return null;
    }
    return parsed;
  } catch {
    // A corrupt cache is not an error, it is a full re-index.
    return null;
  }
}

// ---- Indexing ----

function indexOne(file: ScannedFile, moduleCache: Map<string, string>): Partial<FileEntry> {
  if (file.kind === "content_asset") {
    return { asset: parseContentAsset(file.rel) };
  }
  if (file.kind === "config_ini") {
    const facts = parseIni(readFileSync(file.abs, "utf-8"));
    return {
      inputMappings: facts.inputMappings.length ? facts.inputMappings : undefined,
      gameplayTags: facts.gameplayTags.length ? facts.gameplayTags : undefined,
    };
  }
  if (file.kind === "build_cs") {
    const name = basename(file.abs).slice(0, -".Build.cs".length);
    return { module: parseBuildCs(readFileSync(file.abs, "utf-8"), name) };
  }
  const module = moduleForFile(file.abs, moduleCache);
  const symbols = parseCpp(readFileSync(file.abs, "utf-8"), module);
  return { symbols: symbols.length ? symbols : undefined };
}

export function indexProject(projectRoot: string, options: IndexOptions = {}): IndexResult {
  const started = Date.now();
  const root = resolve(projectRoot);
  if (!existsSync(root)) {
    return { success: false, data: null, error: `Project root does not exist: ${root}` };
  }

  const storePath = options.storePath ?? defaultStorePath(root);
  const includePlugins = options.includePlugins !== false;
  const previous = loadIndex(storePath);
  const schemaChanged =
    previous !== null &&
    (previous.schemaVersion !== PROJECT_INDEX_SCHEMA_VERSION || previous.projectRoot !== root);
  const reusable = previous !== null && !schemaChanged ? previous.files : {};

  const files: Record<string, FileEntry> = {};
  const reasons: Record<InvalidationReason, number> = {
    new: 0,
    size_changed: 0,
    mtime_changed: 0,
    schema_version_changed: 0,
  };
  let indexed = 0;
  let skipped = 0;
  const moduleCache = new Map<string, string>();

  let scanned: ScannedFile[];
  try {
    scanned = collectFiles(root, includePlugins);
  } catch (err) {
    return { success: false, data: null, error: `Scan failed: ${String(err)}` };
  }

  for (const file of scanned) {
    let size: number;
    let mtimeMs: number;
    try {
      const st = statSync(file.abs);
      size = st.size;
      mtimeMs = st.mtimeMs;
    } catch {
      continue;
    }

    const prev = reusable[file.rel];
    let reason: InvalidationReason | null = null;
    if (schemaChanged && previous?.files[file.rel]) reason = "schema_version_changed";
    else if (!prev) reason = "new";
    else if (prev.size !== size) reason = "size_changed";
    else if (prev.mtimeMs !== mtimeMs) reason = "mtime_changed";

    if (reason === null && prev) {
      files[file.rel] = prev;
      skipped++;
      continue;
    }

    const effectiveReason: InvalidationReason = reason ?? "new";
    let facts: Partial<FileEntry>;
    try {
      facts = indexOne(file, moduleCache);
    } catch (err) {
      return {
        success: false,
        data: null,
        error: `Failed to index ${file.rel}: ${String(err)}`,
      };
    }
    files[file.rel] = {
      kind: file.kind,
      size,
      mtimeMs,
      indexedAt: new Date().toISOString(),
      invalidatedBy: effectiveReason,
      ...facts,
    };
    reasons[effectiveReason]++;
    indexed++;
  }

  const removed = previous
    ? Object.keys(previous.files).filter((p) => !(p in files))
    : [];

  const index: ProjectIndex = {
    schemaVersion: PROJECT_INDEX_SCHEMA_VERSION,
    projectRoot: root,
    generatedAt: new Date().toISOString(),
    files,
  };

  if (options.write !== false) {
    try {
      mkdirSync(dirname(storePath), { recursive: true });
      writeFileSync(storePath, JSON.stringify(index), "utf-8");
    } catch (err) {
      return { success: false, data: null, error: `Failed to write store: ${String(err)}` };
    }
  }

  return {
    success: true,
    data: {
      index,
      stats: {
        storePath,
        scanned: scanned.length,
        indexed,
        skipped,
        removed,
        reasons,
        durationMs: Date.now() - started,
      },
    },
  };
}

// ---- Lookup ----

export interface SymbolHit {
  file: string;
  symbol: SymbolFact;
}

/** Exact or substring symbol lookup across the whole index. */
export function findSymbols(
  index: ProjectIndex,
  name: string,
  exact = true
): SymbolHit[] {
  const hits: SymbolHit[] = [];
  const needle = name.toLowerCase();
  for (const [file, entry] of Object.entries(index.files)) {
    for (const symbol of entry.symbols ?? []) {
      const match = exact
        ? symbol.name === name
        : symbol.name.toLowerCase().includes(needle);
      if (match) hits.push({ file, symbol });
    }
  }
  return hits;
}

/** All modules in the index, keyed by module name. */
export function listModules(index: ProjectIndex): ModuleFact[] {
  const out: ModuleFact[] = [];
  for (const entry of Object.values(index.files)) {
    if (entry.module) out.push(entry.module);
  }
  return out;
}

/** Every input mapping across every indexed ini. */
export function listInputMappings(index: ProjectIndex): InputMappingFact[] {
  return Object.values(index.files).flatMap((e) => e.inputMappings ?? []);
}

/** Every gameplay tag across every indexed ini. */
export function listGameplayTags(index: ProjectIndex): GameplayTagFact[] {
  return Object.values(index.files).flatMap((e) => e.gameplayTags ?? []);
}

/** Content inventory by package path. */
export function listContentAssets(index: ProjectIndex): ContentAssetFact[] {
  return Object.values(index.files).flatMap((e) => (e.asset ? [e.asset] : []));
}
