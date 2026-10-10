module CpAutotest

// Camera warp (MetalFX Denoiser plugin, Warp.mm) with 4x frame generation: synthetic mouse turns sent to the game
// process only (cp-run's MOUSE event, mousemove.swift: the user's pointer does not move) so the warp calibrates, then
// a steady turn during which the plugin saves 16 consecutive game frames warped and not ("warpeval"):
// scripts/warpeval_report.py checks that a warped frame is closer to the next frame than the frame itself.
// Run: METALFX_FG_MULT=4 CP_RESOLUTION=3456x2160 RTBENCH_PRESET=Performance@3
//   RTBENCH_CVARS=MFX/OverrideEnable=1,MFX/Quality=4 RTBENCH_SETTINGS=/video/display:MaximumFPS_OnOff=false
//   RTBENCH_SCENARIO=warpcheck tools/rtbench pt
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpWkMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

func CpWkMouse(dx: Int32, dy: Int32, steps: Int32, ms: Int32) -> Void {
    CpReport("{\"event\":\"MOUSE\",\"dx\":" + ToString(dx) + ",\"dy\":" + ToString(dy) + ",\"steps\":" + ToString(steps)
        + ",\"ms\":" + ToString(ms) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"warpcheck\"}");
            return 30.0;
        case 1:
            CpWkMtl("framelimit", "0");
            CpWkMtl("framegen", "on");
            CpWkMtl("warp", "on");
            return 6.0;
        // Calibration: left and right at three speeds, then down and up.
        case 2: CpWkMouse(4, 0, 125, 8); return 2.0;
        case 3: CpWkMouse(-4, 0, 125, 8); return 2.0;
        case 4: CpWkMouse(12, 0, 60, 8); return 1.5;
        case 5: CpWkMouse(-12, 0, 60, 8); return 1.5;
        case 6: CpWkMouse(30, 0, 25, 8); return 1.0;
        case 7: CpWkMouse(-30, 0, 25, 8); return 1.0;
        case 8: CpWkMouse(0, 6, 60, 8); return 1.5;
        case 9: CpWkMouse(0, -6, 60, 8); return 1.5;
        case 10: CpWkMouse(6, 0, 400, 8); return 1.0;     // a steady turn, 3.2 s
        case 11: CpWkMtl("warpeval", "16"); return 3.0;   // 16 frames during it
        case 12: CpWkMouse(-6, 0, 400, 8); return 4.0;    // back
        case 13: return 60.0;                             // the saves are written in the background
        default:
            CpWkMtl("warp", "off");
            CpWkMtl("framegen", "off");
            CpCheck("warpcheck_done", true, mode);
            return -1.0;
    }
}
