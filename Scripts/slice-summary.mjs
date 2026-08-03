#!/usr/bin/env node
// Consolidate the seven slice evidence files into one ranked gap list.
//
// The ranking in docs/VERTICAL_SLICES.md used to be typed by hand, which means
// it was correct exactly until the next slice changed. This derives it from the
// evidence files instead, so the table and the runs cannot disagree.
//
// Reads docs/evidence/slice-<id>.json (warm phase only: a cold run re-reads what
// a warm run sealed and discovers no new gaps). Writes
// docs/evidence/slice-summary.json and prints the markdown table.
//
//   node Scripts/slice-summary.mjs

import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const evidenceDir = join(repoRoot, "docs", "evidence");

const files = readdirSync(evidenceDir)
  .filter((f) => /^slice-[a-z]+\.json$/.test(f) && f !== "slice-summary.json")
  .sort();
if (files.length === 0) {
  console.error("no slice evidence found. Run the slice harnesses first.");
  process.exit(1);
}

const slices = files.map((f) => JSON.parse(readFileSync(join(evidenceDir, f), "utf8")));

/** primitive name -> { gap_kind, slices[], why, owner, legacy_equivalent } */
const gaps = new Map();
for (const slice of slices) {
  for (const m of slice.missing_primitives ?? []) {
    const entry = gaps.get(m.tool) ?? {
      tool: m.tool,
      state: m.state,
      gap_kind: m.gap_kind ?? "NEW",
      owner: m.proposed_schema?.owner ?? "UNOWNED",
      legacy_equivalent: m.legacy_equivalent ?? null,
      why: m.why,
      blocks: [],
    };
    entry.blocks.push(slice.slice);
    gaps.set(m.tool, entry);
  }
}

const ranked = [...gaps.values()].sort((a, b) =>
  b.blocks.length - a.blocks.length || a.tool.localeCompare(b.tool));

const summary = {
  purpose: "Consolidated missing-primitive list derived from the seven vertical slice evidence files. Do not hand-edit: regenerate with node Scripts/slice-summary.mjs.",
  generated_at: new Date().toISOString(),
  slices: slices.map((s) => ({
    slice: s.slice,
    verdict: s.verdict,
    phase: s.phase,
    editor_available: s.editor?.available ?? false,
    missing_primitive_count: (s.missing_primitives ?? []).length,
    checks: s.checks,
  })),
  verdict_counts: slices.reduce((acc, s) => {
    acc[s.verdict] = (acc[s.verdict] ?? 0) + 1;
    return acc;
  }, {}),
  distinct_missing_primitives: ranked.length,
  // PORT: the capability runs today behind a human opt-in and needs a native
  // front. NEW: nothing in either catalog has ever done this.
  by_gap_kind: ranked.reduce((acc, g) => {
    acc[g.gap_kind] = (acc[g.gap_kind] ?? 0) + 1;
    return acc;
  }, {}),
  unowned_count: ranked.filter((g) => /UNOWNED/i.test(g.owner)).length,
  missing_primitives: ranked,
};

writeFileSync(join(evidenceDir, "slice-summary.json"), JSON.stringify(summary, null, 2) + "\n");

console.log(`| Slices blocked | Primitive | Gap | Owner | Blocks |`);
console.log(`|---|---|---|---|---|`);
for (const g of ranked) {
  const gap = g.gap_kind === "PORT"
    ? `PORT (${g.legacy_equivalent.map((e) => e.tool).join(", ")})`
    : "NEW";
  console.log(`| ${g.blocks.length} | \`${g.tool}\` | ${gap} | ${g.owner} | ${g.blocks.join(", ")} |`);
}
console.log(`\n${ranked.length} distinct primitives across ${slices.length} slices.`);
console.log(`gap kinds: ${JSON.stringify(summary.by_gap_kind)}, unowned: ${summary.unowned_count}`);
console.log(`verdicts: ${JSON.stringify(summary.verdict_counts)}`);
console.log(`written: ${join(evidenceDir, "slice-summary.json")}`);
