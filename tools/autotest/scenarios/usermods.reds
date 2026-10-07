module CpAutotest

// Checks the user's own mods in the world (whatever is installed in the game folder): records added by tweak
// files exist, and Immersive Timeskip's key (O) opens its time-skip screen. Screenshots before and after.
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    switch step {
        case 0:
            // From r6/tweaks/silent_silencers.yaml (Silent Silencers).
            CpCheck("silent_silencers_tweak", IsDefined(TweakDBInterface.GetUIIconRecord(t"UIIcon.SilentSilencerDebuff")), "");
            CpShot("before-timeskip");
            // 31: kVK_ANSI_O, bound to immersive_time_skip in r6/input/mods.xml.
            CpReport("{\"event\":\"PRESS\",\"key\":31}");
            return 4.0;
        case 1:
            CpShot("after-O");
            CpReport("{\"event\":\"PRESS\",\"key\":53}");
            return 3.0;
        case 2:
            CpShot("after-esc");
            return 2.0;
    }
    return -1.0;
}
