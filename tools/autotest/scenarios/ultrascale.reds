module CpAutotest

// The render scale of MetalFX "Ultra Performance" (MetalFX Denoiser plugin: ConfigVars::SetUltraScale, request
// "ultrascale <scale>"), at the save's position with time frozen. Per scale: set it, settle, screenshot, frame timing,
// then a turn (1 degree per frame) with a screenshot during it (the temporal upscaler in motion), and back.
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_DENOISE=fx \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=ultrascale tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpUsYaw: Float;

func CpUsMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpUsScale(i: Int32) -> String {
    switch i {
        case 0: return "3";
        case 1: return "2.75";
        default: return "2.5";
    }
}

func CpUsTurn(player: ref<PlayerPuppet>, yaw: Float) -> Void {
    let rot: EulerAngles;
    rot.Yaw = yaw;
    GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.GetWorldPosition(), rot);
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"ultrascale\"}");
        player.cpUsYaw = player.GetWorldYaw();
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    // Per scale, 40 steps: 0 set, 1 screenshot, 2 timing, 3..32 turn (screenshot at 23), 33 turn back, 34..39 idle.
    let round = (step - 2) / 40;
    let phase = (step - 2) % 40;
    if round >= 3 {
        CpUsMtl("ultrascale", "3", 0);
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("ultrascale_done", true, mode);
        return -1.0;
    }
    let tag = "s0-" + mode + "-x" + CpUsScale(round);
    if phase == 0 {
        CpUsMtl("ultrascale", CpUsScale(round), 0);
        return 6.0;
    }
    if phase == 1 {
        CpShot(tag);
        return 2.0;
    }
    if phase == 2 {
        CpUsMtl("perf", tag, 180);
        return 9.0;
    }
    if phase < 33 {
        CpUsTurn(player, player.cpUsYaw - 1.0 * Cast<Float>(phase - 2));
        if phase == 23 {
            CpShot(tag + "-turn");
            return 2.0;
        }
        return 0.0;
    }
    if phase == 33 {
        CpUsTurn(player, player.cpUsYaw);
        return 3.0;
    }
    return 0.0;
}
