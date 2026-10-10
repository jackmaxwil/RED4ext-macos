module CpAutotest

// Responsiveness settings of the MetalFX Denoiser plugin: frame timing with frame generation off (uncapped, then the
// plugin's frame limiter at 29 fps, which waits right before the game reads input), then frame generation on (run with
// METALFX_FG_MULT=4 for 4x) with the limiter, still and during a turn. The plugin logs the pacer's statistics and, on a
// visible display, input-to-display latency ("Latency (...)").
// Run through tools/rtbench with the game's own cap off: METALFX_FG_MULT=4 CP_RESOLUTION=3456x2160
//   RTBENCH_PRESET=Performance@3 RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4
//   RTBENCH_SETTINGS=/video/display:MaximumFPS_OnOff=false RTBENCH_SCENARIO=latency tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpLatYaw: Float;

func CpLatMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpLatTurn(player: ref<PlayerPuppet>, yaw: Float) -> Void {
    let rot: EulerAngles;
    rot.Yaw = yaw;
    GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.GetWorldPosition(), rot);
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"latency\"}");
        player.cpLatYaw = player.GetWorldYaw();
        return 30.0;
    }
    switch step {
        case 1:
            CpLatMtl("framegen", "off", 0);
            CpLatMtl("framelimit", "0", 0);
            return 4.0;
        case 2:
            CpLatMtl("perf", "s0-" + mode + "-off", 180);
            return 8.0;
        case 3:
            CpLatMtl("framelimit", "29", 0);
            return 4.0;
        case 4:
            CpLatMtl("perf", "s0-" + mode + "-off-limit29", 180);
            return 8.0;
        case 5:
            CpLatMtl("framegen", "on", 0);
            return 6.0;
        case 6:
            CpLatMtl("perf", "s0-" + mode + "-fg-limit29", 180);
            return 8.0;
        case 7:
            CpLatMtl("fgqueue", "game", 0); // 4x: generation in the game's command buffer instead of the pacer's queue
            return 4.0;
        case 8:
            CpLatMtl("perf", "s0-" + mode + "-fg-limit29-gamequeue", 180);
            return 8.0;
        case 9:
            CpLatMtl("fgqueue", "own", 0);
            CpLatMtl("perf", "s0-" + mode + "-fg-limit29-turn", 180);
            return 0.0;
    }
    // Steps 10..189: a turn, 1 degree per frame.
    if step < 190 {
        CpLatTurn(player, player.cpLatYaw - 1.0 * Cast<Float>(step - 9));
        return 0.0;
    }
    if step == 190 {
        return 8.0;
    }
    CpLatMtl("framegen", "off", 0);
    CpLatMtl("framelimit", "0", 0);
    CpCheck("latency_done", true, mode);
    return -1.0;
}
