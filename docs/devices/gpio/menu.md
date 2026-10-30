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

| Property Name | Expander Index | Physical Pad | Host Key | Report Byte | Navigation Role |
|---|---|---|---|---|---|
| `menu` / `menu-button` | 0 | 9 | KEY_F20 | 67 | Open / close main menu, cancel/back |
| `ok` / `ok-button` | 1 | 3 | KEY_ENTER | 74 | Enter category / confirm selection |
| `playback` | 2 | 8 | KEY_F21 | 68 | Switch to playback image review |
| `delete` | 3 | 23 | KEY_DELETE | 69 | Delete prompt / item removal |
| `i-button` | 4 | 2 | KEY_I | 72 | Toggle quick i-menu overlay |
| `right` | 7 | 6 | KEY_RIGHT | 34 | Enter category item list / sub-menu |
| `left` | 8 | 7 | KEY_LEFT | 35 | Return to category tabs / cancel |
| `up` | 9 | 5 | KEY_UP | 36 | Navigate category up / item up |
| `down` | 10 | 4 | KEY_DOWN | 37 | Navigate category down / item down |

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
   directly to Live View and engaging exposure/autofocus metering.
3. **AF-ON Button (`af-on`, PG2)**:
   Instantly aborts the menu from any level, returning directly to Live View and
   triggering AF drive.
4. **Shutter Full-Press (`shutter-full-press`, S2, PG13)**:
   Instantly aborts the menu and initiates full optical exposure capture.

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
