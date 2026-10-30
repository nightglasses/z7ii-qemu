import { existsSync, statSync } from 'node:fs'
import { resolve } from 'node:path'

export function checkContent(source, page) {
  const rules = [
    [/\p{Script=Han}/u, 'Public handbook prose must be English'],
    [/\bissue[\s#_-]*\d+\b|github\.com\/[^\s)]+\/issues\//i, 'Document the technical topic instead of a work ticket'],
    [/work\/(?:runs|issue\d+|research|docsfix|ida|gui|validation)\//, 'Embed or publish the evidence instead of citing a private capture'],
    [/\/Users\/[^/\s]+\/|\/home\/[^/\s]+\/|[A-Z]:\\Users\\[^\\\s]+\\/i, 'Remove personal absolute paths'],
  ]
  for (const [pattern, message] of rules) {
    if (pattern.test(source)) throw new Error(`${message}: ${page}`)
  }
  // An immutable external source URL may contain its original publication identity.
  const presentation = source.replace(/https?:\/\/[^\s)]+/g, '')
  if (/\b20\d{2}-[01]\d-[0-3]\d\b|[-/]20\d{2}(?:0[1-9]|1[0-2])(?:0[1-9]|[12]\d|3[01])\b/.test(presentation)) {
    throw new Error(`Use source revisions and input profiles instead of calendar entries: ${page}`)
  }
}

function navigationLinks(value) {
  if (Array.isArray(value)) return value.flatMap(navigationLinks)
  if (!value || typeof value !== 'object') return []
  return Object.entries(value).flatMap(([key, child]) =>
    key === 'link' && typeof child === 'string' ? [child] : navigationLinks(child))
}

export function checkSite({ srcDir, pages, themeConfig, evidence }) {
  const sources = new Set(pages)
  const links = new Set(navigationLinks(themeConfig).map(link =>
    link.split(/[?#]/)[0].replace(/\.(?:md|html)$/, '')))
  for (const page of sources) {
    if (page === 'index.md' || page.endsWith('/AGENTS.md')) continue
    if (!links.has('/' + page.replace(/\.md$/, ''))) {
      throw new Error(`Article is missing from navigation: ${page}`)
    }
  }
  for (const [source, entry] of Object.entries(evidence)) {
    if (!sources.has(source)) throw new Error(`Evidence refers to a missing page: ${source}`)
    const seen = new Set()
    for (const record of entry.records) {
      if (seen.has(record.href)) throw new Error(`Duplicate evidence attachment on ${source}: ${record.href}`)
      seen.add(record.href)
      const path = resolve(srcDir, 'public', record.href.slice(1))
      if (!record.href.startsWith('/evidence/') || !existsSync(path) || !statSync(path).isFile()) {
        throw new Error(`Missing published evidence on ${source}: ${record.href}`)
      }
    }
  }
}
