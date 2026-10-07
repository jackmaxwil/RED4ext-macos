module CpAutotest

// Long-session stability: 60 one-minute steps in the world with all plugins loaded. Each step does one thing that
// exercises the game and the plugins: skip 3 game hours (weather, time of day, NPC population), open or close
// ModMenu; every 10 steps also reload TweakXL's tweaks and take a screenshot. Run with
//   CP_PLUGINS=ModMenu,TweakXL,ArchiveXL tools/cp-run soak
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let steps = 60;
    if step >= steps {
        CpCheck("soak_survived", true, ToString(steps) + " minutes");
        return -1.0;
    }
    let overlay = player.modmenuOverlay;
    switch step % 3 {
        case 0:
            let time = GameInstance.GetTimeSystem(player.GetGame());
            time.SetGameTimeBySeconds(Cast<Int32>(time.GetGameTimeStamp()) + 3 * 3600);
            break;
        case 1:
            if IsDefined(overlay) {
                overlay.ModMenu_Toggle(true);
            }
            break;
        default:
            if IsDefined(overlay) {
                overlay.ModMenu_Toggle(false);
            }
    }
    if step % 10 == 0 {
        TweakXL.Reload();
        CpShot("soak-" + ToString(step));
    }
    CpReport("{\"event\":\"SOAK\",\"step\":" + ToString(step) + "}");
    return 60.0;
}
