# Phase 2.3 - ModMenu Handoff Document

**Status:** Tasks 1-2 Complete (RTTI Verified), Tasks 3-10 Ready to Start  
**Date:** February 8, 2026  
**Estimated Remaining:** ~20 hours

---

## Current Status

### ✅ Completed

**Task 1: Verify RTTI registration** - COMPLETE
- ModMenu loads successfully: `ModMenu (version: 0.1.0) has been loaded`
- No RTTI registration errors in logs
- Plugin initializes without crashes

**Task 2: Fix RTTI registration** - NOT NEEDED
- RTTI is working correctly on macOS
- All bridge functions register properly

### 🔄 Ready to Start

**Task 3: Design menu UI layout**  
**Task 4: Implement REDscript menu UI**  
**Task 5: Wire native → REDscript bridge**  
**Task 6: Implement settings persistence**  
**Task 7: Test with external plugins**  
**Task 8: Input system validation (F10)**  
**Task 9: Polish and edge cases**  
**Task 10: Package release**

---

## Project Structure

```
~/Development/cyberpunk/cp2077-modmenu/
├── src/
│   ├── main.cpp                 # Plugin entry + bridge functions
│   └── modmenu_backend.cpp      # Backend implementation
├── scripts/
│   └── Scripts/ModMenu/
│       ├── ModMenu.reds         # Module definition (minimal)
│       ├── InkHooks.reds        # Input handling + UI hooks
│       └── Events.reds          # Event definitions
├── build/
│   └── libModMenu.dylib         # Built binary
└── CMakeLists.txt
```

**Installation Location:**
```
~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/red4ext/plugins/ModMenu/
├── ModMenu.dylib
├── Scripts/
│   └── ModMenu/
│       ├── ModMenu.reds
│       ├── InkHooks.reds
│       └── Events.reds
└── config/
```

---

## Native Bridge API (Already Implemented)

The native→REDscript bridge is **fully implemented** in `main.cpp`. Available functions:

### State Management
- `ModMenu_IsOpen() -> Bool`
- `ModMenu_SetOpen(Bool) -> Bool`

### Logging
- `ModMenu_GetRed4extLogTail(Int32 maxBytes) -> String`
- `ModMenu_GetPluginLogTail(String modId, Int32 maxBytes) -> String`

### Mod Enumeration
- `ModMenu_GetModCount() -> Int32`
- `ModMenu_GetModId(Int32 index) -> String`
- `ModMenu_GetModName(Int32 index) -> String`

### Page Enumeration
- `ModMenu_GetPageCount(String modId) -> Int32`
- `ModMenu_GetPageId(String modId, Int32 index) -> String`
- `ModMenu_GetPageTitle(String modId, Int32 index) -> String`

### Entry Enumeration
- `ModMenu_GetEntryCount(String modId, String pageId) -> Int32`
- `ModMenu_GetEntryId(String modId, String pageId, Int32 index) -> String`
- `ModMenu_GetEntryTitle(String modId, String pageId, Int32 index) -> String`
- `ModMenu_GetEntryType(String modId, String pageId, Int32 index) -> Int32`

### Value Get/Set
- `ModMenu_GetToggleValue(String modId, String pageId, String entryId) -> Bool`
- `ModMenu_SetToggleValue(String modId, String pageId, String entryId, Bool value) -> Bool`
- `ModMenu_GetSliderValue(String modId, String pageId, String entryId) -> Float`
- `ModMenu_SetSliderValue(String modId, String pageId, String entryId, Float value) -> Bool`
- `ModMenu_PressButton(String modId, String pageId, String entryId) -> Bool`

### Diagnostics
- `ModMenu_ScanIncompatiblePlugins() -> String`
- `ModMenu_GenerateReportBundle(Int32 logMaxBytes) -> String`

---

## Task Breakdown

### Task 3: Design Menu UI Layout (1h)

**Current State:** Uses placeholder `streaming_spinner.inkwidget`

**Target Design:**
```
┌─────────────────────────────────────────┐
│ ModMenu v0.1.0                    [X]   │
├─────────────────────────────────────────┤
│ ┌────────────┐ ┌─────────────────────┐  │
│ │ Mod List   │ │ Settings Panel      │  │
│ │            │ │                     │  │
│ │ • ModMenu  │ │ [Toggle] Feature A  │  │
│ │ • TweakXL  │ │ [Slider] Value B    │  │
│ │ • ArchiveXL│ │ [Button] Action C   │  │
│ │            │ │                     │  │
│ └────────────┘ └─────────────────────┘  │
└─────────────────────────────────────────┘
```

**Components Needed:**
- `inkCanvas` - Root container
- `inkHorizontalPanel` - Main layout
- `inkVerticalPanel` - Mod list sidebar
- `inkScrollArea` - Settings panel
- `inkCheckbox` / custom toggle - Boolean settings
- `inkSlider` - Float values
- `inkButton` - Actions

### Task 4: Implement REDscript Menu UI (6h)

**Files to Modify:**
1. `InkHooks.reds` - Replace placeholder widget with real UI
2. Create new `ModMenuUI.reds` - UI controller class

**Implementation Steps:**

1. **Create UI Controller Class:**
```redscript
public class ModMenuUIController extends inkGameController {
    private let rootCanvas: wref<inkCanvas>;
    private let modListPanel: wref<inkVerticalPanel>;
    private let settingsPanel: wref<inkScrollArea>;
    
    protected cb func OnInitialize() -> Void {
        this.CreateUI();
    }
    
    private func CreateUI() -> Void {
        // Create root canvas
        // Create mod list sidebar
        // Create settings panel
        // Populate with mods
    }
}
```

2. **Replace Placeholder in InkHooks.reds:**
```redscript
// OLD (line 97-101):
this.modmenuWidget = this.SpawnFromExternal(
    root,
    r"base\\gameplay\\gui\\widgets\\streaming_spinner\\streaming_spinner.inkwidget",
    n"Root"
);

// NEW:
// Spawn custom ModMenu UI widget
// Or create programmatically using ink widgets
```

3. **Add UI Event Handlers:**
- Mod selection changed → Update settings panel
- Toggle changed → Call `ModMenu_SetToggleValue`
- Slider changed → Call `ModMenu_SetSliderValue`
- Button pressed → Call `ModMenu_PressButton`

### Task 5: Wire Native → REDscript Bridge (3h)

**Already Done:** Bridge functions registered in `main.cpp`

**Needed:** Call these functions from REDscript UI

**Example:**
```redscript
// Get mod count
let modCount = ModMenu_GetModCount();

// Populate mod list
for i in 0..modCount {
    let modId = ModMenu_GetModId(i);
    let modName = ModMenu_GetModName(i);
    // Add to UI list
}

// Handle toggle change
private func OnToggleChanged(modId: String, pageId: String, entryId: String, value: Bool) -> Void {
    ModMenu_SetToggleValue(modId, pageId, entryId, value);
}
```

### Task 6: Implement Settings Persistence (2h)

**Current State:** Backend has persistence API

**Implementation:**
- Settings automatically saved by native backend
- Verify JSON persistence works on macOS
- Test settings survive game restart

### Task 7: Test with External Plugins (2h)

**Test Setup:**
1. Create test plugin that registers with ModMenu
2. Verify it appears in mod list
3. Test settings get/set
4. Test callbacks fire

**Test Plugin Code:**
```cpp
// Test plugin Main()
const auto* api = ModMenu_GetApi();
if (api && api->RegisterMod) {
    ModMenuModInfo mod = {
        .modId = {.ptr = "TestMod"},
        .name = {.ptr = "Test Mod"},
        .author = {.ptr = "Test Author"},
        .version = {.ptr = "1.0.0"},
    };
    api->RegisterMod(&mod);
    
    ModMenuPageInfo page = {.pageId = {.ptr = "settings"}, .title = {.ptr = "Settings"}};
    api->RegisterPage("TestMod", &page);
    
    ModMenuToggleInfo toggle = {
        .entryId = {.ptr = "enable_feature"},
        .title = {.ptr = "Enable Feature"},
        .defaultValue = true,
        .onChanged = &OnToggleChanged,
    };
    api->RegisterToggle("TestMod", "settings", &toggle);
}
```

### Task 8: Input System Validation - F10 (1h)

**Current State:** Input handling implemented in `InkHooks.reds`

**Verify:**
1. Input mapping exists in `r6/input/mods.xml`
2. F10 key triggers `modmenu_toggle` action
3. Menu opens/closes on F10 press

**Input Config:**
```xml
<!-- r6/input/mods.xml -->
<bindings>
  <mapping name="modmenu_toggle" type="Button">
    <button id="IK_F10"/>
  </mapping>
</bindings>
```

### Task 9: Polish and Edge Cases (2h)

**Handle:**
- Empty mod list (show "No mods registered")
- Many mods (scrollbar in mod list)
- Long mod names (truncate with ellipsis)
- Settings validation (clamp slider values)
- Menu open during cutscenes (auto-close?)

### Task 10: Package Release (1h)

**Create:** `ModMenu-v0.2.0-macos-arm64.zip`

**Contents:**
```
ModMenu/
├── ModMenu.dylib
├── Scripts/
│   └── ModMenu/
│       ├── ModMenu.reds
│       ├── InkHooks.reds
│       ├── ModMenuUI.reds (new)
│       └── Events.reds
└── config/
    └── input.json (if needed)
```

---

## Key Implementation Notes

### UI Widget Creation

Cyberpunk 2077 uses ink (UI) widgets. Key types:
- `inkCanvas` - Container with absolute positioning
- `inkHorizontalPanel` - Horizontal layout
- `inkVerticalPanel` - Vertical layout
- `inkScrollArea` - Scrollable container
- `inkText` - Text display
- `inkImage` - Image display

### Spawning Widgets

```redscript
// From external widget file
let widget = this.SpawnFromExternal(parent, r"path\to\widget.inkwidget", n"Root");

// From local library
let widget = this.SpawnFromLocal(parent, n"widgetName");
```

### Event System

Already implemented in `Events.reds`:
```redscript
public class ModMenuToggleEvent extends Event {
    public let open: Bool;
}
```

---

## Testing Checklist

- [ ] Menu opens on F10 press
- [ ] Menu closes on second F10 press
- [ ] Mod list populates with registered mods
- [ ] Selecting mod shows its settings pages
- [ ] Toggles update backend state
- [ ] Sliders update backend state
- [ ] Buttons trigger callbacks
- [ ] Settings persist across game restarts
- [ ] Menu works during gameplay (not just main menu)
- [ ] No crashes when opening/closing rapidly

---

## Quick Commands

```bash
# Build ModMenu
cd ~/Development/cyberpunk/cp2077-modmenu/build
cmake .. && make -j$(sysctl -n hw.ncpu)

# Install to game
GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
cp ~/Development/cyberpunk/cp2077-modmenu/build/libModMenu.dylib "$GAME_DIR/red4ext/plugins/ModMenu/ModMenu.dylib"
codesign -f -s - "$GAME_DIR/red4ext/plugins/ModMenu/ModMenu.dylib"

# Launch game
cd "$GAME_DIR"
./launch_red4ext.sh

# Check logs
cat "$GAME_DIR/red4ext/logs/red4ext.log" | grep -i modmenu
```

---

## Success Criteria

ModMenu Phase 2.3 is complete when:

1. ✅ F10 opens/closes the mod menu
2. ✅ Menu shows list of registered mods
3. ✅ Selecting a mod shows its settings
4. ✅ Toggles, sliders, and buttons work
5. ✅ Settings persist to disk
6. ✅ External plugins can register settings
7. ✅ UI is polished and handles edge cases

---

## Next Phase

After ModMenu completion, proceed to:
- **Phase 3.1:** MetalFX Denoiser (36h)
- **Phase 4.x:** CyberMod Studio modules

---

**Current Blockers:** None - ready to implement UI

**Key Files:**
- Native: `~/Development/cyberpunk/cp2077-modmenu/src/main.cpp`
- UI Hook: `~/Development/cyberpunk/cp2077-modmenu/scripts/Scripts/ModMenu/InkHooks.reds`
- Events: `~/Development/cyberpunk/cp2077-modmenu/scripts/Scripts/ModMenu/Events.reds`

**End of Handoff Document**
