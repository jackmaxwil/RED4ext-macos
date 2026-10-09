module CpAutotest

// GPU time per pass (MetalFX Denoiser plugin "profile" request: GPU timestamps at every encoder's start and end in one
// traced frame). At the save's position, with time frozen: one frame with the game's NRD (off) and one with MetalFX's
// temporal denoised scaler (fx). Report: scripts/gpu_profile.py RUN/metal/s0-<mode>-<variant>.trace.jsonl
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_SCENARIO=profile tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpProfileMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"profile\"}");
            return 30.0;
        case 1:
            time.SetTimeDilation(n"cpbench", 0.0);
            CpProfileMtl("denoise", "off");
            return 5.0;
        case 2:
            CpProfileMtl("profile", "s0-" + mode + "-off");
            return 6.0;
        case 3:
            CpProfileMtl("denoise", "fx");
            return 6.0;
        case 4:
            CpProfileMtl("profile", "s0-" + mode + "-fx");
            return 6.0;
    }
    CpProfileMtl("denoise", "off");
    time.UnsetTimeDilation(n"cpbench");
    CpCheck("profile_done", true, mode);
    return -1.0;
}
