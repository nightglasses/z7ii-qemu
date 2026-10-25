import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import { checkContent, checkSite } from './checks.mjs'

test('prose checks preserve operational paths and numeric hardware identities', () => {
  assert.doesNotThrow(() => checkContent(
    'Run work/build/qemu. PA 0x20060104 and raw 20060104 are addresses. Dark 2026 is the theme. '
      + '[Fixed edition](https://example.org/spec/2024/CRD-model-20240321/#formula)',
    'contract.md'))
  for (const source of [
    '\u4e2d', 'Issue #123', 'https://github.com/example/project/issues/123',
    'Status: 2099-12-31', '/evidence/profile-20991231/result.json',
    'work/runs/private/proof.json', '/Users/sample/private/proof.json',
    '/home/sample/private/proof.json', 'C:\\Users\\sample\\private\\proof.json',
  ]) assert.throws(() => checkContent(source, 'contract.md'), /contract\.md/)
})

test('site checks cover article discovery and published evidence boundaries', () => {
  const srcDir = mkdtempSync(join(tmpdir(), 'handbook-check-'))
  try {
    mkdirSync(join(srcDir, 'public/evidence/contract'), { recursive: true })
    writeFileSync(join(srcDir, 'public/evidence/contract/result.json'), '{}')
    const record = { title: 'Result', href: '/evidence/contract/result.json', kind: 'Observation' }
    const site = {
      srcDir, pages: ['index.md', 'contract.md', 'nested/AGENTS.md'],
      themeConfig: { sidebar: { '/': [{ items: [{ link: '/contract#result' }] }] } },
      evidence: { 'contract.md': { records: [record] } },
    }
    assert.doesNotThrow(() => checkSite(site))
    assert.throws(() => checkSite({ ...site, pages: [...site.pages, 'unlisted.md'] }), /missing from navigation: unlisted/)
    assert.throws(() => checkSite({ ...site, pages: ['index.md'] }), /missing page: contract/)
    assert.throws(() => checkSite({ ...site, evidence: { 'contract.md': { records: [record, record] } } }), /Duplicate evidence/)
    assert.throws(() => checkSite({ ...site, evidence: { 'contract.md': { records: [{ ...record, href: '/evidence/missing.json' }] } } }), /Missing published evidence/)
    assert.throws(() => checkSite({ ...site, evidence: { 'contract.md': { records: [{ ...record, href: '/evidence/contract' }] } } }), /Missing published evidence/)
    assert.throws(() => checkSite({ ...site, evidence: { 'contract.md': { records: [{ ...record, href: '/elsewhere/result.json' }] } } }), /Missing published evidence/)
  } finally {
    rmSync(srcDir, { recursive: true, force: true })
  }
})
