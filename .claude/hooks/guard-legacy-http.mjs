// PreToolUse hook on Bash|PowerShell. The legacy HTTP listener is
// migration-only and must never be enabled silently. Any shell command that
// touches MCP_ENABLE_LEGACY_HTTP is escalated to an explicit user prompt.
let raw = "";
process.stdin.on("data", (c) => (raw += c));
process.stdin.on("end", () => {
  let input = {};
  try {
    input = JSON.parse(raw);
  } catch {
    process.exit(0); // unparseable input: stay out of the way
  }
  const cmd = (input.tool_input && input.tool_input.command) || "";
  if (/MCP_ENABLE_LEGACY_HTTP/i.test(cmd)) {
    console.log(
      JSON.stringify({
        hookSpecificOutput: {
          hookEventName: "PreToolUse",
          permissionDecision: "ask",
          permissionDecisionReason:
            "This command touches MCP_ENABLE_LEGACY_HTTP. The legacy HTTP " +
            "listener is migration-only and requires an explicit human OK " +
            "(AGENTS.md, strict Unreal tooling protocol rule 4).",
        },
      }),
    );
  }
  process.exit(0);
});
