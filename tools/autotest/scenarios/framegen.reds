module CpAutotest

// MetalFX frame interpolation (MetalFX Denoiser plugin, FrameGen.mm) with path tracing at 3x and the denoiser. At the
// save's position: frame times with frame generation off and on while time runs (the plugin logs displayed frame
// intervals: "FrameGen: displayed frame interval"), screenshots, and a turn with frame generation on.
// Run through tools/rtbench: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_DENOISE=fx \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=framegen tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpFgYaw: Float;

func CpFgMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpFgTurn(player: ref<PlayerPuppet>, yaw: Float) -> Void {
    let rot: EulerAngles;
    rot.Yaw = yaw;
    GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.GetWorldPosition(), rot);
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"framegen\"}");
        player.cpFgYaw = player.GetWorldYaw();
        return 30.0;
    }
    switch step {
        case 1:
            CpFgMtl("framegen", "off", 0);
            return 4.0;
        case 2:
            CpShot("s0-" + mode + "-fgoff");
            CpFgMtl("perf", "s0-" + mode + "-fgoff", 240);
            return 12.0;
        case 3:
            CpFgMtl("framegen", "on", 0);
            return 6.0;
        case 4:
            CpShot("s0-" + mode + "-fgon");
            CpFgMtl("perf", "s0-" + mode + "-fgon", 240);
            return 14.0;
    }
    // Steps 5..154: a slow turn with frame generation on (150 steps of 1 degree, one per frame: steady motion for the
    // plugin's interpolation check), a screenshot halfway.
    if step < 155 {
        CpFgTurn(player, player.cpFgYaw - 1.0 * Cast<Float>(step - 4));
        if step == 6 {
            CpFgMtl("fgeval", "12", 0); // the plugin's interpolation check (FrameGen::Evaluate) during the steady turn
        }
        if step == 35 {
            CpShot("s0-" + mode + "-fgturn");
        }
        return 0.0;
    }
    if step == 155 {
        return 5.0;
    }
    CpFgMtl("framegen", "off", 0);
    CpCheck("framegen_done", true, mode);
    return -1.0;
}
