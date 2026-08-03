#!/usr/bin/env python3
"""Acceptance tests for the ue427 skill tooling.

Editor-free: everything here runs against temporary directories and the
repository's own files. No UE4 editor, no network, no agent CLI required.

Covers:
  - UE4.27 detection from .uproject and from Build.version
  - rejection of 4.26, 5.x, and unknown versions
  - absence of any environment-variable version bypass
  - Codex TOML merge preserving unrelated servers, and idempotency
  - Gemini settings merge preserving unrelated keys
  - skill install as link, backup of an existing destination, idempotency
  - dry run writing nothing
  - shared .agents location installed once per run
  - tool catalog staying in sync with annotations.ts
  - the skill declaring no forbidden transport

Run:  python Scripts/ue427-skill-acceptance.py
      npm run test:ue427-skill
"""

from __future__ import annotations

import json
import os
import re
import shutil
import sys
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "Scripts"))
sys.path.insert(0, os.path.join(REPO, "skills", "unreal-engine-4-27", "scripts"))

import ue427  # noqa: E402
import verify_project_version as vpv  # noqa: E402


def write_uproject(directory: str, association: str) -> str:
    """Create a minimal .uproject with the given EngineAssociation."""
    path = os.path.join(directory, "Test.uproject")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"FileVersion": 3, "EngineAssociation": association}, handle)
    return path


def write_build_version(root: str, major: int, minor: int) -> None:
    """Create Engine/Build/Build.version under a fake engine root."""
    build_dir = os.path.join(root, "Engine", "Build")
    os.makedirs(build_dir, exist_ok=True)
    with open(os.path.join(build_dir, "Build.version"), "w", encoding="utf-8") as handle:
        json.dump({"MajorVersion": major, "MinorVersion": minor, "PatchVersion": 2}, handle)


class TestVersionGate(unittest.TestCase):
    """The 4.27 gate must accept only 4.27, with no way to switch it off."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_accepts_427(self) -> None:
        write_uproject(self.dir, "4.27")
        result = vpv.verify_project(self.dir, env={})
        self.assertTrue(result["ok"])
        self.assertEqual(result["version"], "4.27")

    def test_accepts_4272_patch_association(self) -> None:
        write_uproject(self.dir, "4.27.2")
        self.assertTrue(vpv.verify_project(self.dir, env={})["ok"])

    def test_rejects_426(self) -> None:
        write_uproject(self.dir, "4.26")
        result = vpv.verify_project(self.dir, env={})
        self.assertFalse(result["ok"])
        self.assertIn("4.26", result["error"])

    def test_rejects_ue5(self) -> None:
        write_uproject(self.dir, "5.3")
        result = vpv.verify_project(self.dir, env={})
        self.assertFalse(result["ok"])
        self.assertIn("5.3", result["error"])

    def test_source_build_uses_build_version(self) -> None:
        project = os.path.join(self.dir, "Games", "MyGame")
        os.makedirs(project)
        write_uproject(project, "{1234ABCD-0000-0000-0000-000000000000}")
        write_build_version(self.dir, 4, 27)
        result = vpv.verify_project(project, env={})
        self.assertTrue(result["ok"])
        self.assertEqual(result["source"], "Build.version")

    def test_source_build_rejects_non_427(self) -> None:
        project = os.path.join(self.dir, "Games", "MyGame")
        os.makedirs(project)
        write_uproject(project, "")
        write_build_version(self.dir, 5, 1)
        result = vpv.verify_project(project, env={})
        self.assertFalse(result["ok"])
        self.assertIn("5.1", result["error"])

    def test_unknown_version_is_explicit(self) -> None:
        write_uproject(self.dir, "")
        result = vpv.verify_project(self.dir, env={})
        self.assertFalse(result["ok"])
        self.assertIn("Unknown engine version", result["error"])

    def test_no_environment_bypass_exists(self) -> None:
        """No env var may switch the gate off, including the legacy name."""
        write_uproject(self.dir, "5.0")
        for candidate in ("UE427_TEST_BYPASS_VERSION", "UE427_SKIP_VERSION_CHECK"):
            result = vpv.verify_project(self.dir, env={candidate: "1"})
            self.assertFalse(result["ok"], "%s must not bypass the check" % candidate)
        self.assertFalse(hasattr(vpv, "BYPASS_ENV"))


class TestCodexTomlMerge(unittest.TestCase):
    """Codex config edits must never disturb other servers or settings."""

    SECTION = "[mcp_servers.unreal-bridge]\ncommand = 'node'\nargs = ['x.js']\n"

    def test_append_preserves_other_sections(self) -> None:
        existing = "[mcp_servers.other]\ncommand = 'node'\n\n[profile]\nname = 'x'\n"
        merged = ue427.merge_toml_section(existing, "mcp_servers.unreal-bridge", self.SECTION)
        self.assertIn("[mcp_servers.other]", merged)
        self.assertIn("[profile]", merged)
        self.assertIn("[mcp_servers.unreal-bridge]", merged)

    def test_merge_is_idempotent(self) -> None:
        once = ue427.merge_toml_section("", "mcp_servers.unreal-bridge", self.SECTION)
        twice = ue427.merge_toml_section(once, "mcp_servers.unreal-bridge", self.SECTION)
        self.assertEqual(once, twice)

    def test_merge_replaces_stale_entry(self) -> None:
        stale = "[mcp_servers.unreal-bridge]\ncommand = 'old'\n\n[keep]\nx = 1\n"
        merged = ue427.merge_toml_section(stale, "mcp_servers.unreal-bridge", self.SECTION)
        self.assertNotIn("'old'", merged)
        self.assertIn("[keep]", merged)

    def test_generated_section_uses_absolute_path(self) -> None:
        section = ue427.codex_toml_section("D:/repo/mcp-server/dist/index.js", "D:/repo", {})
        self.assertIn("D:/repo/mcp-server/dist/index.js", section)
        self.assertIn("command = 'node'", section)


class TestSkillInstall(unittest.TestCase):
    """Installing must link, back up, stay idempotent, and honour dry run."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.join(self.tmp.name, "skills")
        os.makedirs(self.root)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_installs_and_resolves_to_canonical(self) -> None:
        installer = ue427.Installer()
        dst = installer.install_skill(self.root)
        self.assertTrue(os.path.exists(os.path.join(dst, "SKILL.md")))
        self.assertEqual(os.path.realpath(dst), os.path.realpath(ue427.SKILL_SRC))

    def test_reinstall_is_idempotent(self) -> None:
        ue427.Installer().install_skill(self.root)
        second = ue427.Installer()
        second.install_skill(self.root)
        self.assertEqual([c for c in second.changed if c.startswith("backup")], [])

    def test_existing_copy_is_backed_up_outside_skills_root(self) -> None:
        stale = os.path.join(self.root, ue427.SKILL_NAME)
        os.makedirs(stale)
        with open(os.path.join(stale, "old.txt"), "w", encoding="utf-8") as handle:
            handle.write("stale")
        ue427.Installer().install_skill(self.root)
        # A backup inside the skills root would be discovered as a second skill.
        self.assertEqual([d for d in os.listdir(self.root) if ".bak-" in d], [])
        backup_root = self.root + "-backups"
        backups = os.listdir(backup_root)
        self.assertEqual(len(backups), 1)
        self.assertTrue(os.path.isfile(os.path.join(backup_root, backups[0], "old.txt")))

    def test_dry_run_writes_nothing(self) -> None:
        installer = ue427.Installer(dry_run=True)
        installer.install_skill(self.root)
        self.assertEqual(os.listdir(self.root), [])
        self.assertTrue(installer.changed)

    def test_shared_root_installed_once_per_run(self) -> None:
        installer = ue427.Installer()
        installer.install_skill(self.root)
        installer.install_skill(self.root)
        links = [c for c in installer.changed if "link-skill" in c or "copy-skill" in c]
        self.assertEqual(len(links), 1)

    def test_copy_mode_produces_a_real_directory(self) -> None:
        installer = ue427.Installer(use_links=False)
        dst = installer.install_skill(self.root)
        self.assertFalse(os.path.islink(dst))
        self.assertTrue(os.path.isfile(os.path.join(dst, "SKILL.md")))


class TestServerTarget(unittest.TestCase):
    """Agents must be pointed at the existing unreal-bridge server only."""

    def test_server_name_and_entry(self) -> None:
        self.assertEqual(ue427.SERVER_NAME, "unreal-bridge")
        self.assertTrue(ue427.SERVER_ENTRY.endswith(os.path.join("mcp-server", "dist", "index.js")))

    def test_no_http_transport_anywhere_in_the_cli(self) -> None:
        with open(os.path.join(REPO, "Scripts", "ue427.py"), encoding="utf-8") as handle:
            text = handle.read()
        for forbidden in ("30010", "remote/object", "WebControl", "http://127.0.0.1"):
            self.assertNotIn(forbidden, text, "ue427.py must not reference %s" % forbidden)


class TestSkillContent(unittest.TestCase):
    """The canonical skill must be well formed and free of banned transports."""

    def setUp(self) -> None:
        self.skill = os.path.join(REPO, "skills", "unreal-engine-4-27", "SKILL.md")
        with open(self.skill, encoding="utf-8") as handle:
            self.text = handle.read()

    def test_frontmatter_is_portable_and_names_the_skill(self) -> None:
        match = re.match(r"\A---\n(.*?)\n---\n", self.text, re.DOTALL)
        self.assertIsNotNone(match, "SKILL.md needs a frontmatter block")
        fields = dict(re.findall(r"^([A-Za-z-]+):\s*(.*)$", match.group(1), re.M))
        self.assertEqual(fields.get("name"), "unreal-engine-4-27")
        description = fields.get("description", "")
        self.assertIn("4.27", description)
        self.assertIn("ONLY", description)
        self.assertIn("STOP", description)
        for key in fields:
            self.assertIn(key, {"name", "description", "license", "allowed-tools", "metadata"})

    def test_declares_the_puerts_transport_and_not_http(self) -> None:
        self.assertIn("puerts_", self.text)
        self.assertIn("named pipe", self.text)
        # Concrete HTTP transport details must not appear at all: a port, a
        # console command that starts a web server, or a URL would be an
        # instruction an agent could follow.
        for forbidden in ("30010", "WebControl.StartServer", "http://", "/remote/object"):
            self.assertNotIn(forbidden, self.text,
                             "SKILL.md must not mention %s" % forbidden)
        # Naming a banned transport is allowed only where it is being banned,
        # which is the point of the prohibition list.
        for line in self.text.splitlines():
            if "Remote Control" in line:
                self.assertIn("Never use", line,
                              "Remote Control may only appear in the prohibition: %r" % line)

    def test_uses_ue4_executable_names(self) -> None:
        self.assertIn("UE4Editor", self.text)

    def test_references_exist(self) -> None:
        ref_dir = os.path.join(os.path.dirname(self.skill), "references")
        for name in ("setup.md", "operations.md", "tool-catalog.md",
                     "security.md", "troubleshooting.md"):
            self.assertTrue(os.path.isfile(os.path.join(ref_dir, name)), name)


class TestToolCatalog(unittest.TestCase):
    """The generated catalog must match annotations.ts exactly."""

    def test_catalog_is_current(self) -> None:
        path = os.path.join(REPO, "skills", "unreal-engine-4-27",
                            "references", "tool-catalog.md")
        with open(path, encoding="utf-8") as handle:
            on_disk = handle.read()
        self.assertEqual(
            on_disk.strip(), ue427.generate_catalog().strip(),
            "tool-catalog.md is stale. Run: python Scripts/ue427.py catalog")

    def test_catalog_lists_real_tools(self) -> None:
        catalog = ue427.generate_catalog()
        for tool in ("puerts_diagnostic", "puerts_spawn_actor", "puerts_save",
                     "puerts_graph_inspect", "engine_source_search"):
            self.assertIn("`%s`" % tool, catalog)

    def test_destructive_tools_are_classified_as_such(self) -> None:
        catalog = ue427.generate_catalog()
        destructive = catalog.split("## Destructive")[-1]
        self.assertIn("puerts_save", destructive)
        self.assertIn("puerts_undo", destructive)


if __name__ == "__main__":
    unittest.main(verbosity=2)
