module CpAutotest

// The MetalFX Denoiser plugin's dynamic render scale (DynamicScale.cpp, request "dynscale <fps>"): Ultra Performance
// at 2.25 (the sharpest allowed), frame timing; then a 40 fps target (25 ms: needs a coarser scale), frame timing after
// it settles and during a turn.
// The plugin logs each scale change ("Dynamic scale: a -> b").
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=dynscale tools/rtbench rt_psycho
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpDsYaw: Float;

func CpDsMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpDsTurn(player: ref<PlayerPuppet>, yaw: Float) -> Void {
    let rot: EulerAngles;
    rot.Yaw = yaw;
    GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.GetWorldPosition(), rot);
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"dynscale\"}");
        player.cpDsYaw = player.GetWorldYaw();
        return 30.0;
    }
    switch step {
        case 1:
            CpDsMtl("framegen", "off", 0);
            CpDsMtl("ultrascale", "2.25", 0);
            return 6.0;
        case 2:
            CpDsMtl("perf", "s0-" + mode + "-fixed", 180);
            return 9.0;
        case 3:
            CpDsMtl("dynscale", "40", 0);
            return 20.0;
        case 4:
            CpDsMtl("perf", "s0-" + mode + "-dyn", 180);
            return 9.0;
        case 5:
            CpDsMtl("perf", "s0-" + mode + "-dynturn", 180);
            return 0.0;
    }
    // Steps 6..185: a turn, 2 degrees per frame.
    if step < 186 {
        CpDsTurn(player, player.cpDsYaw - 2.0 * Cast<Float>(step - 5));
        return 0.0;
    }
    if step == 186 {
        return 10.0;
    }
    CpDsMtl("dynscale", "0", 0);
    CpCheck("dynscale_done", true, mode);
    return -1.0;
}
