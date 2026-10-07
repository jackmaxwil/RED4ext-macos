// Unattended test driver. Installed into r6/scripts/cp_autotest/ by tools/cp-run for one run, then removed.
// CpAutotestScenario() is generated next to this file by cp-run.
// Every report is one stdout line "CPAUTOTEST <json>", which cp-run turns into results.json.
module CpAutotest

public func CpReport(json: String) -> Void {
    LogChannel(n"DEBUG", "CPAUTOTEST " + json);
}

public func CpCheck(name: String, pass: Bool, detail: String) -> Void {
    CpReport("{\"event\":\"CHECK\",\"name\":\"" + name + "\",\"pass\":" + ToString(pass) + ",\"detail\":\"" + detail + "\"}");
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    CpReport("{\"event\":\"MAIN_MENU\",\"scenario\":\"" + CpAutotestScenario() + "\"}");
    let handler = this.GetSystemRequestsHandler();
    if handler.HasLastCheckpoint() {
        CpReport("{\"event\":\"LOAD_LAST_CHECKPOINT\"}");
        handler.LoadLastCheckpoint(false);
    } else {
        CpCheck("has_checkpoint", false, "no save to load");
        CpReport("{\"event\":\"DONE\"}");
        handler.ExitGame();
    }
    return result;
}

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
    let result = wrappedMethod();
    CpReport("{\"event\":\"PLAYER_ATTACHED\"}");
    CpCheck("player_attached", true, "");
    // Scenario-specific checks (tweakxl, archivexl, modmenu) are added in Phase 3.
    CpReport("{\"event\":\"DONE\"}");
    return result;
}
