module CpAutotest

// One frame's render targets and MetalFX inputs as PNGs (MetalFX Denoiser plugin request "dump"), at the save's
// position with time frozen, to map the G-buffer. Run through tools/rtbench: RTBENCH_SCENARIO=dump tools/rtbench rt_ultra
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"dump\"}");
            return 30.0;
        case 1:
            GameInstance.GetTimeSystem(player.GetGame()).SetTimeDilation(n"cpbench", 0.0);
            return 3.0;
        case 2:
            CpReport("{\"event\":\"MTL\",\"kind\":\"dump\",\"name\":\"s0-" + mode + "\",\"frames\":0}");
            return 8.0;
        case 3:
            CpShot("s0-" + mode);
            return 3.0;
    }
    GameInstance.GetTimeSystem(player.GetGame()).UnsetTimeDilation(n"cpbench");
    CpCheck("dump_done", true, mode);
    return -1.0;
}
