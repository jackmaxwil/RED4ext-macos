module CpAutotest

// The hidden MetalFX "Ultra Performance" render scale (3x: MFX/OverrideEnable=1 and MFX/Quality=4, see the MetalFX
// Denoiser's docs) switched at runtime, with the game's NRD and with the denoiser. At the save's position, with time
// frozen: frame timing and a screenshot at the preset from the settings, then with the override, for off and fx.
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_SCENARIO=scale tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpScaleMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"scale\"}");
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    // Four rounds: (off, preset), (off, 3x), (fx, 3x), (fx, preset): [set, wait, screenshot, timing].
    let round = (step - 2) / 3;
    let phase = (step - 2) % 3;
    if round >= 4 {
        CpScaleMtl("cvar", "MFX/OverrideEnable=0", 0);
        CpScaleMtl("denoise", "off", 0);
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("scale_done", true, mode);
        return -1.0;
    }
    let fx = round >= 2;
    let ultra = round == 1 || round == 2;
    let tag = "s0-" + mode + "-" + (fx ? "fx" : "off") + (ultra ? "-3x" : "-preset");
    switch phase {
        case 0:
            CpScaleMtl("denoise", fx ? "fx" : "off", 0);
            CpScaleMtl("cvar", "MFX/Quality=4", 0);
            CpScaleMtl("cvar", ultra ? "MFX/OverrideEnable=1" : "MFX/OverrideEnable=0", 0);
            return 8.0;
        case 1:
            CpShot(tag);
            return 2.0;
        default:
            CpScaleMtl("perf", tag, 180);
            return 12.0;
    }
}
