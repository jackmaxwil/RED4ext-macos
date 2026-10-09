module CpAutotest

// Frame generation HUD and frame warp check (MetalFX Denoiser plugin). With frame generation on: the camera turning one
// degree per frame for 15 s with the game's frame held for half a frame interval rounded down to refreshes (current),
// then 15 s with the old half-frame hold (the plugin logs per 240 presents how many generated frames had the HUD
// restored; a screenshot in each). Then frame warp on with synthetic mouse turns (cp-run's MOUSE event): left and right
// to calibrate, then a slow turn during which six re-aimed frames are saved next to the game's next frame (warpdump).
// Run: CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_DENOISE=fx \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=hudcheck tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpHcYaw: Float;

func CpHcMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

func CpHcMouse(dx: Int32, dy: Int32, steps: Int32, ms: Int32) -> Void {
    CpReport("{\"event\":\"MOUSE\",\"dx\":" + ToString(dx) + ",\"dy\":" + ToString(dy) + ",\"steps\":" + ToString(steps)
        + ",\"ms\":" + ToString(ms) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"hudcheck\"}");
        player.cpHcYaw = player.GetWorldYaw();
        return 30.0;
    }
    if step == 1 {
        CpHcMtl("framegen", "on");
        return 4.0;
    }
    // Two hold phases of 360 steps (one degree per frame, about 15 s).
    let phase = (step - 2) / 360;
    let i = (step - 2) % 360;
    if phase < 2 {
        if i == 0 {
            CpHcMtl("fghold", phase == 0 ? "floor" : "half");
        }
        let rot: EulerAngles;
        rot.Yaw = player.cpHcYaw - Cast<Float>(i % 90);
        GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.GetWorldPosition(), rot);
        if i == 200 {
            CpShot("s0-" + mode + "-hud" + ToString(phase));
            return 2.0;
        }
        return 0.0;
    }
    switch step - 722 {
        case 0:
            CpHcMtl("fghold", "floor");
            CpHcMtl("warp", "on");
            return 2.0;
        case 1: CpHcMouse(6, 0, 250, 8); return 2.5;    // calibration: right, left, up and down
        case 2: CpHcMouse(-6, 0, 250, 8); return 2.5;
        case 3: CpHcMouse(0, 4, 120, 8); return 1.2;
        case 4: CpHcMouse(0, -4, 120, 8); return 1.2;
        case 5: CpHcMouse(6, 0, 250, 8); return 2.5;
        case 6: CpHcMouse(-6, 0, 250, 8); return 2.5;
        case 7: CpHcMouse(4, 0, 1000, 8); return 3.0;   // a slow 8 s turn
        case 8: CpHcMtl("warpdump", "6"); return 6.0;
        default:
            CpHcMtl("warp", "off");
            CpHcMtl("framegen", "off");
            CpCheck("hudcheck_done", true, mode);
            return -1.0;
    }
}
