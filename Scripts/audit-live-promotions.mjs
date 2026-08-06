import { readFileSync, writeFileSync, existsSync } from 'fs';
import { resolve } from 'path';

const repoRoot = resolve('.');
const inventoryPath = `${repoRoot}/docs/TOOL_INVENTORY.json`;
const metadataPath = `${repoRoot}/docs/TOOL_CAPABILITY_METADATA.json`;
const auditOutputPath = `${repoRoot}/docs/evidence/mock-promotion/2026-08-04/final-audit.json`;

const inventory = JSON.parse(readFileSync(inventoryPath, 'utf8'));
const metadata = JSON.parse(readFileSync(metadataPath, 'utf8'));

const auditRows = [];
let acceptedCount = 0;
let revertedCount = 0;
let missingEvidenceCount = 0;

// Valid backend-to-verification combinations
const VALID_COMBINATIONS = {
  native_pipe: new Set(['live_verified', 'live_partial', 'implemented']),
  native_pipe_alias: new Set(['compat_verified', 'mock_only']),
  legacy_http: new Set(['replaced', 'retired', 'untested']),
  server_local: new Set(['editor_free_verified', 'pending_live'])
};

for (const tool of inventory.tools) {
  const meta = metadata.tools[tool.name] || {};
  const verifState = tool.verification;
  const evidencePaths = meta.live_evidence || tool.live_evidence || [];
  
  let auditResult = 'PASS';
  const missingRequirements = [];

  // Enforce valid backend to state combinations
  const validStates = VALID_COMBINATIONS[tool.backend];
  if (!validStates || !validStates.has(verifState)) {
    auditResult = 'FAIL';
    missingRequirements.push(`Invalid backend/verification combination: ${tool.backend} cannot be marked '${verifState}'`);
  }

  if (verifState === 'live_verified') {
    if (evidencePaths.length === 0) {
      auditResult = 'FAIL';
      missingRequirements.push('Missing live_evidence array');
    }
    for (const ep of evidencePaths) {
      if (!existsSync(`${repoRoot}/${ep}`)) {
        auditResult = 'FAIL';
        missingRequirements.push(`Evidence path not found: ${ep}`);
      }
    }
  } else if (verifState === 'compat_verified') {
    if (!tool.replacement_tool) {
      auditResult = 'FAIL';
      missingRequirements.push('Missing replacement_tool for alias');
    }
    if (evidencePaths.length === 0) {
      auditResult = 'FAIL';
      missingRequirements.push('Missing evidence paths for alias');
    }
  } else if (verifState === 'replaced') {
    if (!tool.replacement_tool) {
      auditResult = 'FAIL';
      missingRequirements.push('Missing replacement_tool for replaced registration');
    }
  } else if (verifState === 'retired') {
    // Legacy HTTP tools marked retired are disabled
  }

  if (auditResult === 'PASS') {
    acceptedCount++;
  } else {
    missingEvidenceCount++;
  }

  auditRows.push({
    name: tool.name,
    backend: tool.backend,
    canonical_capability: tool.canonical_capability || tool.name,
    final_state: verifState,
    disposition: verifState,
    evidence_paths: evidencePaths,
    audit_result: auditResult,
    missing_requirements: missingRequirements
  });
}

const auditReport = {
  timestamp: new Date().toISOString(),
  total_audited: auditRows.length,
  promotions_accepted: acceptedCount,
  promotions_reverted: revertedCount,
  missing_evidence_rows: missingEvidenceCount,
  rows: auditRows
};

writeFileSync(auditOutputPath, JSON.stringify(auditReport, null, 2) + '\n', 'utf8');
console.log(`Audit complete: ${auditRows.length} tools audited. Passed: ${acceptedCount}, Failed: ${missingEvidenceCount}. Report written to ${auditOutputPath}`);
