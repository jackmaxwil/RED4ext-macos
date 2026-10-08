module CpAutotest

// Engine config variable experiments: at the save's position, with time frozen, sets each variable in turn through
// the MetalFX Denoiser plugin (request "cvar <group>/<name>=<value>": verified addresses, checked in memory), takes a
// screenshot and GPU frame times, then restores the default and times the frame again, so each experiment has a
// baseline right before and after it. Results: metal/cvar.jsonl (before/after values), metal/*.perf.json.
// Run through tools/rtbench: RTBENCH_SCENARIO=cvartest tools/rtbench rt_ultra pt
// Report: ../cp2077-metalfx-denoiser/scripts/cvartest_report.py <runs>
public func CpRunMenuChecks(scenario: String) -> Void {}

// Experiment i: variable, experiment value, default (restored after).
func CpCvarCount() -> Int32 = 8;

func CpCvarPath(i: Int32) -> String {
    switch i {
        case 0: return "RayTracing/Debug/SkipStaticMeshes";
        case 1: return "RayTracing/Diffuse/EnableHalfResolutionTracing";
        case 2: return "RayTracing/Reflection/EnableHalfResolutionTracing";
        case 3: return "RayTracing/EnableNRD";
        case 4: return "RayTracing/Reference/RayNumber";
        case 5: return "RayTracing/Reference/RayNumber";
        case 6: return "RayTracing/Reference/BounceNumber";
    }
    return "RayTracing/Reference/BounceNumber";
}

func CpCvarValue(i: Int32) -> String {
    switch i {
        case 0: return "true";
        case 1: return "0";
        case 2: return "0";
        case 3: return "false";
        case 4: return "1";
        case 5: return "2";
        case 6: return "1";
    }
    return "3";
}

func CpCvarDefault(i: Int32) -> String {
    switch i {
        case 0: return "false";
        case 1: return "1";
        case 2: return "1";
        case 3: return "true";
    }
    return "0xDEADBEEF"; // RayNumber / BounceNumber: "not set, the quality preset decides"
}

func CpCvarMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"cvartest\"}");
        return 30.0;
    }
    if step == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 3.0;
    }
    if step == 2 {
        CpCvarMtl("perf", "s0-" + mode + "-base0", 180);
        return 10.0;
    }
    // Per experiment: set, screenshot, time, restore, time.
    let i = (step - 3) / 5;
    let phase = (step - 3) % 5;
    if i >= CpCvarCount() {
        time.UnsetTimeDilation(n"cpbench");
        CpCheck("cvartest_done", true, mode);
        return -1.0;
    }
    switch phase {
        case 0:
            CpCvarMtl("cvar", CpCvarPath(i) + "=" + CpCvarValue(i), 0);
            return 3.0;
        case 1:
            CpShot("s0-" + mode + "-exp" + ToString(i));
            return 2.0;
        case 2:
            CpCvarMtl("perf", "s0-" + mode + "-exp" + ToString(i), 180);
            return 10.0;
        case 3:
            CpCvarMtl("cvar", CpCvarPath(i) + "=" + CpCvarDefault(i), 0);
            return 3.0;
        default:
            CpCvarMtl("perf", "s0-" + mode + "-base" + ToString(i + 1), 180);
            return 10.0;
    }
}
