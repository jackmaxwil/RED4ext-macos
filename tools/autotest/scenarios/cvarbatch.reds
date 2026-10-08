module CpAutotest

// Engine config variable experiments, run by the MetalFX Denoiser plugin from its cvar-experiments.txt: at the save's
// position with time frozen, the plugin sets each variable, asks for a screenshot and times the frame, then restores
// the value and does the same again, and reports DONE itself (see StartBatch in its MetalTrace.mm).
// Run through tools/rtbench: RTBENCH_SCENARIO=cvarbatch RTBENCH_EXPERIMENTS=<file> tools/rtbench rt_ultra pt
// Report: ../cp2077-metalfx-denoiser/scripts/cvar_report.py <runs>
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"cvarbatch\"}");
            return 30.0;
        case 1:
            GameInstance.GetTimeSystem(player.GetGame()).SetTimeDilation(n"cpbench", 0.0);
            CpReport("{\"event\":\"MTL\",\"kind\":\"cvarbatch\",\"name\":\"start\",\"frames\":0}");
            return 3600.0; // the plugin reports DONE when the batch ends
    }
    return -1.0;
}
