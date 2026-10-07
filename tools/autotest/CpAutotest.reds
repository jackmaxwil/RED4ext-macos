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
        // Loading waits for OnSavesForLoadReady: right after the start screen the save list is not known yet, and a
        // load requested now does nothing.
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

@addField(SingleplayerMenuGameController)
private let cpLoadIssued: Bool;

// The menu has the save list: load the most recent save (the main menu's Continue), once.
@wrapMethod(SingleplayerMenuGameController)
protected cb func OnSavesForLoadReady(saves: array<String>) -> Bool {
    let result = wrappedMethod(saves);
    if Equals(CpAutotestScenario(), "load") && !this.cpLoadIssued && ArraySize(saves) > 0 {
        this.cpLoadIssued = true;
        CpReport("{\"event\":\"LOAD_LAST_CHECKPOINT\",\"saves\":" + ToString(ArraySize(saves)) + "}");
        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
    }
    return result;
}

// The loading screen has ended and the player is in the world (the main-menu scene's puppet never gets this).
@wrapMethod(PlayerPuppet)
protected cb func OnMakePlayerVisibleAfterSpawn(evt: ref<EndGracePeriodAfterSpawn>) -> Bool {
    let result = wrappedMethod(evt);
    CpReport("{\"event\":\"WORLD_READY\"}");
    return result;
}
