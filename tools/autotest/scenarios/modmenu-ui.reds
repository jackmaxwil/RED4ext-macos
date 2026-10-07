module CpAutotest

// ModMenu's in-world overlay, driven from script: open it, select a mod, flip its demo toggle, close it with its key.
// Pointer hit-testing itself is not covered: these call the same methods the click callbacks call.
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let overlay = player.modmenuOverlay;
    switch step {
        case 0:
            CpCheck("overlay_attached", IsDefined(overlay), "");
            if !IsDefined(overlay) {
                return -1.0;
            }
            CpShot("closed-before");
            return 3.0;
        case 1:
            overlay.ModMenu_Toggle(true);
            CpCheck("open", ModMenu_IsOpen() && player.modmenuOpen, "");
            CpCheck("modal_on", overlay.ModMenu_IsModal(), "");
            CpShot("open");
            return 3.0;
        case 2:
            overlay.ModMenu_SelectMod(0);
            CpShot("mod-selected");
            return 3.0;
        case 3:
            let before = ModMenu_GetToggleValue("ModMenu", "main", "demo_toggle");
            overlay.ModMenu_FlipToggle("demo_toggle");
            CpCheck("toggle_flipped", NotEquals(ModMenu_GetToggleValue("ModMenu", "main", "demo_toggle"), before), "");
            CpShot("toggled");
            return 3.0;
        case 4:
            // Restore the user's value, then close with the toggle key (50: `), as a player would.
            overlay.ModMenu_FlipToggle("demo_toggle");
            CpReport("{\"event\":\"PRESS\",\"key\":50}");
            return 4.0;
        case 5:
            CpCheck("closed_by_key", !ModMenu_IsOpen() && !player.modmenuOpen, "");
            CpCheck("modal_off", !overlay.ModMenu_IsModal(), "");
            CpShot("closed-after");
            return 3.0;
    }
    return -1.0;
}
