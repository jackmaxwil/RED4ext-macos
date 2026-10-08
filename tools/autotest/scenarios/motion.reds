module CpAutotest

// Denoiser quality in motion (MetalFX Denoiser plugin): at the save's position, for each variant (off: the game's NRD
// and MetalFX; fx: MetalFX's temporal denoised scaler with NRD's camera matrices; fxid: identity matrices; fxrh: the
// matrices with view space flipped to -z), twice: teleport back to the start, let the image settle with time frozen,
// arm the plugin's "motion" capture, walk forward for 1.2 s, freeze time. The plugin writes the upscaler output of the
// first frame after the camera stops and of the frame 90 frames later; their difference is what the motion left behind
// (ghosting, lag, noise in disoccluded areas). Report: scripts/motion_report.py <run>.
// Run through tools/rtbench: RTBENCH_SCENARIO=motion tools/rtbench pt rt_ultra
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpMotionStart: Vector4;

@addField(PlayerPuppet)
public let cpMotionYaw: Float;

func CpMotionMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

func CpMotionVariant(round: Int32) -> String {
    switch round % 4 {
        case 1: return "fx";
        case 2: return "fxid";
        case 3: return "fxrh";
    }
    return "off";
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let game = player.GetGame();
    let time = GameInstance.GetTimeSystem(game);
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"motion\"}");
        return 30.0;
    }
    if step == 1 {
        player.cpMotionStart = player.GetWorldPosition();
        player.cpMotionYaw = player.GetWorldYaw();
        time.SetTimeDilation(n"cpbench", 0.0);
        return 2.0;
    }
    // Steps 2..: eight rounds (off, fx, fxid, fxrh, twice) of [reset, walk, stop].
    let round = (step - 2) / 3;
    let phase = (step - 2) % 3;
    if round >= 8 {
        CpMotionMtl("denoise", "off");
        CpMotionMtl("denoisecam", "game");
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("motion_done", true, mode);
        return -1.0;
    }
    let variant = CpMotionVariant(round);
    let tag = "s0-" + mode + "-" + variant + ToString(round / 4 + 1);
    switch phase {
        case 0:
            CpMotionMtl("denoise", Equals(variant, "off") ? "off" : "fx");
            CpMotionMtl("denoisecam", Equals(variant, "fxid") ? "identity" : Equals(variant, "fxrh") ? "rh" : "game");
            let rot: EulerAngles;
            rot.Yaw = player.cpMotionYaw;
            GameInstance.GetTeleportationFacility(game).Teleport(player, player.cpMotionStart, rot);
            return 6.0;
        case 1:
            CpMotionMtl("motion", tag);
            time.UnsetTimeDilation(n"cpbench");
            CpReport("{\"event\":\"PRESS\",\"key\":13,\"hold\":1200}");
            return 1.8;
        default:
            time.SetTimeDilation(n"cpbench", 0.0);
            return 4.0;
    }
}
