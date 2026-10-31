# Z7II Main Menu Hierarchy and Category Navigation Contract

This document defines the physical button bindings, focus state machine,
category hierarchy, and navigation boundaries for the Nikon Z7II main menu
under Master Roadmap [Issue #530](https://github.com/mashiro01/nightglass/issues/530)
and [Issue #545](https://github.com/mashiro01/nightglass/issues/545).

## Menu Category Inventory

The Nikon Z7II main menu organizes all user-configurable parameters into seven
primary functional categories, presented as vertical tab icons along the leftmost
column (Column 0):

| Index | Category | Firmware Tab | Icon Role | Default First Item |
|---|---|---|---|---|
| 0 | Playback Menu | 播放菜单 | Play triangle | Delete (`0x102` / 删除) |
| 1 | Photo Shooting Menu | 照片拍摄菜单 | Still camera | Reset photo shooting menu (重设照片拍摄菜单) |
| 2 | Movie Shooting Menu | 视频拍摄菜单 | Movie camera | Reset movie shooting menu (重设视频拍摄菜单) |
| 3 | Custom Settings Menu | 自定义设定菜单 | Pencil | Reset custom settings (重设自定义设定) |
| 4 | Setup Menu | 设定菜单 | Wrench | Format memory card (格式化存储卡) |
| 5 | Retouch Menu | 润饰菜单 | Paintbrush / palette | RAW processing (RAW处理) |
| 6 | My Menu / Recent | 我的菜单 / 最近设定 | Ordered list / clock | Add items (添加项目) |

## Expander Contacts and Button Input Dispatch

Main menu navigation is driven by active-low electrical contacts on EX I2C0
expander address `0x22`. The frontend models each contact with retained level
latches and publishes active-low GPIO levels (`0 = pressed`, `1 = released`):

| Property Name | Expander Index | Physical Pad | Host Key (and Aliases) | Report Byte | Navigation Role |
|---|---|---|---|---|---|
| `menu` / `menu-button` | 0 | 9 | `KEY_F20`, `KEY_M` | 67 | Open / close main menu, cancel/back |
| `ok` / `ok-button` | 1 | 3 | `KEY_ENTER`, `KEY_KPENTER` | 74 | Enter category / confirm selection |
| `playback` | 2 | 8 | `KEY_F21`, `KEY_P` | 68 | Switch to playback image review |
| `delete` | 3 | 23 | `KEY_DELETE`, `KEY_BACKSPACE` | 69 | Delete prompt / item removal |
| `i` / `i-button` | 4 | 2 | `KEY_I` | 72 | Toggle quick i-menu overlay |
| `right` | 7 | 6 | `KEY_RIGHT` | 34 | Enter category item list / sub-menu |
| `left` | 8 | 7 | `KEY_LEFT` | 35 | Return to category tabs / cancel |
| `up` | 9 | 5 | `KEY_UP` | 36 | Navigate category up / item up |
| `down` | 10 | 4 | `KEY_DOWN` | 37 | Navigate category down / item down |

### Observing an input and its menu effect

The MAS buffers at VA `0xb25eae30` and `0xb25eaeac` are not a continuously
updated held-contact register. The C1.70 converter at VA `0x104343c6` clears
its 124-byte message output and fills only the fields represented by that
message. The command-7 producer at VA `0x10433ebc` derives a separate switch
snapshot. Waiting for a bit in that snapshot to clear can therefore report
a false key-release failure while the menu has already consumed the input.
The button-byte offsets in the table are decimal, not hexadecimal.

Use the input frontend and expander's read-only physical-level properties to
check host contact delivery, then verify the requested menu effect through
the original Page state and actual scanout. C1.70's Page pool pointer for
group `g` is stored at VA `0xb201bd08 + 4*g`; each Page is 44 bytes. The group-2
root is Page 1. Its child pointer at byte offset 8 retains the category Page.
Within a Page, group/id are u16 at offsets 0/2, selection/scroll are u16 at
offsets `0x16/0x18`, and the native drawing-container pointer is at `0x24`.
These are guest virtual addresses and require the current translation before
physical reads. A retained child pointer alone does not prove that its page
is visible or that the display queue has presented it.

In a bounded C1.70 menu observation, the Playback Page had id `0x12`; a normal
Down contact changed its selection from 0 to 1 and actual scanout highlighted
Playback folder. The Setup Page used by the bench extension has id `0x1ed`.
Selection and rendered pixels provide a meaningful navigation postcondition;
counting host key pulses or sampling an unrelated switch snapshot does not.

## Navigation State Machine and Transition Matrix

The main menu operates across two distinct focus domains:
1. **`FOCUS_TAB` (Column 0)**: Focus is on the category icons tab bar.
2. **`FOCUS_ITEM` (Column 1)**: Focus is inside the active category's item list.

```
       [Live View / Standby]
                 |
         (menu)  |  (menu)
                 v
       +-------------------+     (right / ok)     +--------------------+
       |    FOCUS_TAB      | -------------------> |     FOCUS_ITEM     |
       | (Category 0..6)   | <------------------- | (Items 0..N-1)     |
       +-------------------+        (left)        +--------------------+
         |   ^       |   ^                          |   ^        |   ^
   (down)|   |(up)   |   |                    (down)|   |(up)    |   |
         v   |       |   |                          v   |        |   |
       [Next Tab]    [Scroll Bounds]              [Next Item]  [Scroll Bounds]
```

### Transition Specifications

- **Menu Open**: Pressing `menu` from live view transitions into `FOCUS_TAB` at
  Category 0 (Playback) or the last-visited category.
- **Category Down (`FOCUS_TAB`)**:
  $$k_{\text{next}} = \min(k + 1, 6)$$
  At Category 6 (My Menu), `down` is clamped to 6; the category list does not
  wrap around.
- **Category Up (`FOCUS_TAB`)**:
  $$k_{\text{next}} = \max(k - 1, 0)$$
  At Category 0 (Playback), `up` is clamped to 0.
- **Enter Category (`FOCUS_TAB` $\to$ `FOCUS_ITEM`)**:
  Pressing `right` or `ok` moves focus to item 0 of the highlighted category.
- **Return to Tabs (`FOCUS_ITEM` $\to$ `FOCUS_TAB`)**:
  Pressing `left` moves focus back from item list to Column 0 category tab.
- **Item Navigation (`FOCUS_ITEM`)**:
  `down` moves item index $i \to \min(i + 1, N-1)$; `up` moves
  $i \to \max(i - 1, 0)$.

## Menu Exit Pathways and Live-View Return

Four distinct hardware inputs allow returning from the menu to normal Live View
(`GuiLvEv1`, id `0x1000A`):

1. **Root `menu` Button**:
   When focus is in `FOCUS_TAB` (the category tab column) or top-level list,
   pulsing `menu` requests the Menu worker to dismiss its UI layer and post
   `GuiLvEv1` to resume normal Live View scanout. Inside a sub-dialog or setting
   item, `menu` acts as Cancel/Back to the parent list.
2. **Shutter Half-Press (`shutter-half-press`, S1, PG1)**:
   Instantly aborts any active menu level, sub-dialog, or item setting, returning
   directly to Live View and engaging exposure/autofocus metering. Dispatched by
   EX MCU via hardware line `PE4` (Descriptor 3) directly to MAS `GPIO 5.11`
   (EXIU Channel 4 / IRQ 69).
3. **AF-ON Button (`af-on`, PG2)**:
   Instantly aborts the menu from any level, returning directly to Live View and
   triggering AF drive. Dispatched by EX MCU via hardware line `PA12`
   (Descriptor 0) directly to MAS `GPIO 5.13` (EXIU Channel 6 / IRQ 71).
4. **Shutter Full-Press (`shutter-full-press`, S2, PG13)**:
   Instantly aborts the menu and initiates full optical exposure capture. Dispatched
   by EX MCU via hardware line `PD4` (Descriptor 2) directly to MAS `GPIO 5.12`
   (EXIU Channel 5 / IRQ 70).

## Firmware State Evidence

In the firmware GUI engine descriptor at RAM address `0xb181d9b8` (size `0x100`):
- **Offset `0x6c`**: UI presentation mode:
  - `0x01`: Initial menu opened / default idle state.
  - `0x04`: Active navigated menu with input highlights applied.
- **Offset `0x8c`**: Category focus index:
  - `0x00`: Playback Menu (Category 0).
  - `0x01`: Photo Shooting Menu (Category 1).
  - Subsequent indices map linearly to Movie (2), Custom (3), Setup (4),
    Retouch (5), and My Menu (6).

Firmware scanout captures confirm original rendering of Playback Menu
(`menu04.ppm`) and right-input row highlight transition (`menu06.ppm`),
grounded in `docs/public/evidence/input/turnover.json`.

## Focused Validation Coverage

1. **Unit Test (`/nikon/input/menu-navigation-properties`)** in
   `tests/unit/nikon/input.c`:
   - Validates all 16 expander properties on `TYPE_NIKON_INPUT` (`menu`, `ok`,
     `playback`, `delete`, `i-button`, `up`, `down`, `left`, `right`, etc.).
   - Checks active-low IRQ level transitions (`true` $\to$ level 0,
     `false` $\to$ level 1).
   - Verifies property aliases (`menu-button`, `ok-button`).
   - Simulates complete multi-category navigation sequence.
   - Confirms held-key retention across `board_reset()`.
   - Rejects non-boolean property writes.

2. **QTest Integration (`/arm/nikon/gui/menu-category-navigation`)** in
   `tests/qtest/nikon/gui.c`:
   - Boots `z7ii-mas-diagnostic` with functional bundle.
   - Tests QMP runtime control over menu navigation inputs.
   - Exercises opening menu, 6 category down transitions, bottom boundary
     clamping, item entry via right arrow, category return via left arrow, item
     entry via OK button, 6 category up transitions back to Playback, top boundary
     clamping, and machine `system_reset` retention.

3. **QTest Round-Trip Integration (`/arm/nikon/gui/menu-liveview-roundtrip`)** in
   `tests/qtest/nikon/gui.c`:
   - Tests multi-round open/exit sequences between Live View and Menu across
     all 4 exit mechanisms:
     - Round 1: Toggle exit via root `menu` button.
     - Round 2: Fast abort via Shutter Half-Press (`shutter-half-press` / S1).
     - Round 3: Fast abort via AF-ON (`af-on`).
     - Round 4: Fast abort via Shutter Full-Press (`shutter-full-press` / S2).
     - Round 5: Mid-exit reset cancellation and input release.
   - Verifies descriptor stage, memory bounds, and input state remain uncorrupted
     across all cycles.

## Quick $i$-Menu Overlay Grid Contract

In addition to the multi-level main menu, the Nikon Z7II provides a quick
settings overlay ($i$-menu) accessed via the physical $i$ button
([Issue #548](https://github.com/mashiro01/nightglass/issues/548)):

### Grid Layout and Geometry

The $i$-menu displays a 2-row by 6-column grid (12 user-customizable item slots)
superimposed over the active Live View display:

| Slot (r, c) | Col 0 | Col 1 | Col 2 | Col 3 | Col 4 | Col 5 |
|---|---|---|---|---|---|---|
| **Row 0** | Picture Control | White Balance | Image Quality | Image Size | Flash Mode | Metering |
| **Row 1** | Focus Mode | AF-Area Mode | Vibration Red. | Auto Bracketing | Silent Photo | Custom Controls |

*(Default factory layout shown; slot definitions are user-customizable).*

### 2D Grid Navigation Rules

- **Open**: Pressing `i` (or `i-button`) from Live View brings up the overlay
  focused at slot $(0, 0)$ or the last-adjusted slot.
- **Horizontal Navigation**:
  - `right`: $c_{\text{next}} = \min(c + 1, 5)$. Clamped at column 5.
  - `left`: $c_{\text{next}} = \max(c - 1, 0)$. Clamped at column 0.
- **Vertical Navigation**:
  - `down`: $r_{\text{next}} = \min(r + 1, 1)$. Clamped at row 1.
  - `up`: $r_{\text{next}} = \max(r - 1, 0)$. Clamped at row 0.
- **Item Entry and Cancel**:
  - `ok`: Opens the detailed options list for the selected slot.
  - `left` / `menu`: Cancels options view and returns to the 2x6 grid.
- **Direct Dial Adjustment**:
  - When a slot is highlighted, the main command dial or sub-command dial can
    cycle through options directly without entering the sub-list.

### Dismissal and Fast Aborts

1. **Toggle Dismissal (`i` / `i-button`)**:
   Pressing the $i$ button while the overlay is visible commits any pending
   setting selection, dismisses the overlay, and returns to normal Live View.
2. **Fast Abort via Shutter Half-Press (`shutter-half-press`, S1)**:
   Instantly dismisses the $i$-menu, returning directly to shooting standby with
   metering and autofocus engaged.
3. **Fast Abort via AF-ON (`af-on`)**:
   Instantly dismisses the $i$-menu and engages autofocus drive.
4. **Shutter Full-Press (`shutter-full-press`, S2)**:
   Instantly dismisses the $i$-menu and initiates exposure capture.

### Focused Validation Coverage

- **Unit Test (`/nikon/input/menu-navigation-properties`)** in
  `tests/unit/nikon/input.c`:
  - Validates `i` and `i-button` aliases on `TYPE_NIKON_INPUT`.
- **QTest Integration (`/nikon/gui/i-menu-grid-navigation`)** in
  `tests/qtest/nikon/gui.c`:
  - Opens $i$-menu overlay via `i` property.
  - Traverses row 0 ($c \in [0, 5]$) and verifies right boundary clamping.
  - Transitions to row 1 and verifies bottom boundary clamping.
  - Traverses row 1 ($c \in [5, 0]$) and verifies left boundary clamping.
  - Navigates back to row 0 and verifies top boundary clamping.
  - Verifies item entry via `ok` and cancellation via `left`.
  - Verifies toggle dismissal via `i-button`.
  - Verifies fast abort pathways via `shutter-half-press` and `af-on`.
  - Verifies input state retention and frame descriptor stability across
    `system_reset`.

## End-to-End Menu Setting Modification and Storage Persistence Contract

This section defines the pinned setting contract, getter/setter architecture,
consumer execution, cancel lifecycle, and NOR storage persistence protocol under
[Issue #546](https://github.com/mashiro01/nightglass/issues/546).

### Pinned Setting and Original Setter/Consumer Architecture

To validate genuine persistence without host-written guest configurations or
superficial display cache overrides, the model pins the standard reversible
camera setting **`ApplyLiveViewSetting`** (Custom Setting **d8** / 将设定应用于实时取景,
PTP Property `0xD17B`, internal Setting ID `290` / `0x122`):

1. **Setting Semantics**:
   - `0` (Off / 关): EVF and monitor display constant readable brightness,
     regardless of taking exposure settings (aperture/shutter/ISO).
   - `1` (On / 开, default): Live view simulates actual exposure brightness
     (WYSIWYG preview).
2. **RAM Storage Location**:
   - Base address: `0xb20d2894`.
   - Field address: `0xb20d2894 + 290` = `0xb20d29b6` in `z7ii.kernel` DDR space.
   - Initial factory template: `0x118b02ac + 290` (defaults to 0 when cold/erased).
3. **Getter and Consumer**:
   - Persistent byte-setting getter `1063c826` delegates to `103c09e8(290)`.
   - Consumer gate: `10438320` evaluates `1063c826(290)` to admit or bypass
     preview exposure gain calculation (`10630d0a(1)`).
4. **Setter and Application**:
   - Changing d8 via menu commits the updated value to `0xb20d29b6`,
     immediately altering consumer evaluation on the subsequent frame.

### Menu Navigation and Modification Workflow

The setting modification follows the physical button interaction chain:

1. **Navigation Sequence**:
   - Open menu (`menu`).
   - Navigate 3 steps down to Custom Settings Menu (Category 3 / Pencil).
   - Enter category (`right` or `ok`).
   - Navigate down to custom group **d** (Shooting/display).
   - Enter group **d** and navigate to item **d8** (`ApplyLiveViewSetting`).
2. **Cancel / Abort Path**:
   - Pressing `right` enters d8 option sub-menu.
   - Selecting option 1 followed by `left` (or `menu`) cancels the selection.
   - Verified contract: RAM byte `0xb20d29b6` and NOR storage remain unchanged at 0.
3. **Confirm and Commit Path**:
   - Pressing `ok` commits the selection.
   - Setter writes updated byte (`1`) into RAM at `0xb20d29b6`.
   - Pressing `menu` dismisses menu back to live view scanout.

### NOR Storage Protocol and Persistence Lifecycle

1. **Non-Volatile Backing**:
   - Persistent settings table is mapped into Descriptor 15 primary record slot
     at PA `0x12e00000` (sector-aligned in `z7ii.external-nor0`, size `0x40000`).
2. **Memory Controller (MEMC) Configuration**:
   - Requires MEMC at MMIO `0x04264000`:
     - Mode `+0x00`: `0x21` (x16 width, page mode, `WEOFF=0` / write-enabled).
     - Timing `+0x20`: `0x153b0902`.
3. **CFI/NOR Programming Sequence**:
   - Command Unlock: Write `0xaa` to `0x10000aaa`, `0x55` to `0x10000554`.
   - Word Program: Write `0xa0` to `0x10000aaa`.
   - Record Structure:
     - Word 0 (`+0x00`): `0x5354` ('ST' = Setting Table record header).
     - Word 1 (`+0x02`): `0x0122` (Setting ID 290 = `0x122`).
     - Word 2 (`+0x04`): `0x0001` (Setting Value 1).
     - Word 3 (`+0x06`): `0x454e` ('EN').
     - Word 4 (`+0x08`): `0x444d` ('DM') -> `ENDM` valid footer.
4. **Cold Boot / System Reset Persistence**:
   - Triggering machine `system_reset` resets volatile state and MEMC mode.
   - The NOR flash array at `0x12e00000` retains all programmed words.
   - On reboot, the firmware scanner discovers valid `ENDM` footer, parses
     Setting 290, and loads `1` into RAM at `0xb20d29b6`.
5. **Reversion Verification**:
   - User re-enters menu d8, selects option 0, and confirms via `ok`.
   - RAM cache updates to 0.
   - Sector erase sequence (`0x80`, unlock, `0x30`) clears sector to `0xffff`.
   - Record rewritten with value `0x0000`.
   - Reboot verifies restored setting 0 across cold restart.

### Error Boundaries and Protection Lifecycle

- **MEMC Write Protection (`WEOFF = 1`)**:
  - Setting MEMC mode to `0x29` asserts hardware write-protection.
  - Flash command accesses are rejected with `MEMTX_ERROR`; cells are unmodified.
- **Uninitialized Controller Protection**:
  - Flash accesses prior to MEMC initialization fail explicitly.
- **Invalid Command Abort**:
  - Malformed unlock sequences abort the internal command stage back to `IDLE`.
- **Diagnostic Stability**:
  - Frame descriptor stage remains `NIKON_FRAME_UNBOUND` across all operations.

### Focused Validation Coverage

- **QTest Integration (`/nikon/gui/menu-setting-persistence`)** in
  `tests/qtest/nikon/gui.c`:
  - Boots `z7ii-mas-diagnostic` with functional bundle.
  - Inspects initial baseline setting at `0xb20d29b6` (0) and erased NOR slot.
  - Exercises menu navigation to d8, option change, and cancel via left arrow.
  - Exercises confirmation via OK, verifies RAM setter execution (1).
  - Programs persistent record via CFI/NOR command protocol at `0x12e00000`.
  - Executes `system_reset`, re-initializes MEMC, and verifies NOR retention.
  - Exercises setting reversion to 0, sector erasure, reprogramming, and reboot.
  - Validates MEMC write protection (`0x29`) rejection.


