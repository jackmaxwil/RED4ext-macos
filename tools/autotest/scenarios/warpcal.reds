module CpAutotest

// Frame warp calibration data (MetalFX Denoiser plugin, Warp.mm): synthetic mouse turns sent to the game process
// (cp-run's MOUSE event, mousemove.swift) with frame generation on, while the plugin records each frame's camera and the
// game's mouse events (METALFX_WARP_RECORD=<prefix>). Turns: left and right at three speeds, then up and down.
// Run: METALFX_WARP_RECORD=/path/prefix CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3 RTBENCH_DENOISE=fx \
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SCENARIO=warpcal tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpWcMouse(dx: Int32, dy: Int32, steps: Int32, ms: Int32) -> Void {
    CpReport("{\"event\":\"MOUSE\",\"dx\":" + ToString(dx) + ",\"dy\":" + ToString(dy) + ",\"steps\":" + ToString(steps)
        + ",\"ms\":" + ToString(ms) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"warpcal\"}");
            return 30.0;
        case 1:
            CpReport("{\"event\":\"MTL\",\"kind\":\"framegen\",\"name\":\"on\",\"frames\":0}");
            return 6.0;
        case 2: CpWcMouse(4, 0, 125, 8); return 2.0;    // slow right, 1 s
        case 3: CpWcMouse(-4, 0, 125, 8); return 2.0;   // back
        case 4: CpWcMouse(12, 0, 60, 8); return 1.5;    // medium
        case 5: CpWcMouse(-12, 0, 60, 8); return 1.5;
        case 6: CpWcMouse(30, 0, 25, 8); return 1.0;    // fast
        case 7: CpWcMouse(-30, 0, 25, 8); return 1.0;
        case 8: CpWcMouse(0, 6, 60, 8); return 1.5;     // down
        case 9: CpWcMouse(0, -6, 60, 8); return 1.5;    // up
        case 10: CpWcMouse(8, 4, 90, 8); return 1.5;    // diagonal
        case 11: CpWcMouse(-8, -4, 90, 8); return 1.5;
        case 12: CpWcMouse(4, 0, 125, 8); return 2.0;   // again, slow
        case 13: CpWcMouse(-4, 0, 125, 8); return 2.0;
        default:
            CpReport("{\"event\":\"MTL\",\"kind\":\"framegen\",\"name\":\"off\",\"frames\":0}");
            CpCheck("warpcal_done", true, mode);
            return -1.0;
    }
}
