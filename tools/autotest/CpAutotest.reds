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
    if CpAutotestInWorld() {
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
    if CpAutotestInWorld() && !this.cpLoadIssued && ArraySize(saves) > 0 {
        this.cpLoadIssued = true;
        CpReport("{\"event\":\"LOAD_LAST_CHECKPOINT\",\"saves\":" + ToString(ArraySize(saves)) + "}");
        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
    }
    return result;
}

@addField(PlayerPuppet)
private let cpStepsStarted: Bool;

@addField(PlayerPuppet)
public let cpHud: wref<inkGameController>;

// A HUD controller of the player's world, to ask whether that world is the start screen's scene (pre-game).
@wrapMethod(PopupsManager)
protected cb func OnPlayerAttach(playerPuppet: ref<GameObject>) -> Bool {
    let result = wrappedMethod(playerPuppet);
    let player = playerPuppet as PlayerPuppet;
    if IsDefined(player) {
        player.cpHud = this;
    }
    return result;
}

// The loading screen has ended and the player is in the world. In-world scenarios then run their steps and report
// DONE. The start screen's scene puppet gets this too; its steps stop at step 0 (pre-game check).
@wrapMethod(PlayerPuppet)
protected cb func OnMakePlayerVisibleAfterSpawn(evt: ref<EndGracePeriodAfterSpawn>) -> Bool {
    let result = wrappedMethod(evt);
    CpReport("{\"event\":\"WORLD_READY\"}");
    if CpAutotestInWorld() && !this.cpStepsStarted {
        this.cpStepsStarted = true;
        // Let the HUD finish building before the first step.
        CpScheduleStep(this, 0, 5.0);
    }
    return result;
}

// Steps run one after another, a delay apart: CpRunWorldStep(player, n) does step n and returns the delay before step
// n + 1, or a negative value when the scenario is finished. A step that wants a screenshot reports CpShot(name) and
// returns a delay of a few seconds, which gives cp-run time to take it.
public class CpStepCallback extends DelayCallback {
    public let player: wref<PlayerPuppet>;
    public let step: Int32;

    public func Call() -> Void {
        if !IsDefined(this.player) {
            return;
        }
        if this.step == 0 {
            let hud = this.player.cpHud;
            if !IsDefined(hud) || hud.GetSystemRequestsHandler().IsPreGame() {
                return;
            }
        }
        let next = CpRunWorldStep(this.player, this.step);
        if next < 0.0 {
            CpReport("{\"event\":\"DONE\"}");
        } else {
            CpScheduleStep(this.player, this.step + 1, next);
        }
    }
}

public func CpScheduleStep(player: ref<PlayerPuppet>, step: Int32, delay: Float) -> Void {
    let callback = new CpStepCallback();
    callback.player = player;
    callback.step = step;
    GameInstance.GetDelaySystem(player.GetGame()).DelayCallback(callback, delay, false);
}

public func CpShot(name: String) -> Void {
    CpReport("{\"event\":\"SHOT\",\"name\":\"" + name + "\"}");
}
