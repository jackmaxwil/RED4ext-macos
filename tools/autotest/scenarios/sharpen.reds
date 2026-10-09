module CpAutotest

// Sharpening after MetalFX's denoised scaler (MetalFX Denoiser plugin, Denoise::SetSharpness), at the save's position
// with time frozen: a screenshot and frame timing at each strength, then a short walk forward with a screenshot while
// moving, at strength 0 and 0.5.
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_DENOISE=fx \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=sharpen tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpSharpMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpSharpLevel(i: Int32) -> String {
    switch i {
        case 0: return "0";
        case 1: return "0.25";
        case 2: return "0.5";
        case 3: return "0.75";
        default: return "1";
    }
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"sharpen\"}");
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    // Five strengths: [set, screenshot, timing].
    let round = (step - 2) / 3;
    let phase = (step - 2) % 3;
    if round < 5 {
        let tag = "s0-" + mode + "-sharp" + CpSharpLevel(round);
        switch phase {
            case 0:
                CpSharpMtl("sharpen", CpSharpLevel(round), 0);
                return 4.0;
            case 1:
                CpShot(tag);
                return 2.0;
            default:
                CpSharpMtl("perf", tag, 180);
                return 8.0;
        }
    }
    CpSharpMtl("sharpen", "0", 0);
    time.UnsetTimeDilation(n"cpbench");
    CpCheck("sharpen_done", true, mode);
    return -1.0;
}
