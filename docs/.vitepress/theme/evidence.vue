<script setup lang="ts">
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import archive from '../evidence.json'

type Record = { title: string; href: string; kind: string }
const { page } = useData()
const entry = computed(() => (archive as { [page: string]: { records: Record[] } })[page.value.relativePath])
</script>

<template>
  <aside v-if="entry" class="evidence-panel" aria-label="Evidence availability">
    <h2 id="published-evidence">Evidence</h2>
    <p>The observations and their limits are embedded in this page.
      <a :href="withBase('/validation/evidence')">Archive scope</a>
      distinguishes published records from historical run labels.</p>
    <details v-if="entry.records.length">
      <summary>Published attachments ({{ entry.records.length }})</summary>
      <ul>
        <li v-for="record in entry.records" :key="record.href">
          <a :href="withBase(record.href)">{{ record.title }}</a>
          <span class="evidence-kind"> — {{ record.kind }}</span>
        </li>
      </ul>
    </details>
    <p v-else class="evidence-kind">No raw capture is archived for this historical record.
      Its evidence is limited to the observations and source references embedded above.</p>
  </aside>
</template>

<style scoped>
.evidence-panel { margin-top: 2rem; padding: 1.25rem; border: 1px solid var(--vp-c-divider); border-radius: 6px; background: var(--vp-c-bg-alt); font-size: .9rem; overflow-wrap: anywhere; }
.evidence-panel h2 { margin-top: 0; border: 0; padding: 0; font-size: 1rem; }
.evidence-panel summary { cursor: pointer; color: var(--vp-c-brand-1); }
.evidence-kind { color: var(--vp-c-text-3); }
</style>
