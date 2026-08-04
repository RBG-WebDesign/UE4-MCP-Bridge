import { readFileSync, writeFileSync, existsSync } from 'fs';
import { resolve } from 'path';

const repoRoot = resolve('.');
const inventoryPath = `${repoRoot}/docs/TOOL_INVENTORY.json`;
const metadataPath = `${repoRoot}/docs/TOOL_CAPABILITY_METADATA.json`;
const matrixPath = `${repoRoot}/docs/MOCK_PROMOTION_MATRIX.json`;
const auditOutputPath = `${repoRoot}/docs/evidence/mock-promotion/2026-08-04/final-audit.json`;

const inventory = JSON.parse(readFileSync(inventoryPath, 'utf8'));
const metadata = JSON.parse(readFileSync(metadataPath, 'utf8'));
const matrix = existsSync(matrixPath) ? JSON.parse(readFileSync(matrixPath, 'utf8')) : [];

const auditRows = [];
let acceptedCount = 0;
let revertedCount = 0;
let missingEvidenceCount = 0;

for (const tool of inventory.tools) {
  const meta = metadata.tools[tool.name] || {};
  const verifState = tool.verification || meta.verification || 'untested';
  const evidencePaths = meta.live_evidence || tool.live_evidence || [];
  
  let auditResult = 'PASS';
  const missingRequirements = [];

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
    if (!meta.notes) {
      auditResult = 'FAIL';
      missingRequirements.push('Missing retirement notes');
    }
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
    disposition: tool.backend === 'legacy_http' ? (tool.replacement_tool ? 'replaced' : 'retired') : verifState,
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
