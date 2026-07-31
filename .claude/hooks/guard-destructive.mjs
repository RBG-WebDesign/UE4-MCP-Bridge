// PreToolUse hook matched on destructive unreal-bridge tools
// (mcp__unreal-bridge__puerts_delete_*). Always escalates to an explicit
// user prompt; it never auto-denies, so a deliberate confirmation goes through.
let raw = "";
process.stdin.on("data", (c) => (raw += c));
process.stdin.on("end", () => {
  let tool = "a destructive unreal-bridge tool";
  try {
    tool = JSON.parse(raw).tool_name || tool;
  } catch {
    // fall through with the generic name
  }
  console.log(
    JSON.stringify({
      hookSpecificOutput: {
        hookEventName: "PreToolUse",
        permissionDecision: "ask",
        permissionDecisionReason:
          tool +
          " is destructive in the Unreal editor. AGENTS.md safety rules " +
          "require explicit confirmation (and a source control checkpoint) " +
          "before destructive changes.",
      },
    }),
  );
  process.exit(0);
});
