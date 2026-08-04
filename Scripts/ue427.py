#!/usr/bin/env python3
"""ue427: one command to install, diagnose, repair, and launch the UE4.27
bridge across Claude Code, OpenAI Codex, and Google Gemini CLI.

Subcommands:
  install     Install the canonical skill and register the unreal-bridge MCP server
  uninstall   Remove the installed skill and MCP registration
  doctor      Diagnose repo build, skill install, MCP config, project version, session
  repair      Fix what doctor found that is safe to fix automatically
  update      git pull, rebuild the MCP server, reinstall the skill
  start       Verify, then launch an agent from the UE4.27 project directory
  verify      Prove each client actually discovers the installed skill
  catalog     Regenerate references/tool-catalog.md from annotations.ts

Every agent is pointed at the existing `unreal-bridge` MCP server in this
repository (node mcp-server/dist/index.js, authenticated named pipe). This
script never configures an HTTP or Remote Control transport: editor traffic
goes through puerts_* only, per AGENTS.md.

Stdlib only. Python 3.8+.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
from typing import Any, Dict, List, Mapping, Optional, Sequence, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL_NAME = "unreal-engine-4-27"
SKILL_SRC = os.path.join(REPO_ROOT, "skills", SKILL_NAME)
SERVER_NAME = "unreal-bridge"
SERVER_ENTRY = os.path.join(REPO_ROOT, "mcp-server", "dist", "index.js")
MCP_JSON = os.path.join(REPO_ROOT, ".mcp.json")

# CLI clients are detected by their executable; Antigravity is an application,
# so it is detected by its configuration directory.
#
# Claude Desktop is deliberately absent. Its Code tab runs Claude Code and
# shares CLAUDE.md, project skills, hooks and MCP configuration with the CLI,
# so installing for "claude" already covers it. Its Chat tab uses a separate
# claude_desktop_config.json, which is not a coding surface; writing the bridge
# there would add a second, redundant registration.
CLI_AGENTS = ("claude", "codex", "gemini")
APP_AGENTS = ("antigravity",)
AGENTS = CLI_AGENTS + APP_AGENTS

sys.path.insert(0, os.path.join(SKILL_SRC, "scripts"))
import verify_project_version as vpv  # noqa: E402


# ---------------------------------------------------------------- helpers

def timestamp() -> str:
    """Return a filesystem-safe timestamp for backup names."""
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def cli_path(name: str) -> Optional[str]:
    """Resolve an agent CLI to something CreateProcess can launch.

    On Windows npm installs `name`, `name.cmd` and `name.ps1`; only .cmd/.exe
    are directly launchable by subprocess. Returns None when not installed.
    """
    if os.name == "nt":
        return shutil.which(name + ".cmd") or shutil.which(name + ".exe")
    return shutil.which(name)


def home() -> str:
    """Return the user home directory."""
    return os.path.expanduser("~")


def antigravity_root() -> str:
    """Antigravity's global customization root.

    Its own docs name `~/.gemini/config/` as the global root and `.agents/` as
    the per-project one. `~/.gemini/antigravity/skills` is a link to
    `~/.gemini/config/skills`, so writing to the documented root covers both.
    """
    return os.path.join(home(), ".gemini", "config")




def antigravity_ide_launcher() -> Optional[str]:
    """Locate the Antigravity IDE launcher.

    The IDE ships a VS Code style `antigravity-ide` shim in its bin directory,
    which opens a folder as a workspace. It is a separate application from
    `Antigravity.exe`, the agent manager, and it is the one that reads a
    project's AGENTS.md and .agents directory.
    """
    on_path = cli_path("antigravity-ide") or shutil.which("antigravity-ide")
    if on_path:
        return on_path
    local = os.environ.get("LOCALAPPDATA", os.path.join(home(), "AppData", "Local"))
    candidates = [
        os.path.join(local, "Programs", "Antigravity IDE", "bin", "antigravity-ide.cmd"),
        os.path.join(local, "Programs", "Antigravity IDE", "bin", "antigravity-ide"),
        os.path.join(local, "Programs", "Antigravity IDE", "Antigravity IDE.exe"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return None


def app_installed(agent: str) -> bool:
    """Whether an application client (not a CLI) is present on this machine."""
    if agent == "antigravity":
        return os.path.isdir(antigravity_root()) or os.path.isdir(
            os.path.join(home(), ".gemini", "antigravity"))
    return False


def detect_agents() -> List[str]:
    """Return every agent client actually installed on this machine."""
    found = [agent for agent in CLI_AGENTS if cli_path(agent)]
    found += [agent for agent in APP_AGENTS if app_installed(agent)]
    return found


def skill_roots(agent: str, scope: str, project_dir: Optional[str]) -> List[str]:
    """Return the skill parent directories for an agent and scope.

    Claude Code uses .claude/skills, which Claude Desktop's Code tab shares.
    Codex, Gemini and Antigravity all read the shared .agents/skills location
    for a project. At user scope Antigravity differs: its global customization
    root is ~/.gemini/config, not ~/.agents.
    """
    base = home() if scope == "user" else (project_dir or REPO_ROOT)
    if agent == "claude":
        return [os.path.join(base, ".claude", "skills")]
    if agent == "antigravity" and scope == "user":
        return [os.path.join(antigravity_root(), "skills")]
    return [os.path.join(base, ".agents", "skills")]


def read_configured_project() -> Optional[str]:
    """Read MCP_UNREAL_PROJECT_ROOT from the repo .mcp.json, if present."""
    if not os.path.isfile(MCP_JSON):
        return None
    try:
        with open(MCP_JSON, "r", encoding="utf-8-sig") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    server = data.get("mcpServers", {}).get(SERVER_NAME, {})
    return server.get("env", {}).get("MCP_UNREAL_PROJECT_ROOT")


def read_engine_root() -> Optional[str]:
    """Read UE_ENGINE_ROOT from the environment or the repo .mcp.json."""
    from_env = os.environ.get("UE_ENGINE_ROOT")
    if from_env:
        return from_env
    if os.path.isfile(MCP_JSON):
        try:
            with open(MCP_JSON, "r", encoding="utf-8-sig") as handle:
                data = json.load(handle)
            return data["mcpServers"][SERVER_NAME]["env"].get("UE_ENGINE_ROOT")
        except (OSError, ValueError, KeyError):
            return None
    return None


def session_manifest(project_root: str) -> str:
    """Return the path of the editor session manifest for a project."""
    return os.path.join(project_root, "Saved", "MCPPuerTSBridge", "session.json")


def read_session(project_root: str) -> Optional[Dict[str, Any]]:
    """Return the advertised editor session for a project, or None."""
    path = session_manifest(project_root)
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8-sig") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


# ------------------------------------------------------------- installing

class Installer:
    """Installs the canonical skill and MCP registration for each agent."""

    def __init__(self, dry_run: bool = False, use_links: bool = True) -> None:
        self.dry_run = dry_run
        self.use_links = use_links
        self.changed: List[str] = []
        # Codex and Gemini share .agents/skills. Installing once per run keeps
        # the reported paths honest and avoids a redundant second pass.
        self._done_roots: set = set()

    def log(self, action: str, path: str) -> None:
        """Record and print one changed path."""
        self.changed.append("%s %s" % (action, path))
        print("  %s: %s" % (action, path))

    def note(self, message: str) -> None:
        """Print an informational line that is not a change."""
        print("  %s" % message)

    def _backup(self, path: str, parent: str) -> None:
        """Move an existing destination aside, outside the skills root.

        A backup left inside the skills directory would be discovered by the
        client as a second, stale skill.
        """
        backup_dir = parent.rstrip("\\/") + "-backups"
        dest = os.path.join(backup_dir, "%s.bak-%s" % (SKILL_NAME, timestamp()))
        if not self.dry_run:
            os.makedirs(backup_dir, exist_ok=True)
            shutil.move(path, dest)
        self.log("backup", dest)

    def _link(self, src: str, dst: str) -> bool:
        """Create a directory link at dst pointing to src. True on success."""
        try:
            os.symlink(src, dst, target_is_directory=True)
            return True
        except (OSError, NotImplementedError, AttributeError):
            pass
        if os.name == "nt":
            # Junctions need no elevation, unlike symlinks without Developer Mode.
            result = subprocess.run(
                ["cmd", "/c", "mklink", "/J", dst, src],
                capture_output=True, text=True, encoding="utf-8", errors="replace",
            )
            return result.returncode == 0
        return False

    def install_skill(self, root: str) -> str:
        """Link (or copy) the canonical skill into one skills directory."""
        dst = os.path.join(root, SKILL_NAME)
        key = os.path.normcase(os.path.abspath(root))
        if key in self._done_roots:
            self.note("shared location already installed this run: %s" % dst)
            return dst
        self._done_roots.add(key)
        if os.path.islink(dst) or os.path.isdir(dst):
            same = False
            try:
                same = os.path.realpath(dst) == os.path.realpath(SKILL_SRC)
            except OSError:
                same = False
            if same:
                self.note("already current: %s" % dst)
                return dst
            self._backup(dst, root)
        if not self.dry_run:
            os.makedirs(root, exist_ok=True)
        if self.use_links:
            if self.dry_run:
                self.log("link-skill", dst)
                return dst
            if self._link(SKILL_SRC, dst):
                self.log("link-skill", dst)
                return dst
            self.note("link unavailable, falling back to copy")
        if not self.dry_run:
            shutil.copytree(SKILL_SRC, dst)
        self.log("copy-skill", dst)
        return dst

    def server_env(self, project: Optional[str]) -> Dict[str, str]:
        """Return the environment the unreal-bridge server needs."""
        env: Dict[str, str] = {}
        engine_root = read_engine_root()
        if engine_root:
            env["UE_ENGINE_ROOT"] = engine_root
        target = project or read_configured_project()
        if target:
            env["MCP_UNREAL_PROJECT_ROOT"] = target.replace("\\", "/")
        return env

    def register_claude(self, scope: str, project: Optional[str],
                        project_dir: Optional[str]) -> None:
        """Register unreal-bridge with Claude Code via its own CLI."""
        claude = cli_path("claude")
        if claude is None:
            self.note("claude CLI not found, skipped MCP registration")
            return
        cmd = [claude, "mcp", "add", "--scope", scope, SERVER_NAME]
        for key, value in self.server_env(project).items():
            cmd += ["-e", "%s=%s" % (key, value)]
        cmd += ["--", "node", SERVER_ENTRY]
        if self.dry_run:
            self.note("would run: %s" % " ".join(cmd))
            return
        cwd = project_dir if scope == "project" else None
        subprocess.run([claude, "mcp", "remove", "--scope", scope, SERVER_NAME],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd)
        result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd)
        if result.returncode == 0:
            self.log("mcp-register(claude,%s)" % scope, SERVER_NAME)
        else:
            self.note("claude mcp add failed: %s"
                      % (result.stderr or result.stdout).strip())

    def register_codex(self, scope: str, project: Optional[str]) -> None:
        """Register unreal-bridge with Codex via its own CLI, else config.toml."""
        codex = cli_path("codex")
        env = self.server_env(project)
        if scope == "user" and codex:
            cmd = [codex, "mcp", "add", SERVER_NAME]
            for key, value in env.items():
                cmd += ["--env", "%s=%s" % (key, value)]
            cmd += ["--", "node", SERVER_ENTRY]
            if self.dry_run:
                self.note("would run: %s" % " ".join(cmd))
                return
            subprocess.run([codex, "mcp", "remove", SERVER_NAME],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
            result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
            if result.returncode == 0:
                self.log("mcp-register(codex,user)", SERVER_NAME)
                return
            self.note("codex mcp add failed (%s), merging config.toml"
                      % (result.stderr or result.stdout).strip())
        config = os.path.join(home() if scope == "user" else REPO_ROOT,
                              ".codex", "config.toml")
        section = codex_toml_section(SERVER_ENTRY, REPO_ROOT, env)
        text = ""
        if os.path.isfile(config):
            with open(config, "r", encoding="utf-8") as handle:
                text = handle.read()
        merged = merge_toml_section(text, "mcp_servers.%s" % SERVER_NAME, section)
        if merged == text:
            self.note("codex config already current: %s" % config)
            return
        if os.path.isfile(config) and not self.dry_run:
            shutil.copy2(config, "%s.bak-%s" % (config, timestamp()))
        if not self.dry_run:
            os.makedirs(os.path.dirname(config), exist_ok=True)
            with open(config, "w", encoding="utf-8") as handle:
                handle.write(merged)
        self.log("mcp-register(codex,%s)" % scope, config)

    def register_json_mcp(self, label: str, config: str, entry: Dict[str, Any]) -> None:
        """Merge one server into a JSON config's mcpServers, preserving the rest.

        Used by every client whose MCP configuration is a JSON file with an
        mcpServers object: Gemini CLI, Antigravity, and Claude Desktop. Only the
        unreal-bridge key is touched; other servers and settings are untouched.
        """
        settings: Dict[str, Any] = {}
        if os.path.isfile(config):
            try:
                with open(config, "r", encoding="utf-8-sig") as handle:
                    settings = json.load(handle)
            except ValueError:
                self.note("%s is not valid JSON, leaving it alone" % config)
                return
        servers = settings.setdefault("mcpServers", {})
        if servers.get(SERVER_NAME) == entry:
            self.note("%s already current: %s" % (label, config))
            return
        servers[SERVER_NAME] = entry
        if os.path.isfile(config) and not self.dry_run:
            shutil.copy2(config, "%s.bak-%s" % (config, timestamp()))
        if not self.dry_run:
            os.makedirs(os.path.dirname(config), exist_ok=True)
            with open(config, "w", encoding="utf-8") as handle:
                json.dump(settings, handle, indent=2)
                handle.write("\n")
        self.log("mcp-register(%s)" % label, config)

    def stdio_entry(self, project: Optional[str], extras: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Build a plain stdio MCP entry for the unreal-bridge server."""
        entry: Dict[str, Any] = {
            "command": "node",
            "args": [SERVER_ENTRY.replace("\\", "/")],
            "cwd": REPO_ROOT.replace("\\", "/"),
        }
        env = self.server_env(project)
        if env:
            entry["env"] = env
        if extras:
            entry.update(extras)
        return entry

    def register_gemini(self, scope: str, project: Optional[str],
                        project_dir: Optional[str]) -> None:
        """Merge unreal-bridge into the Gemini CLI settings.json."""
        base = home() if scope == "user" else (project_dir or REPO_ROOT)
        config = os.path.join(base, ".gemini", "settings.json")
        entry = self.stdio_entry(project, {"timeout": 120000, "trust": False})
        self.register_json_mcp("gemini,%s" % scope, config, entry)

    def register_antigravity(self, scope: str, project: Optional[str],
                             project_dir: Optional[str]) -> None:
        """Merge unreal-bridge into Antigravity's mcp_config.json.

        Antigravity accepts a plain {command, args} entry; the $typeName field
        on some of its own entries is serialization metadata, not required.
        """
        if scope == "user":
            config = os.path.join(antigravity_root(), "mcp_config.json")
        else:
            config = os.path.join(project_dir or REPO_ROOT, ".agents", "mcp_config.json")
        self.register_json_mcp("antigravity,%s" % scope, config,
                               self.stdio_entry(project))


    def install(self, agent: str, scope: str, project: Optional[str],
                project_dir: Optional[str]) -> None:
        """Install skill and MCP registration for one agent and scope."""
        print("--- %s (%s scope) ---" % (agent, scope))
        for root in skill_roots(agent, scope, project_dir):
            self.install_skill(root)
        if agent == "claude":
            self.register_claude(scope, project, project_dir)
        elif agent == "codex":
            self.register_codex(scope, project)
        elif agent == "gemini":
            self.register_gemini(scope, project, project_dir)
        elif agent == "antigravity":
            self.register_antigravity(scope, project, project_dir)


def codex_toml_section(entry: str, cwd: str, env: Mapping[str, str]) -> str:
    """Build the [mcp_servers.unreal-bridge] block for a Codex config.toml."""
    lines = [
        "[mcp_servers.%s]" % SERVER_NAME,
        "command = 'node'",
        "args = ['%s']" % entry.replace("\\", "/"),
        "cwd = '%s'" % cwd.replace("\\", "/"),
    ]
    if env:
        lines.append("")
        lines.append("  [mcp_servers.%s.env]" % SERVER_NAME)
        for key, value in env.items():
            lines.append("  %s = '%s'" % (key, value))
    return "\n".join(lines) + "\n"


def merge_toml_section(text: str, section: str, block: str) -> str:
    """Replace one [section] in a TOML document, or append it.

    Only the named section is touched; every other server and setting in the
    file is preserved byte for byte.
    """
    pattern = re.compile(
        r"^\[%s\]\s*\n(?:(?!^\[).*\n?)*" % re.escape(section), re.MULTILINE)
    if pattern.search(text):
        return pattern.sub(block, text)
    if text and not text.endswith("\n"):
        text += "\n"
    if text:
        text += "\n"
    return text + block


# ---------------------------------------------------------------- doctor

class Finding:
    """One doctor result: ok, warn, or error, with an optional fix hint."""

    def __init__(self, level: str, check: str, detail: str,
                 fix: Optional[str] = None) -> None:
        self.level = level
        self.check = check
        self.detail = detail
        self.fix = fix

    def render(self) -> str:
        """Return the printable line for this finding."""
        mark = {"ok": "OK  ", "warn": "WARN", "error": "FAIL"}[self.level]
        line = "%s %s: %s" % (mark, self.check, self.detail)
        if self.fix and self.level != "ok":
            line += "\n       fix: %s" % self.fix
        return line


def check_build() -> List[Finding]:
    """Check that the MCP server is installed and built."""
    findings = []
    if not os.path.isdir(os.path.join(REPO_ROOT, "node_modules")):
        findings.append(Finding("error", "deps", "node_modules missing",
                                "npm install"))
    if not os.path.isfile(SERVER_ENTRY):
        findings.append(Finding(
            "error", "build", "mcp-server/dist/index.js missing, so no puerts_* tools load",
            "npm run build, then restart the agent"))
    else:
        src_dir = os.path.join(REPO_ROOT, "mcp-server", "src")
        newest_src = newest_mtime(src_dir, (".ts",))
        dist_mtime = os.path.getmtime(SERVER_ENTRY)
        if newest_src and newest_src > dist_mtime:
            findings.append(Finding(
                "warn", "build", "dist is older than src, clients run stale tools",
                "npm run build, then restart the agent"))
        else:
            findings.append(Finding("ok", "build", "mcp-server/dist is present and current"))
    return findings


def newest_mtime(directory: str, suffixes: Tuple[str, ...]) -> float:
    """Return the newest mtime under a directory for the given suffixes."""
    newest = 0.0
    for dirpath, dirnames, filenames in os.walk(directory):
        dirnames[:] = [d for d in dirnames if d not in ("node_modules", "__pycache__")]
        for filename in filenames:
            if filename.endswith(suffixes):
                newest = max(newest, os.path.getmtime(os.path.join(dirpath, filename)))
    return newest


def check_skill_installed() -> List[Finding]:
    """Check the canonical skill is installed for each detected agent."""
    findings = []
    if not os.path.isfile(os.path.join(SKILL_SRC, "SKILL.md")):
        return [Finding("error", "skill-source", "skills/%s/SKILL.md missing" % SKILL_NAME,
                        "restore the canonical skill in this repository")]
    for agent in detect_agents():
        for root in skill_roots(agent, "user", None):
            dst = os.path.join(root, SKILL_NAME)
            if not os.path.exists(dst):
                findings.append(Finding("error", "skill(%s)" % agent, "not installed at %s" % dst,
                                        "python Scripts/ue427.py install"))
                continue
            linked = os.path.realpath(dst) == os.path.realpath(SKILL_SRC)
            if linked:
                findings.append(Finding("ok", "skill(%s)" % agent, "linked to canonical source"))
            else:
                findings.append(Finding(
                    "warn", "skill(%s)" % agent, "installed as a copy at %s, may drift" % dst,
                    "python Scripts/ue427.py repair"))
    return findings


def check_mcp_registered() -> List[Finding]:
    """Check unreal-bridge is registered with each detected agent."""
    findings = []
    for agent in detect_agents():
        if agent == "claude":
            claude = cli_path("claude")
            result = subprocess.run([claude, "mcp", "get", SERVER_NAME],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace")
            if result.returncode == 0:
                findings.append(Finding("ok", "mcp(claude)", "%s registered" % SERVER_NAME))
            else:
                findings.append(Finding("error", "mcp(claude)", "%s not registered" % SERVER_NAME,
                                        "python Scripts/ue427.py install --agent claude"))
        elif agent == "codex":
            config = os.path.join(home(), ".codex", "config.toml")
            present = os.path.isfile(config) and \
                ("[mcp_servers.%s]" % SERVER_NAME) in open(config, encoding="utf-8").read()
            findings.append(Finding("ok" if present else "error", "mcp(codex)",
                                    "%s %s in %s" % (SERVER_NAME,
                                                     "registered" if present else "missing",
                                                     config),
                                    None if present else "python Scripts/ue427.py install --agent codex"))
        else:
            config = {
                "gemini": os.path.join(home(), ".gemini", "settings.json"),
                "antigravity": os.path.join(antigravity_root(), "mcp_config.json"),
            }[agent]
            present = False
            if os.path.isfile(config):
                try:
                    with open(config, encoding="utf-8-sig") as handle:
                        present = SERVER_NAME in json.load(handle).get("mcpServers", {})
                except ValueError:
                    present = False
            findings.append(Finding("ok" if present else "error", "mcp(%s)" % agent,
                                    "%s %s" % (SERVER_NAME,
                                               "registered" if present else "missing"),
                                    None if present else
                                    "python Scripts/ue427.py install --agent %s" % agent))
    return findings


def check_project(project: Optional[str]) -> List[Finding]:
    """Check the target project is Unreal Engine 4.27 and advertises a session."""
    target = project or read_configured_project()
    if not target:
        return [Finding("warn", "project", "no MCP_UNREAL_PROJECT_ROOT configured",
                        "python Scripts/ue427.py install --project <path to .uproject>")]
    result = vpv.verify_project(target)
    if not result["ok"]:
        return [Finding("error", "project-version", result["error"],
                        "point the bridge at a 4.27 project; other versions are refused")]
    findings = [Finding("ok", "project-version",
                        "Unreal Engine %s (%s)" % (result["version"], result["source"]))]

    session = read_session(target)
    if session is None:
        findings.append(Finding(
            "error", "editor-session",
            "no editor advertises a session for %s" % target,
            "open that project in UE4Editor (Scripts/start-ue4-project.ps1), or point "
            "MCP_UNREAL_PROJECT_ROOT at the project that is actually running"))
    elif session.get("shutdown_state") != "running":
        findings.append(Finding("warn", "editor-session",
                                "session state is %r" % session.get("shutdown_state"),
                                "restart the editor"))
    else:
        findings.append(Finding("ok", "editor-session",
                                "editor pid %s advertises %s"
                                % (session.get("editor_pid"), session.get("pipe_name"))))
    return findings


def run_bridge_doctor() -> List[Finding]:
    """Delegate repository health to the existing Scripts/bridge-doctor.mjs."""
    script = os.path.join(REPO_ROOT, "Scripts", "bridge-doctor.mjs")
    if not os.path.isfile(script):
        return []
    node = shutil.which("node")
    if node is None:
        return [Finding("warn", "bridge-doctor", "node not found, skipped")]
    result = subprocess.run([node, script, "--quiet"], capture_output=True,
                            text=True, encoding="utf-8", errors="replace",
                            cwd=REPO_ROOT)
    level = "ok" if result.returncode == 0 else "error"
    detail = (result.stdout or result.stderr).strip() or "no problems reported"
    return [Finding(level, "bridge-doctor", detail.replace("\n", "\n       "),
                    "see Scripts/bridge-doctor.mjs output above")]


def doctor(project: Optional[str]) -> int:
    """Run every check and print a report. Exit 1 if any check failed."""
    print("ue427 doctor")
    print("repo: %s" % REPO_ROOT)
    agents = detect_agents()
    print("agents detected: %s" % (", ".join(agents) if agents else "none"))
    print("")
    findings: List[Finding] = []
    findings += check_build()
    findings += check_skill_installed()
    findings += check_mcp_registered()
    findings += check_project(project)
    findings += run_bridge_doctor()
    for finding in findings:
        print(finding.render())
    errors = sum(1 for f in findings if f.level == "error")
    warns = sum(1 for f in findings if f.level == "warn")
    print("")
    print("%d ok, %d warning(s), %d error(s)"
          % (sum(1 for f in findings if f.level == "ok"), warns, errors))
    if errors:
        print("Run: python Scripts/ue427.py repair")
    return 1 if errors else 0


# ---------------------------------------------------------------- actions

def npm(*args: str) -> int:
    """Run an npm command in the repository root."""
    npm_bin = shutil.which("npm.cmd") or shutil.which("npm")
    if npm_bin is None:
        print("npm not found on PATH", file=sys.stderr)
        return 1
    return subprocess.run([npm_bin, *args], cwd=REPO_ROOT).returncode


def repair(project: Optional[str], dry_run: bool) -> int:
    """Fix what is safe to fix: build the server, reinstall skill links."""
    print("ue427 repair")
    if not os.path.isdir(os.path.join(REPO_ROOT, "node_modules")):
        print("installing dependencies")
        if npm("install") != 0:
            return 1
    src_dir = os.path.join(REPO_ROOT, "mcp-server", "src")
    if not os.path.isfile(SERVER_ENTRY) or newest_mtime(src_dir, (".ts",)) > os.path.getmtime(SERVER_ENTRY):
        print("building the MCP server")
        if npm("run", "build") != 0:
            return 1
    install(list(detect_agents()), ["user"], project, None, dry_run, use_links=True)
    print("")
    print("Repair finished. Restart your agent so it reconnects the MCP server.")
    return doctor(project)


def update(project: Optional[str]) -> int:
    """Pull the latest main, rebuild, and reinstall the skill."""
    print("ue427 update")
    result = subprocess.run(["git", "pull", "--ff-only"], cwd=REPO_ROOT)
    if result.returncode != 0:
        print("git pull --ff-only failed. Resolve manually; nothing else was changed.",
              file=sys.stderr)
        return 1
    if npm("install") != 0 or npm("run", "build") != 0:
        return 1
    install(list(detect_agents()), ["user"], project, None, False, use_links=True)
    print("")
    print("Update finished. Restart your agent so it reconnects the MCP server.")
    return 0


def start(agent: str, project: Optional[str]) -> int:
    """Verify the project and bridge, then launch an agent in the project directory."""
    target = project or read_configured_project()
    if not target:
        print("No project configured. Pass --project <path to .uproject>.", file=sys.stderr)
        return 1
    result = vpv.verify_project(target)
    if not result["ok"]:
        print("FAIL: %s" % result["error"], file=sys.stderr)
        return 2
    print("Project verified: Unreal Engine %s (%s)" % (result["version"], result["source"]))

    if read_session(target) is None:
        print("WARNING: no editor advertises a session for this project yet.")
        print("         Open it with Scripts/start-ue4-project.ps1 before running editor tools.")

    project_dir = target if os.path.isdir(target) else os.path.dirname(target)

    if agent == "antigravity":
        # Open the IDE, not Antigravity.exe, which is the agent manager. The IDE
        # is what reads a workspace's AGENTS.md, GEMINI.md and .agents
        # directory, and Gemini CLI run from its terminal detects it through
        # ANTIGRAVITY_CLI_ALIAS.
        #
        # The workspace is this repository, because that is where the rules and
        # the canonical skill live. The UE4.27 project is reached over MCP by
        # configuration, not by working directory, so it does not need to be
        # the folder that is open.
        launcher = antigravity_ide_launcher()
        if launcher is None:
            print("Antigravity IDE not found. Install it, or open the folder from the app.",
                  file=sys.stderr)
            return 1
        print("Opening %s in Antigravity IDE" % REPO_ROOT)
        print("Bridge target stays %s" % target)
        return subprocess.run([launcher, REPO_ROOT], cwd=REPO_ROOT).returncode

    binary = cli_path(agent)
    if binary is None:
        print("%s CLI not found on PATH" % agent, file=sys.stderr)
        return 1
    print("Launching %s in %s" % (agent, project_dir))
    return subprocess.run([binary], cwd=project_dir).returncode


def install(agents: Sequence[str], scopes: Sequence[str], project: Optional[str],
            project_dir: Optional[str], dry_run: bool, use_links: bool) -> int:
    """Install the skill and MCP registration for the given agents and scopes."""
    if project:
        result = vpv.verify_project(project)
        if not result["ok"]:
            print("Project verification FAILED: %s" % result["error"], file=sys.stderr)
            return 2
        print("Project verified: Unreal Engine %s (%s)" % (result["version"], result["source"]))
    installer = Installer(dry_run=dry_run, use_links=use_links)
    if dry_run:
        print("DRY RUN: nothing will be written.")
    for scope in scopes:
        for agent in agents:
            installer.install(agent, scope, project, project_dir)
    print("")
    print("Changed paths:")
    for line in installer.changed or ["  (none)"]:
        print("  " + line if not line.startswith("  ") else line)
    print("")
    print("Restart your agent so it picks up the MCP server.")
    return 0


def uninstall(agents: Sequence[str], scopes: Sequence[str],
              project_dir: Optional[str], dry_run: bool) -> int:
    """Remove the installed skill and MCP registration."""
    installer = Installer(dry_run=dry_run)
    for scope in scopes:
        for agent in agents:
            print("--- %s (%s scope) ---" % (agent, scope))
            for root in skill_roots(agent, scope, project_dir):
                dst = os.path.join(root, SKILL_NAME)
                if os.path.islink(dst) or os.path.isdir(dst):
                    if not dry_run:
                        if os.path.islink(dst) or (os.name == "nt" and os.path.isdir(dst)
                                                   and os.path.realpath(dst) != os.path.abspath(dst)):
                            os.rmdir(dst)  # link or junction: removes the link only
                        else:
                            shutil.rmtree(dst)
                    installer.log("remove-skill", dst)
                else:
                    installer.note("not installed: %s" % dst)
            if agent == "claude" and cli_path("claude"):
                if not dry_run:
                    subprocess.run([cli_path("claude"), "mcp", "remove", "--scope", scope,
                                    SERVER_NAME], capture_output=True, text=True, encoding="utf-8", errors="replace")
                installer.log("mcp-remove(claude,%s)" % scope, SERVER_NAME)
            elif agent == "codex" and cli_path("codex") and scope == "user":
                if not dry_run:
                    subprocess.run([cli_path("codex"), "mcp", "remove", SERVER_NAME],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace")
                installer.log("mcp-remove(codex,user)", SERVER_NAME)
            else:
                if agent == "gemini":
                    config = os.path.join(home() if scope == "user" else (project_dir or REPO_ROOT),
                                          ".gemini", "settings.json")
                else:
                    config = (os.path.join(antigravity_root(), "mcp_config.json") if scope == "user"
                              else os.path.join(project_dir or REPO_ROOT, ".agents", "mcp_config.json"))
                if os.path.isfile(config):
                    with open(config, encoding="utf-8-sig") as handle:
                        settings = json.load(handle)
                    if SERVER_NAME in settings.get("mcpServers", {}):
                        del settings["mcpServers"][SERVER_NAME]
                        if not dry_run:
                            shutil.copy2(config, "%s.bak-%s" % (config, timestamp()))
                            with open(config, "w", encoding="utf-8") as handle:
                                json.dump(settings, handle, indent=2)
                                handle.write("\n")
                        installer.log("mcp-remove(%s,%s)" % (agent, scope), config)
    print("")
    print("Changed paths:")
    for line in installer.changed or ["  (none)"]:
        print("  " + line if not line.startswith("  ") else line)
    return 0


# ----------------------------------------------------------- verification

def verify_codex_discovery(expect_path: str, cwd: str) -> Tuple[bool, str]:
    """Ask the real codex app-server whether it discovers the skill.

    Drives the same JSON-RPC protocol behind the interactive /skills command:
    initialize, then skills/list with forceReload to bypass the skill cache.
    """
    codex = cli_path("codex")
    if codex is None:
        return False, "codex CLI not found"
    proc = subprocess.Popen([codex, "app-server"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding="utf-8", errors="replace", bufsize=1)
    try:
        def request(msg_id: int, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
            proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": msg_id,
                                         "method": method, "params": params}) + "\n")
            proc.stdin.flush()
            while True:
                line = proc.stdout.readline()
                if not line:
                    raise RuntimeError("app-server closed stdout")
                line = line.strip()
                if not line:
                    continue
                try:
                    message = json.loads(line)
                except ValueError:
                    continue
                if message.get("id") == msg_id:
                    return message

        request(1, "initialize", {"clientInfo": {"name": "ue427-verify", "version": "1.0.0"}})
        reply = request(2, "skills/list", {"cwds": [cwd], "forceReload": True})
        if "error" in reply:
            return False, "skills/list errored: %s" % reply["error"]
        for entry in reply.get("result", {}).get("data", []):
            for skill in entry.get("skills", []):
                if skill.get("name") == SKILL_NAME:
                    actual = os.path.normcase(os.path.realpath(skill["path"]))
                    expected = os.path.normcase(os.path.realpath(expect_path))
                    if actual != expected:
                        return False, "found at %s, expected %s" % (skill["path"], expect_path)
                    if not skill.get("enabled", True):
                        return False, "found but disabled"
                    return True, "%s (%s scope)" % (skill["path"], skill.get("scope"))
        return False, "not present in skills/list"
    except (OSError, RuntimeError) as exc:
        return False, str(exc)
    finally:
        try:
            proc.stdin.close()
            proc.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()


def verify_gemini_discovery() -> Tuple[bool, str]:
    """Ask the Gemini CLI whether it discovers the skill."""
    gemini = cli_path("gemini")
    if gemini is None:
        return False, "gemini CLI not found"
    result = subprocess.run([gemini, "skills", "list"], capture_output=True,
                            text=True, encoding="utf-8", errors="replace",
                            cwd=REPO_ROOT)
    output = result.stdout or ""
    if SKILL_NAME not in output:
        return False, "not present in `gemini skills list`"
    for block in output.split("\n\n"):
        if block.strip().startswith(SKILL_NAME):
            enabled = "[Enabled]" in block
            location = ""
            for line in block.splitlines():
                if "Location:" in line:
                    location = line.split("Location:", 1)[1].strip()
            if not enabled:
                return False, "listed but disabled"
            return True, location or "listed and enabled"
    return True, "listed"


def verify(project: Optional[str]) -> int:
    """Report what is proven about each client, separating two kinds of claim.

    DIRECTLY VERIFIED means the client itself was asked and answered: Codex
    over its app-server protocol, Gemini through its CLI, Claude Code through
    `claude mcp get`. CONFIGURATION PRESENT means files are in the right place
    with the right contents, which is necessary but is not proof the
    application loaded them. Antigravity exposes no query interface, so its
    configuration cannot be confirmed at runtime from here; open the app and
    check its MCP panel.
    """
    print("ue427 verify")
    failures = 0
    direct: List[str] = []
    config_only: List[str] = []

    if cli_path("claude"):
        result = subprocess.run([cli_path("claude"), "mcp", "get", SERVER_NAME],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        ok = result.returncode == 0
        connected = "Connected" in (result.stdout or "")
        direct.append("%s claude mcp: %s%s" % ("OK  " if ok else "FAIL", SERVER_NAME,
                                               " (connected)" if connected else " (registered, not connected)"))
        if not ok:
            failures += 1

    if cli_path("codex"):
        expect = os.path.join(home(), ".agents", "skills", SKILL_NAME, "SKILL.md")
        ok, detail = verify_codex_discovery(expect, REPO_ROOT)
        direct.append("%s codex discovery: %s" % ("OK  " if ok else "FAIL", detail))
        if not ok:
            failures += 1

    if cli_path("gemini"):
        ok, detail = verify_gemini_discovery()
        direct.append("%s gemini discovery: %s" % ("OK  " if ok else "FAIL", detail))
        if not ok:
            failures += 1

    for agent in AGENTS:
        if agent in CLI_AGENTS and cli_path(agent) is None:
            continue
        if agent in APP_AGENTS and not app_installed(agent):
            continue
        root = skill_roots(agent, "user", None)[0]
        installed = os.path.join(root, SKILL_NAME, "SKILL.md")
        present = os.path.isfile(installed)
        config_only.append("%s skill file(%s): %s"
                           % ("OK  " if present else "FAIL", agent, installed))
        if not present:
            failures += 1

    print("")
    print("DIRECTLY VERIFIED (the client was asked and answered)")
    for line in direct or ["  (none: no queryable client installed)"]:
        print("  " + line)

    print("")
    print("CONFIGURATION PRESENT (on disk and correct, runtime load not proven)")
    for line in config_only or ["  (none)"]:
        print("  " + line)
    if app_installed("antigravity"):
        print("  NOTE antigravity: exposes no query interface. Open the app and")
        print("       check its MCP panel to confirm it loaded unreal-bridge.")
    print("  NOTE claude-desktop: its Code tab shares Claude Code's CLAUDE.md,")
    print("       skills and MCP config, so the claude rows above cover it.")
    print("       Confirm in the app: the Code tab, not the Chat tab.")

    print("")
    print("%d failure(s)" % failures)
    return 1 if failures else 0


# ---------------------------------------------------------------- catalog

def generate_catalog() -> str:
    """Build references/tool-catalog.md from mcp-server/src/annotations.ts."""
    source = os.path.join(REPO_ROOT, "mcp-server", "src", "annotations.ts")
    with open(source, "r", encoding="utf-8") as handle:
        text = handle.read()
    named = re.findall(
        r"^  (puerts_[a-z_]+|bridge_[a-z_]+|engine_source_[a-z_]+):\s*([A-Za-z]+)\s*,",
        text, re.MULTILINE)
    inline = re.findall(
        r"^  (puerts_[a-z_]+|bridge_[a-z_]+|engine_source_[a-z_]+):\s*\{[^}]*?readOnlyHint:\s*(true|false)",
        text, re.MULTILINE | re.DOTALL)
    classification: Dict[str, str] = {}
    for name, cls in named:
        classification.setdefault(name, cls)
    for name, read_only in inline:
        classification.setdefault(name, "readOnly" if read_only == "true" else "mutating")

    groups: Dict[str, List[str]] = {}
    for name, cls in classification.items():
        groups.setdefault(cls, []).append(name)

    headings = [
        ("readOnly", "Read-only", "Cannot change editor, project, or disk state. Safe to call unprompted."),
        ("mutating", "Mutating", "Changes state additively. A repeat call adds more. Undoable through the UE4 transaction system."),
        ("mutatingIdempotent", "Mutating, convergent", "Changes state, but the same arguments converge on the same end state."),
        ("destructiveIdempotent", "Destructive, convergent", "Removes or overwrites existing state, converging on the same result. Checkpoint first."),
        ("destructive", "Destructive", "Removes or overwrites state in a way a plain undo may not recover. Checkpoint first."),
    ]

    lines = [
        "# Tool Catalog",
        "",
        "Every tool the `unreal-bridge` MCP server exposes, with the safety",
        "classification it actually carries in `mcp-server/src/annotations.ts`.",
        "",
        "Generated by `python Scripts/ue427.py catalog`. Do not hand-edit: the",
        "test suite fails when this file drifts from the annotations map.",
        "",
        "Total: %d tools." % len(classification),
        "",
    ]
    for key, title, blurb in headings:
        names = sorted(groups.get(key, []))
        if not names:
            continue
        lines.append("## %s (%d)" % (title, len(names)))
        lines.append("")
        lines.append(blurb)
        lines.append("")
        for name in names:
            lines.append("- `%s`" % name)
        lines.append("")
    return "\n".join(lines)


def write_catalog() -> int:
    """Write the generated tool catalog into the skill references."""
    path = os.path.join(SKILL_SRC, "references", "tool-catalog.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(generate_catalog())
    print("wrote %s" % path)
    return 0


# ------------------------------------------------------------------- main

def main(argv: Optional[List[str]] = None) -> int:
    """Parse arguments and dispatch a subcommand."""
    parser = argparse.ArgumentParser(
        prog="ue427",
        description="Install, diagnose, and launch the UE4.27 bridge for Claude Code, Codex, and Gemini.")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--agent", action="append", choices=[*AGENTS, "all"],
                       help="Agent to act on; repeatable. Default: all detected.")
        p.add_argument("--scope", action="append", choices=["user", "project"],
                       help="Install scope; repeatable. Default: user.")
        p.add_argument("--project", help="Path to the UE4.27 .uproject or its directory.")
        p.add_argument("--dry-run", action="store_true", help="Print actions, write nothing.")

    p_install = sub.add_parser("install", help="Install the skill and MCP registration")
    add_common(p_install)
    p_install.add_argument("--copy", action="store_true",
                           help="Copy the skill instead of linking to the canonical source.")

    p_uninstall = sub.add_parser("uninstall", help="Remove the skill and MCP registration")
    add_common(p_uninstall)

    p_doctor = sub.add_parser("doctor", help="Diagnose build, install, config, project, session")
    p_doctor.add_argument("--project")

    p_repair = sub.add_parser("repair", help="Fix what is safe to fix automatically")
    p_repair.add_argument("--project")
    p_repair.add_argument("--dry-run", action="store_true")

    p_update = sub.add_parser("update", help="Pull, rebuild, and reinstall")
    p_update.add_argument("--project")

    p_start = sub.add_parser("start", help="Verify, then launch an agent in the project")
    p_start.add_argument("agent", choices=list(AGENTS))
    p_start.add_argument("--project")

    p_verify = sub.add_parser("verify", help="Prove each client discovers the skill")
    p_verify.add_argument("--project")

    sub.add_parser("catalog", help="Regenerate the tool catalog from annotations.ts")

    args = parser.parse_args(argv)

    if args.command in ("install", "uninstall"):
        agents = args.agent or []
        if not agents or "all" in agents:
            agents = detect_agents()
            if not agents:
                print("No agent CLIs detected (claude/codex/gemini).", file=sys.stderr)
                return 1
            print("Detected agents: %s" % ", ".join(agents))
        scopes = args.scope or ["user"]
        project_dir = None
        if args.project:
            project = os.path.abspath(args.project)
            project_dir = project if os.path.isdir(project) else os.path.dirname(project)
        elif "project" in scopes:
            print("--scope project requires --project <path>.", file=sys.stderr)
            return 1
        if args.command == "install":
            return install(agents, scopes, args.project, project_dir,
                           args.dry_run, use_links=not args.copy)
        return uninstall(agents, scopes, project_dir, args.dry_run)

    if args.command == "doctor":
        return doctor(args.project)
    if args.command == "repair":
        return repair(args.project, args.dry_run)
    if args.command == "update":
        return update(args.project)
    if args.command == "start":
        return start(args.agent, args.project)
    if args.command == "verify":
        return verify(args.project)
    if args.command == "catalog":
        return write_catalog()
    return 1


if __name__ == "__main__":
    sys.exit(main())
