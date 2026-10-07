// Unattended test driver. Installed into r6/scripts/cp_autotest/ by tools/cp-run for one run, then removed.
// CpAutotestScenario() is generated next to this file by cp-run.
// Every report is one JSON line appended to red4ext/logs/autotest.log by RED4ext's native RED4ext_TestReport;
// cp-run turns them into results.json.
module CpAutotest

public func CpReport(json: String) -> Void {
    RED4ext_TestReport(json);
}

public func CpCheck(name: String, pass: Bool, detail: String) -> Void {
    CpReport("{\"event\":\"CHECK\",\"name\":\"" + name + "\",\"pass\":" + ToString(pass) + ",\"detail\":\"" + detail + "\"}");
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    CpReport("{\"event\":\"MAIN_MENU\",\"scenario\":\"" + CpAutotestScenario() + "\"}");
    if Equals(CpAutotestScenario(), "load") {
        // The save list may not be cached yet while the menu initializes, so do not gate on HasLastCheckpoint().
        CpReport("{\"event\":\"LOAD_LAST_CHECKPOINT\"}");
        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
    } else {
        // Menu scenarios: TweakDB, resources and natives are all live at the main menu.
        CpRunMenuChecks(CpAutotestScenario());
        CpReport("{\"event\":\"DONE\"}");
        this.GetSystemRequestsHandler().ExitGame();
    }
    return result;
}

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
    let result = wrappedMethod();
    // Also fires for the main-menu scene's puppet; cp-run only counts events after LOAD_LAST_CHECKPOINT and
    // finishes on the first PLAYER_ATTACHED that follows it.
    CpReport("{\"event\":\"PLAYER_ATTACHED\"}");
    CpCheck("player_attached", true, "");
    // Scenario-specific checks (tweakxl, archivexl, modmenu) are added in Phase 3.
    return result;
}

// The game pauses when its window loses focus; cp-run closes the pause menu (Esc) before in-world key checks.
@wrapMethod(PauseMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    CpReport("{\"event\":\"PAUSE_MENU\"}");
    return result;
}

// The start screen ("press any key"). Pressing a key there loads the player profile and saves, so cp-run presses Space
// as soon as this is reported (CP_PRESS_START=1) instead of skipping the screen.
@wrapMethod(EngagementScreenGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    CpReport("{\"event\":\"START_SCREEN\"}");
    return result;
}
