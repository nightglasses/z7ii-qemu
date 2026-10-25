import DefaultTheme from 'vitepress/theme'
import './custom.css'
import { inBrowser } from 'vitepress'
import { h } from 'vue'
import Evidence from './evidence.vue'

export default {
  extends: DefaultTheme,
  Layout: () => h(DefaultTheme.Layout, null, { 'doc-after': () => h(Evidence) }),
  enhanceApp({ app, router }) {
    if (inBrowser) {
      const initPeleZoom = async () => {
        try {
          const { enableZoom } = await import('pele')

          document.querySelectorAll<HTMLElement>('.pele-container').forEach((container) => {
            if (container.getAttribute('data-zoom-initialized')) return
            container.setAttribute('data-zoom-initialized', 'true')

            const zoom = enableZoom(container, {
              always: true,
              maxScale: 6,
            })

            // Pele handles modified wheel events; fullscreen also accepts plain wheel.
            container.addEventListener('wheel', (event) => {
              if (!container.classList.contains('is-fullscreen') || event.ctrlKey || event.metaKey) return
              event.preventDefault()
              const delta = Math.max(-24, Math.min(24, event.deltaY))
              zoom.zoomBy(2 ** (-delta * 0.02))
            }, { passive: false })

            // Add Fullscreen toggle button to the zoom toolbar
            const toolbar = container.querySelector('.pele-zoom')
            if (toolbar && !toolbar.querySelector('.pele-zoom-fs')) {
              const fsBtn = document.createElement('button')
              fsBtn.type = 'button'
              fsBtn.className = 'pele-zoom-fs'
              fsBtn.title = 'Toggle Fullscreen (or press ESC to exit)'
              fsBtn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 3H5a2 2 0 0 0-2 2v3m18 0V5a2 2 0 0 0-2-2h-3m0 18h3a2 2 0 0 0 2-2v-3M3 16v3a2 2 0 0 0 2 2h3"/></svg>`
              fsBtn.addEventListener('click', (e) => {
                e.stopPropagation()
                const isFs = container.classList.toggle('is-fullscreen')
                document.body.classList.toggle('has-diagram-fullscreen', isFs)
              })
              toolbar.appendChild(fsBtn)
            }
          })
        } catch (e) {
          console.warn('Pele interactive zoom initialization error:', e)
        }
      }

      router.onAfterRouteChanged = () => {
        setTimeout(initPeleZoom, 150)
      }

      if (typeof window !== 'undefined') {
        window.addEventListener('DOMContentLoaded', () => {
          setTimeout(initPeleZoom, 150)
        })
        setTimeout(initPeleZoom, 250)

        // Press Escape to exit fullscreen diagram mode
        window.addEventListener('keydown', (e) => {
          if (e.key === 'Escape') {
            document.querySelectorAll('.pele-container.is-fullscreen').forEach((c) => {
              c.classList.remove('is-fullscreen')
            })
            document.body.classList.remove('has-diagram-fullscreen')
          }
        })
      }
    }
  }
}
