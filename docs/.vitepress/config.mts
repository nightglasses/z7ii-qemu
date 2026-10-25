import { defineConfig } from 'vitepress'
import { render as renderPele } from 'pele'
import { copyFileSync } from 'node:fs'
import { resolve } from 'node:path'
import evidence from './evidence.json'
import { checkContent, checkSite } from './checks.mjs'

const guideSidebar = [{
  text: 'System Architecture',
  items: [
    { text: 'Architecture Overview', link: '/architecture' },
    { text: 'CPU Topologies', link: '/architecture/cpu' },
    { text: 'EXPEED 6 Family', link: '/architecture/expeed6' },
    { text: 'Native Build & Dependencies', link: '/build' },
    { text: 'Machine & Device Layout', link: '/layout' },
    { text: 'Documentation Standards', link: '/writing' },
    { text: 'Evidence Discipline', link: '/AGENTS' },
  ],
}]

export default defineConfig({
    title: 'Nikon Z7II Emulation Handbook',
    description: 'Diagnostic QEMU virtual machine for unchanged Nikon Z7II (C1.70) firmware',
    cleanUrls: true,
    appearance: 'force-dark',
    outDir: '../work/handbook/dist',
    cacheDir: '../work/handbook/cache',
    buildEnd: (site) => {
      checkSite({ srcDir: site.srcDir, pages: site.pages,
        themeConfig: site.site.themeConfig, evidence })
      copyFileSync(resolve(site.srcDir, 'refmap/REFS.tsv'), resolve(site.outDir, 'refmap/REFS.tsv'))
      copyFileSync(resolve(site.srcDir, '.vitepress/theme/LICENSE.txt'), resolve(site.outDir, 'theme-license.txt'))
    },
    markdown: {
      theme: 'dark-plus',
      lineNumbers: true,
      config: (md) => {
        md.core.ruler.before('normalize', 'public-handbook', (state) => {
          checkContent(state.src, state.env.relativePath)
        })
        const defaultFence = md.renderer.rules.fence!
        md.renderer.rules.fence = (tokens, idx, options, env, self) => {
          const token = tokens[idx]
          if (token.info.trim() === 'mermaid') {
            const code = token.content.trim()
            const res = renderPele(code)
            if (!res?.svg) {
              throw new Error(`Diagram has no SVG: ${env.relativePath}`)
            }
            return `<div class="pele-container" style="overflow-x:auto;padding:1rem 0;display:flex;justify-content:center;">${res.svg}</div>`
          }
          return defaultFence(tokens, idx, options, env, self)
        }
      }
    },
    themeConfig: {
      siteTitle: 'Nikon Z7II Handbook',
      nav: [
        { text: 'Progress', link: '/validation/frontier' },
        { text: 'Guide', link: '/architecture' },
        { text: 'Startup', link: '/startup/first' },
        { text: 'Devices', link: '/devices/gate/protocol' },
        { text: 'Runtime', link: '/runtime/policy' },
        { text: 'Inputs', link: '/inputs/nor/descriptor' },
        { text: 'Refmap', link: '/refmap/README' },
        { text: 'Validation', link: '/validation/budgets' },
      ],
      sidebar: {
        '/architecture': guideSidebar,
        '/build': guideSidebar,
        '/layout': guideSidebar,
        '/writing': guideSidebar,
        '/AGENTS': guideSidebar,
        '/startup/': [
          {
            text: 'Startup Sequence & Reset',
            items: [
              { text: 'MAS First Diagnostic Run', link: '/startup/first' },
              { text: 'Boot Shim Contract', link: '/startup/bootshim' },
              { text: 'Dual-Core SMP & Clock Blockage', link: '/startup/smp' },
              { text: 'Bundle Abort Receipt', link: '/startup/bundleabort' },
              { text: 'Display Route Init', link: '/startup/display' },
              { text: 'Protocol Principles', link: '/startup/protocol' },
              { text: 'Relocation Storage', link: '/startup/relocation' },
              { text: 'RAM Backing & TLB Alignment', link: '/startup/ram' },
            ]
          },
          {
            text: 'Component Execution Domains',
            items: [
              { text: 'Five-Component Overview', link: '/startup/components' },
              { text: 'CPU & Board Parameters', link: '/startup/cpuboard' },
              { text: 'MEMC Initialization', link: '/startup/memc' },
              { text: 'SLA Domain Boot', link: '/startup/sla' },
              { text: 'SLA ROM Read', link: '/startup/romread' },
              { text: 'SLA Exception Vectors', link: '/startup/vectors' },
              { text: 'EX Component Profile', link: '/startup/ex' },
              { text: 'EX SysTick Dispatch', link: '/startup/exsystick' },
              { text: 'VR Component Protocol', link: '/startup/vr' },
              { text: 'VR Domain Boundaries', link: '/startup/vrdomain' },
              { text: 'NDS32 System Execution', link: '/startup/system' },
              { text: 'Private Peripherals Hypothesis', link: '/startup/peripherals' },
            ]
          }
        ],
        '/devices/': [
          {
            text: 'Control & Event Dispatchers',
            collapsed: false,
            items: [
              { text: 'Gate Control & Mask', link: '/devices/gate/protocol' },
              { text: 'Gate Dependencies', link: '/devices/gate/dependencies' },
              { text: 'Gate Routing', link: '/devices/gate/routing' },
              { text: 'Gate Runtime', link: '/devices/gate/runtime' },
              { text: 'Gate Parameter Slots', link: '/devices/gate/slots' },
              { text: 'Gate Processing Commands', link: '/devices/gate/commands' },
              { text: 'Gate Stop Lifecycle', link: '/devices/gate/stop' },
              { text: 'Gate SLA Mapping', link: '/devices/gate/sla' },
              { text: 'Banks Dual-Object Control', link: '/devices/banks/protocol' },
              { text: 'Banks Image State', link: '/devices/banks/image' },
              { text: 'Dispatcher Hierarchical IRQ', link: '/devices/dispatcher/protocol' },
              { text: 'Pairs Dual-Bank Config', link: '/devices/pairs/protocol' },
              { text: 'Pairs SLA Profile', link: '/devices/pairs/sla' },
              { text: 'Pairs Parameter Packets', link: '/devices/pairs/parameters' },
              { text: 'Event Router', link: '/devices/router/protocol' },
              { text: 'Router Runtime Requests', link: '/devices/router/runtime' },
              { text: 'Router Timing Producers', link: '/devices/router/timing' },
              { text: 'Router SLA Profile', link: '/devices/router/sla' },
              { text: 'Status Controller', link: '/devices/status/protocol' },
              { text: 'Status SLA Profile', link: '/devices/status/sla' },
              { text: 'Staging Engine', link: '/devices/staging/protocol' },
              { text: 'Staging SLA Profile', link: '/devices/staging/sla' },
              { text: 'Parameters Controller', link: '/devices/parameters/protocol' },
              { text: 'Table Controller', link: '/devices/table/protocol' },
              { text: 'Shared Parameter Words', link: '/devices/shared/protocol' },
              { text: 'Events Controller', link: '/devices/events/protocol' },
              { text: 'Flags Register', link: '/devices/flags/protocol' },
            ]
          },
          {
            text: 'Memory, Clocks & System',
            collapsed: false,
            items: [
              { text: 'MEMC Memory Controller', link: '/devices/memc/protocol' },
              { text: 'Dual-Bank SDI / DDR', link: '/devices/sdi/protocol' },
              { text: 'SDI Runtime Contracts', link: '/devices/sdi/runtime' },
              { text: 'Clock Control & PLL', link: '/devices/clock/protocol' },
              { text: 'Clock Masks', link: '/devices/clock/mask' },
              { text: 'Clock Runtime', link: '/devices/clock/runtime' },
              { text: 'System Config Registers', link: '/devices/sysconfig/protocol' },
              { text: 'Sysconfig Runtime', link: '/devices/sysconfig/runtime' },
              { text: 'Sysconfig SLA Profile', link: '/devices/sysconfig/sla' },
              { text: 'MXIC Config Controller', link: '/devices/mxic/protocol' },
              { text: 'Config View & Readback', link: '/devices/config/view' },
              { text: 'Config Interrupts', link: '/devices/config/interrupts' },
              { text: 'Config Protocol', link: '/devices/config/protocol' },
            ]
          },
          {
            text: 'DMA & Data Movement',
            collapsed: true,
            items: [
              { text: 'HDMAC DMA Controller', link: '/devices/hdmac/protocol' },
              { text: 'HDMAC Descriptor Reload', link: '/devices/hdmac/reload' },
              { text: 'Copy Engine Protocol', link: '/devices/copy/protocol' },
              { text: 'Copy Directions', link: '/devices/copy/directions' },
              { text: 'Copy Repeat Operations', link: '/devices/copy/repeat' },
              { text: 'Fill Engine Protocol', link: '/devices/fill/protocol' },
              { text: 'Fill Engine Runtime', link: '/devices/fill/runtime' },
              { text: 'Transfer Controller', link: '/devices/transfer/protocol' },
              { text: 'Transfer State & Continuation', link: '/devices/transfer/continuation' },
              { text: 'Transform & Register Overwrite', link: '/devices/transfer/overwrite' },
              { text: 'Transfer Prefix State', link: '/devices/transfer/prefix' },
              { text: 'Transfer Paint State', link: '/devices/transfer/paint' },
              { text: 'Fill Mode Selection', link: '/devices/transfer/mode' },
              { text: 'Clear Mode Selection', link: '/devices/transfer/clearmode' },
              { text: 'Rectangle Geometry', link: '/devices/transfer/rectangles' },
              { text: 'Stroke & Fill State', link: '/devices/transfer/stroke' },
              { text: 'Target Buffer Contract', link: '/devices/transfer/target' },
              { text: 'Image Sampling', link: '/devices/transfer/image' },
              { text: 'SRC_OVER Composition', link: '/devices/transfer/over' },
              { text: 'Lanes Controller', link: '/devices/lanes/protocol' },
            ]
          },
          {
            text: 'Serial, Bridges & Bus Protocols',
            collapsed: true,
            items: [
              { text: 'I2C Master Controller', link: '/devices/i2c/protocol' },
              { text: 'SPI Master Interface', link: '/devices/spi/protocol' },
              { text: 'USIO Serial Interface', link: '/devices/usio/protocol' },
              { text: 'MAS / SLA Bridge', link: '/devices/bridge/protocol' },
              { text: 'Completion Notifier', link: '/devices/completion/protocol' },
              { text: 'Completion Runtime', link: '/devices/completion/runtime' },
              { text: 'Notifier Protocol', link: '/devices/notifier/protocol' },
              { text: 'Peer Model Contract', link: '/devices/peer' },
            ]
          },
          {
            text: 'GPIO, Signals & Interrupts',
            collapsed: true,
            items: [
              { text: 'GPIO 12-Port Mux', link: '/devices/gpio/protocol' },
              { text: 'GPIO Pad Inputs', link: '/devices/gpio/input' },
              { text: 'GPIO Pin Muxing', link: '/devices/gpio/mux' },
              { text: 'EXIU External Interrupt Unit', link: '/devices/exiu/protocol' },
              { text: 'EXIU Readback & Ack', link: '/devices/exiu/readback' },
              { text: 'Signal IRQ Combiner', link: '/devices/signal/protocol' },
              { text: 'Signal Input Routing', link: '/devices/signal/input' },
              { text: 'Signal SLA Profile', link: '/devices/signal/sla' },
              { text: 'GIC Runtime Boundary', link: '/devices/gic/runtime' },
            ]
          },
          {
            text: 'Sensors, Readout & Imaging',
            collapsed: true,
            items: [
              { text: 'Sensor Capture Control', link: '/devices/sensor/protocol' },
              { text: 'Sensor Control Masks', link: '/devices/sensor/control' },
              { text: 'Sensor Unit Run & Stop', link: '/devices/sensor/runstop' },
              { text: 'Capture Parameter Packet', link: '/devices/sensor/packet' },
              { text: 'Endpoint & Global Parameters', link: '/devices/endpoints/protocol' },
              { text: 'Capture Service Banks', link: '/devices/capture/banks' },
              { text: 'Capture Configuration & Cursors', link: '/devices/capturegroup/protocol' },
              { text: 'Paired Capture Parameters', link: '/devices/captureparams/protocol' },
              { text: 'Finite Grayscale Stream & DMA', link: '/devices/capturework/protocol' },
              { text: 'Finite Statistics & Data Completion', link: '/devices/statistics/protocol' },
              { text: 'Readout Engine', link: '/devices/readout/protocol' },
              { text: 'MAS Readout Functional Mapping', link: '/devices/readout/mapping' },
              { text: 'Timing Engine Protocol', link: '/devices/timing/protocol' },
              { text: 'Timing Capture', link: '/devices/timing/capture' },
              { text: 'Timing Compare', link: '/devices/timing/compare' },
              { text: 'Timing Load & Divider', link: '/devices/timing/load' },
              { text: 'Timing Extra Features', link: '/devices/timing/extra' },
              { text: 'CComp GPIO Peer', link: '/devices/ccomp/protocol' },
              { text: 'Panel Configuration', link: '/devices/panel/protocol' },
              { text: 'External Sampler', link: '/devices/sampler/protocol' },
              { text: 'IPUD Protocol', link: '/devices/ipud/protocol' },
              { text: 'Processor Device', link: '/devices/processor/protocol' },
              { text: 'Processing Protocol', link: '/devices/processing/protocol' },
            ]
          },
          {
            text: 'Power, Audio, Storage & Peripherals',
            collapsed: true,
            items: [
              { text: 'EX Power Management', link: '/devices/expower/protocol' },
              { text: 'EX Hardware RNG', link: '/devices/exrng/protocol' },
              { text: 'Timestamp Counter', link: '/devices/timestamp/protocol' },
              { text: 'Timestamp Runtime', link: '/devices/timestamp/runtime' },
              { text: 'Counter Protocol', link: '/devices/counter/protocol' },
              { text: 'Audio Interface', link: '/devices/audio/protocol' },
              { text: 'Audio Format & Blocks', link: '/devices/audio/format' },
              { text: 'Audio Observation Profile', link: '/devices/audio/monitor' },
              { text: 'Touch Controller', link: '/devices/touch/protocol' },
              { text: 'Storage Protocol', link: '/devices/storage/protocol' },
              { text: 'Update Engine', link: '/devices/update/protocol' },
              { text: 'Lookup Table Engine', link: '/devices/lookup/protocol' },
              { text: 'Command Interface', link: '/devices/command/protocol' },
              { text: 'Save Protocol', link: '/devices/save/protocol' },
              { text: 'Input Protocol', link: '/devices/input/protocol' },
              { text: 'Acceleration I2C Device', link: '/devices/acceleration/protocol' },
            ]
          }
        ],
        '/runtime/': [
          {
            text: 'Battery & Authentication',
            collapsed: false,
            items: [
              { text: 'Battery Policy & BatCk', link: '/runtime/battery' },
              { text: 'Raw UART Battery Peer', link: '/runtime/batterypeer' },
              { text: 'EX Battery UART Command Inventory', link: '/runtime/batteryprotocol' },
              { text: 'Battery Report & Classification', link: '/runtime/batteryreport' },
              { text: 'EX Command02 Control', link: '/runtime/batterycontrol' },
              { text: 'Wire-Only Battery Tool', link: '/runtime/batterytool' },
              { text: 'Authentication Algorithm & Secrets', link: '/runtime/authentication' },
              { text: 'Battery Compatibility Warning', link: '/runtime/compatibility' },
              { text: 'Challenge Seed Recovery', link: '/runtime/seed' },
            ]
          },
          {
            text: 'Graphics Pipeline & Display',
            collapsed: false,
            items: [
              { text: 'Display Readiness Producers', link: '/runtime/display' },
              { text: 'GUI Chain Architecture', link: '/runtime/guichain' },
              { text: 'Image Information & Display Activation', link: '/runtime/imagerequest' },
              { text: 'EX power qualification and GUI admission', link: '/runtime/guiqualification' },
              { text: 'EX startup, status checks and host transport', link: '/runtime/exstartup' },
              { text: 'Application stages and GUI event producers', link: '/runtime/appstages' },
              { text: 'Display workers, kernel waits and request dispatch', link: '/runtime/displaywait' },
              { text: 'Application dispatch and EX key transport', link: '/runtime/guidispatch' },
              { text: 'Ordinary GUI Update Route', link: '/runtime/guiupdate' },
              { text: 'Inline A8 Image Modulation', link: '/runtime/image' },
              { text: 'Filled Glyph Paths', link: '/runtime/path' },
              { text: 'Mode 1 Composition Clear', link: '/runtime/fill' },
              { text: 'RGBA to YUV105 & YUVA107', link: '/runtime/conversion' },
              { text: 'Captured RGBA Stream Init', link: '/runtime/stream' },
              { text: 'GUI Readiness Gate', link: '/runtime/readiness' },
            ]
          },
          {
            text: 'Kernel & Memory Management',
            collapsed: false,
            items: [
              { text: 'Kernel Syscall Wrappers', link: '/runtime/kernel' },
              { text: 'Guest Object Allocations', link: '/runtime/allocations' },
              { text: 'Kernel Allocator Arena', link: '/runtime/allocator' },
              { text: 'First Bump Allocation', link: '/runtime/buffer' },
              { text: 'Kind40 Storage & Conversion', link: '/runtime/kind40' },
              { text: 'Interpolation Pool', link: '/runtime/interpolation' },
              { text: 'Grid Sample Pool', link: '/runtime/grid' },
              { text: 'Resource11/12 RAM Producers', link: '/runtime/rows' },
              { text: 'Guest PTE & DMA Contract', link: '/runtime/pte' },
              { text: 'Virtual Mode Stacks', link: '/runtime/stacks' },
              { text: 'MAS Scheduler Affinity', link: '/runtime/scheduler' },
              { text: 'Region Translation Table', link: '/runtime/regiontable' },
              { text: 'Selector-One Download Arena', link: '/runtime/selectoronedownload' },
            ]
          },
          {
            text: 'Policy, State & Handoff',
            collapsed: true,
            items: [
              { text: 'Worker0 Mode & Policy', link: '/runtime/policy' },
              { text: 'Worker Exit & Rear Panel', link: '/runtime/handoff' },
              { text: 'Assisted Guest Execution', link: '/runtime/assistance' },
              { text: 'Internal Transport & SPI4', link: '/runtime/transport' },
              { text: 'Private Address Class & Ring', link: '/runtime/address' },
              { text: 'Shared Sending Ring', link: '/runtime/ring' },
              { text: 'PTP Command Boundary (F091)', link: '/runtime/ptp' },
              { text: 'TL4 Coprocessor Startup', link: '/runtime/coprocessor' },
              { text: 'Private EX Composition', link: '/runtime/ex' },
              { text: 'EX Analog Input Contract', link: '/runtime/analog' },
              { text: 'EX System Reset', link: '/runtime/reset' },
              { text: 'MAS Readout State Ownership', link: '/runtime/readoutstate' },
              { text: 'Capture Completion & Retirement', link: '/runtime/capturedata' },
              { text: 'Software Storage Profile', link: '/runtime/storage' },
              { text: 'Descriptor17 Panel Resource', link: '/runtime/resource' },
              { text: 'ID34 Fields & Requirements', link: '/runtime/id34' },
            ]
          },
          {
            text: 'Wait Subsystem',
            collapsed: true,
            items: [
              { text: 'Wait ABI Specification', link: '/runtime/wait/abi' },
              { text: 'Wait Audit Ledger', link: '/runtime/wait/audit' },
              { text: 'EV3 Event Dispatcher', link: '/runtime/wait/ev3' },
              { text: 'Wait Observation Evidence', link: '/runtime/wait/observation' },
            ]
          },
          {
            text: '0x50200000 Address Investigation',
            collapsed: true,
            items: [
              { text: 'Arena & Object Evidence', link: '/runtime/arena502' },
            ]
          }
        ],
        '/inputs/': [
          {
            text: 'NOR Flash Descriptors & Inputs',
            items: [
              { text: 'NOR Descriptor Overview', link: '/inputs/nor/descriptor' },
              { text: 'NOR Descriptor Manifest', link: '/inputs/nor/manifest' },
              { text: 'Descriptor32 Module Loader', link: '/inputs/nor/module' },
              { text: 'Cold-Neutral Resource80 Input', link: '/inputs/nor/virtual' },
              { text: 'Functional Virtual Profile', link: '/inputs/nor/functional' },
              { text: 'Descriptor Candidates', link: '/inputs/nor/candidates' },
              { text: 'NOR Footer Structure', link: '/inputs/nor/footer' },
              { text: 'ID34 Descriptor Contract', link: '/inputs/nor/id34' },
              { text: 'NOR Device Profile', link: '/inputs/nor/profile' },
            ]
          },
          {
            text: 'Component Inputs & Hardware',
            items: [
              { text: 'EX Input Configuration', link: '/inputs/ex/config' },
              { text: 'EX Hardware RNG Input', link: '/inputs/ex/rng' },
              { text: 'NVRAM Slot Allocation', link: '/inputs/nv/slot' },
              { text: 'NVRAM Parameter Records', link: '/inputs/nv/records' },
              { text: 'Camera External Image Input', link: '/inputs/camera/image' },
              { text: 'Camera Physical Key Mappings', link: '/inputs/camera/keys' },
            ]
          }
        ],
        '/reference/': [
          {
            text: 'Historical Experiments',
            items: [
              { text: 'Assisted Boot Reference', link: '/reference/assistedboot' },
            ]
          }
        ],
        '/refmap/': [
          {
            text: 'Address Reference Map',
            items: [
              { text: 'Address Map Overview', link: '/refmap/README' },
              { text: 'Active Research Frontiers', link: '/refmap/FRONTIER' },
            ]
          }
        ],
        '/validation/': [
          {
            text: 'Verification, Fixtures & Receipts',
            items: [
              { text: 'Synthetic Deadlines & Budgets', link: '/validation/budgets' },
              { text: 'Firmware Startup Frontier', link: '/validation/frontier' },
              { text: 'Capture service and cold-neutral input', link: '/validation/capture' },
              { text: 'Graphics publication and timing qualification', link: '/validation/graphics' },
              { text: 'Battery and external-input startup profiles', link: '/validation/inputprofiles' },
              { text: 'EX report publication and host commands', link: '/validation/exreports' },
              { text: 'Display publication and kernel waits', link: '/validation/publication' },
              { text: 'Published Execution Evidence', link: '/validation/evidence' },
              { text: 'Native Integration Tests', link: '/validation/native' },
              { text: 'Peripheral Test Layer', link: '/validation/peripherals' },
              { text: 'Controller Test Layer', link: '/validation/controllers' },
              { text: 'Domain Integration Matrix', link: '/validation/domains' },
              { text: 'Provider Layer Verification', link: '/validation/providers' },
              { text: 'Controller & Input Layers', link: '/validation/layers' },
              { text: 'Reset, Timer & Analog Runtime', link: '/validation/runtime' },
              { text: 'Memory Pressure Tests', link: '/validation/pressure' },
            ]
          }
        ],
      },
      search: {
        provider: 'local'
      },
      outline: [2, 3],
      socialLinks: [
        { icon: 'github', link: 'https://github.com/mashiro01/nightglass-qemu' }
      ],
      footer: {
        message: 'Nikon Z7II C1.70 firmware diagnostic software model on QEMU.',
        copyright: 'Copyright © 2026 nightglass-qemu contributors'
      }
    }
})
