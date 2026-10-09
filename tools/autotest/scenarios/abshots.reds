module CpAutotest

// A/B screenshots at the save's position (moved CP_AB_FORWARD metres forward, see step 1) with time frozen (MetalFX Denoiser plugin requests): current settings,
// sharpening 0, the game's NRD (denoise off), pass-through without the denoised scaler (denoise pass); each after a
// settle. Run through tools/rtbench (RTBENCH_SCENARIO=abshots).
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpAbMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"abshots\"}");
            return 30.0;
        case 1:
            CpAbMtl("framegen", "off"); // real frames only
            // Forward along the view, where the street with NPCs is.
            let pos = player.GetWorldPosition();
            let fwd = player.GetWorldForward();
            pos.X += fwd.X * 35.0;
            pos.Y += fwd.Y * 35.0;
            let rot: EulerAngles;
            rot.Yaw = player.GetWorldYaw();
            GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, pos, rot);
            return 12.0;
        case 2: time.SetTimeDilation(n"cpbench", 0.0); CpShot("s0-" + mode + "-a-current"); return 3.0;
        case 3: CpAbMtl("sharpen", "0"); return 4.0;
        case 4: CpShot("s0-" + mode + "-b-sharpen0"); return 3.0;
        case 5: CpAbMtl("denoise", "off"); return 6.0;
        case 6: CpShot("s0-" + mode + "-c-nrd"); return 3.0;
        case 7: CpAbMtl("denoise", "pass"); return 6.0;
        case 8: CpShot("s0-" + mode + "-d-pass"); return 3.0;
        case 9: CpAbMtl("denoise", "fx"); CpAbMtl("sharpen", "0.4"); return 4.0;
        default:
            time.UnsetTimeDilation(n"cpbench");
            CpCheck("abshots_done", true, mode);
            return -1.0;
    }
}
