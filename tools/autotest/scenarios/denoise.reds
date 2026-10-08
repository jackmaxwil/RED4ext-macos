module CpAutotest

// Path tracing denoiser prototype (MetalFX Denoiser plugin, Denoise.mm): at the save's position, with time frozen,
// switches the plugin's denoise mode in turn (off: the game's NRD and MetalFX; pass: NRD's RELAX dropped and its noisy
// inputs passed through; fx: pass plus MetalFX's temporal denoised scaler) and records GPU frame times and a screenshot
// for each. Everything twice, so drift shows up as a difference between repeats.
// Run through tools/rtbench: RTBENCH_SCENARIO=denoise tools/rtbench pt; report: scripts/passcost_report.py <run>
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpDenoiseMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"denoise\"}");
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    // Steps 2..: six rounds (off, pass, fx, twice) of [set mode, wait, screenshot, timing].
    let round = (step - 2) / 3;
    let phase = (step - 2) % 3;
    if round >= 6 {
        CpDenoiseMtl("denoise", "off", 0);
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("denoise_done", true, mode);
        return -1.0;
    }
    let variant = "off";
    switch round % 3 {
        case 1: variant = "pass"; break;
        case 2: variant = "fx"; break;
    }
    let tag = "s0-" + mode + "-" + variant + ToString(round / 3 + 1);
    switch phase {
        case 0:
            CpDenoiseMtl("denoise", variant, 0);
            return 5.0;
        case 1:
            CpShot(tag);
            return 2.0;
        default:
            CpDenoiseMtl("perf", tag, 180);
            return 10.0;
    }
}
