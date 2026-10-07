module CpAutotest

// ModMenu: its natives are registered from the RTTI callback and callable from script.
public func CpRunMenuChecks(scenario: String) -> Void {
    CpCheck("modmenu_closed_initially", !ModMenu_IsOpen(), "");
    CpCheck("modmenu_set_open", ModMenu_SetOpen(true) && ModMenu_IsOpen(), "");
    ModMenu_SetOpen(false);
    let count = ModMenu_GetModCount();
    CpCheck("modmenu_mod_count", count >= 1, ToString(count));
    CpCheck("modmenu_self_registered", Equals(ModMenu_GetModId(0), "ModMenu"), ModMenu_GetModId(0));
    CpCheck("modmenu_page", Equals(ModMenu_GetPageId("ModMenu", 0), "main"), ModMenu_GetPageId("ModMenu", 0));
}
