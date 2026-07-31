// SessionStart hook. Prints a visible marker so a fresh session proves the
// project hook config loaded, and injects the mandatory startup reading.
const handoff = "docs/UE427_PUERST_MCP_HANDOFF_2026-07-31.md";
console.log(
  JSON.stringify({
    systemMessage: "UE4_BRIDGE_HOOK_ACTIVE",
    hookSpecificOutput: {
      hookEventName: "SessionStart",
      additionalContext:
        "UE4_BRIDGE_HOOK_ACTIVE. Before any bridge work, read AGENTS.md and " +
        handoff +
        " in full. Follow the strict Unreal tooling protocol: puerts_* tools only, " +
        "no HTTP/REST/socket/shell fallbacks to the editor, and report exact native " +
        "tool errors instead of switching transports. Before launching UE4, use " +
        "Scripts/start-ue4-project.ps1; it refuses duplicate UE4Editor processes " +
        "unless a human explicitly chooses -CloseExisting or -AllowAdditional.",
    },
  }),
);
