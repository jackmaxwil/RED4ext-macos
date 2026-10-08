module CpAutotest

// What parts of the frame cost: at the save's position, with time frozen, runs the MetalFX Denoiser tracer's skip test
// in turn (off; "listed": the shaders in skip-fingerprints.txt, for example NRD's; "refit": acceleration structure
// refits; "raygen": the ray generation kernels; "metalfx": the MetalFX temporal scaler) and records GPU frame times and a screenshot for each. Everything
// twice, so drift shows up as a difference between repeats.
// Run through tools/rtbench: RTBENCH_SCENARIO=passcost RTBENCH_SKIP='REBLUR_|RELAX_|SIGMA_' tools/rtbench rt_ultra pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpCostMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"passcost\"}");
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    // Steps 2..: ten rounds (off, listed, refit, raygen, metalfx, twice) of [set skip, wait, screenshot, timing].
    let round = (step - 2) / 3;
    let phase = (step - 2) % 3;
    if round >= 10 {
        CpCostMtl("skip", "off", 0);
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("passcost_done", true, mode);
        return -1.0;
    }
    let variant = "off";
    switch round % 5 {
        case 1: variant = "listed"; break;
        case 2: variant = "refit"; break;
        case 3: variant = "raygen"; break;
        case 4: variant = "metalfx"; break;
    }
    let tag = "s0-" + mode + "-" + variant + ToString(round / 5 + 1);
    switch phase {
        case 0:
            CpCostMtl("skip", variant, 0);
            return 3.0;
        case 1:
            CpShot(tag);
            return 2.0;
        default:
            CpCostMtl("perf", tag, 180);
            return 10.0;
    }
}
