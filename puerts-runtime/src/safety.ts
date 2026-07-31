type UnknownFunction = (...args: unknown[]) => unknown;
type ModuleLoader = (request: string, parent: unknown, isMain: boolean) => unknown;
interface ModuleConstructorShape {
  _load: ModuleLoader;
}

const blockedModules = new Set([
  "child_process",
  "node:child_process",
  "cluster",
  "node:cluster",
  "worker_threads",
  "node:worker_threads",
  "repl",
  "node:repl",
]);

const onePathFunctions = new Set([
  "access", "accessSync", "appendFile", "appendFileSync", "chmod", "chmodSync",
  "chown", "chownSync", "createReadStream", "createWriteStream", "existsSync",
  "lstat", "lstatSync", "mkdir", "mkdirSync", "open", "openSync", "opendir",
  "opendirSync", "readFile", "readFileSync", "readdir", "readdirSync", "readlink",
  "readlinkSync", "realpath", "realpathSync", "rm", "rmSync", "rmdir", "rmdirSync",
  "stat", "statSync", "truncate", "truncateSync", "unlink", "unlinkSync",
  "utimes", "utimesSync", "watch", "watchFile", "writeFile", "writeFileSync",
]);

const twoPathFunctions = new Set([
  "copyFile", "copyFileSync", "cp", "cpSync", "link", "linkSync", "rename",
  "renameSync", "symlink", "symlinkSync",
]);

function canonicalProjectPath(pathModule: Readonly<Record<string, unknown>>, projectRoot: string, value: unknown): string {
  if (typeof value !== "string") {
    throw new Error("File operations require a project-relative or project-absolute string path");
  }
  const resolve = pathModule.resolve as UnknownFunction;
  const normalizedRoot = String(resolve(projectRoot));
  const candidate = String(resolve(value));
  const comparisonRoot = normalizedRoot.toLowerCase();
  const comparisonCandidate = candidate.toLowerCase();
  const finalCode = normalizedRoot.charCodeAt(normalizedRoot.length - 1);
  const separator = normalizedRoot.endsWith("/") || finalCode === 92 ? "" : String.fromCharCode(92);
  if (comparisonCandidate !== comparisonRoot && !comparisonCandidate.startsWith((normalizedRoot + separator).toLowerCase())) {
    throw new Error("File-system access outside the project is blocked: " + candidate);
  }
  return candidate;
}

function guardedFileSystem(realFs: Readonly<Record<PropertyKey, unknown>>, pathModule: Readonly<Record<string, unknown>>, projectRoot: string): object {
  return new Proxy(realFs, {
    get(target: Readonly<Record<PropertyKey, unknown>>, property: PropertyKey): unknown {
      const value = Reflect.get(target, property);
      if (property === "promises" && value !== null && typeof value === "object") {
        return guardedFileSystem(value as Readonly<Record<PropertyKey, unknown>>, pathModule, projectRoot);
      }
      if (typeof property !== "string" || typeof value !== "function") {
        return value;
      }
      if (onePathFunctions.has(property)) {
        return (...args: unknown[]): unknown => {
          const guarded = [...args];
          guarded[0] = canonicalProjectPath(pathModule, projectRoot, guarded[0]);
          return (value as UnknownFunction).apply(target, guarded);
        };
      }
      if (twoPathFunctions.has(property)) {
        return (...args: unknown[]): unknown => {
          const guarded = [...args];
          guarded[0] = canonicalProjectPath(pathModule, projectRoot, guarded[0]);
          guarded[1] = canonicalProjectPath(pathModule, projectRoot, guarded[1]);
          return (value as UnknownFunction).apply(target, guarded);
        };
      }
      return (value as UnknownFunction).bind(target);
    },
  });
}

export function installRuntimeGuards(projectRoot: string, shellCommandsAllowed: boolean): void {
  if (shellCommandsAllowed) {
    throw new Error("The native bridge attempted to enable shell commands");
  }

  const moduleConstructor = require("module") as ModuleConstructorShape;
  const originalLoad = moduleConstructor._load;
  const pathModule = originalLoad("path", null, false) as Readonly<Record<string, unknown>>;
  const realFs = originalLoad("fs", null, false) as Readonly<Record<PropertyKey, unknown>>;
  const guardedFs = guardedFileSystem(realFs, pathModule, projectRoot);

  moduleConstructor._load = (request: string, parent: unknown, isMain: boolean): unknown => {
    if (blockedModules.has(request)) {
      throw new Error("Blocked Node.js module: " + request);
    }
    if (request === "fs" || request === "node:fs" || request === "fs/promises" || request === "node:fs/promises") {
      return request.includes("promises")
        ? Reflect.get(guardedFs, "promises")
        : guardedFs;
    }
    return originalLoad(request, parent, isMain);
  };

  const originalChdir = process.chdir.bind(process);
  process.chdir = (directory: string): void => {
    originalChdir(canonicalProjectPath(pathModule, projectRoot, directory));
  };
}
