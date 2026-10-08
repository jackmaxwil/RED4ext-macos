module CpAutotest

// The MetalFX Denoiser's player setting: its ModMenu page (MetalFX Denoiser > Ray tracing) is registered, and its
// toggles switch the denoiser at runtime. At the save's position, with time frozen: GPU frame times and a screenshot
// with the denoiser as configured, off, back on, and with the noisy-lighting debug view; the toggles end as they
// started. Run without a forced mode and with ModMenu loaded:
// RTBENCH_DENOISE='' RTBENCH_PLUGINS=ModMenu RTBENCH_SCENARIO=denoiser-menu tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpMenuStart: Bool;

func CpMenuMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    let mod = "MetalFXDenoiser";
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"denoiser-menu\"}");
            return 30.0;
        case 1:
            time.SetTimeDilation(n"cpbench", 0.0);
            CpCheck("denoiser_page", ModMenu_GetEntryCount(mod, "main") == 2, ToString(ModMenu_GetEntryCount(mod, "main")));
            player.cpMenuStart = ModMenu_GetToggleValue(mod, "main", "apple_denoiser");
            CpCheck("denoiser_toggle_start", true, ToString(player.cpMenuStart));
            return 4.0;
        case 2:
            CpShot("s0-" + mode + "-configured1");
            CpMenuMtl("perf", "s0-" + mode + "-configured1", 180);
            return 6.0;
        case 3:
            CpCheck("denoiser_toggle_off", ModMenu_SetToggleValue(mod, "main", "apple_denoiser", false), "");
            return 5.0;
        case 4:
            CpShot("s0-" + mode + "-off1");
            CpMenuMtl("perf", "s0-" + mode + "-off1", 180);
            return 6.0;
        case 5:
            CpCheck("denoiser_toggle_on", ModMenu_SetToggleValue(mod, "main", "apple_denoiser", true), "");
            return 5.0;
        case 6:
            CpShot("s0-" + mode + "-on1");
            CpMenuMtl("perf", "s0-" + mode + "-on1", 180);
            return 6.0;
        case 7:
            CpCheck("denoiser_noisy_on", ModMenu_SetToggleValue(mod, "main", "noisy_lighting", true), "");
            return 5.0;
        case 8:
            CpShot("s0-" + mode + "-noisy1");
            return 2.0;
        case 9:
            ModMenu_SetToggleValue(mod, "main", "noisy_lighting", false);
            ModMenu_SetToggleValue(mod, "main", "apple_denoiser", player.cpMenuStart);
            return 2.0;
    }
    time.UnsetTimeDilation(n"cpbench");
    CpCheck("denoiser_menu_done", true, mode);
    return -1.0;
}
