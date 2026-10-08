module CpAutotest

// Ray-tracing benchmark, one ray tracing mode per run (tools/rtbench sets the mode in UserSettings.json through
// cp-run's CP_SETTINGS and names it with CP_TAG; changing these settings at runtime does not switch the renderer).
// At each spot: freeze time, take two still screenshots 2 s apart (flicker), record GPU frame times, trace one frame
// (first spot only), then walk forward and take a screenshot in motion (ghosting).
//
// Frame times and traces come from the MetalFX Denoiser plugin's tracer (METALFX_TRACE=1, CP_PLUGINS=MetalFXDenoiser).
// Spots: 0 is where the save puts the player; the others are fast travel points (CpBenchSpotName) at 23:00. Every
// fast travel point the save knows is listed once (FTPOINT events), to choose spots from.
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpBenchSkip: Bool;

// Fast travel point display names (English) for spots 1.., visited at 23:00.
func CpBenchSpotName(spot: Int32) -> String {
    switch spot {
        case 1: return "Kabuki Market";
        case 2: return "Megabuilding H10: Atrium";
        case 3: return "Afterlife";
        case 4: return "All Foods Plant";
    }
    return "";
}

func CpBenchSpots() -> Int32 = 5;

// Finds a settings variable: UserSettings.GetVar first, then by walking the groups from the root (group paths on this
// platform may differ from the names in UserSettings.json).
func CpBenchFindIn(group: ref<ConfigGroup>, path: String, name: CName) -> ref<ConfigVar> {
    if !IsDefined(group) {
        return null;
    }
    if StrEndsWith(NameToString(group.GetPath()), path) && group.HasVar(name) {
        return group.GetVar(name);
    }
    for child in group.GetGroups(true) {
        let found = CpBenchFindIn(child, path, name);
        if IsDefined(found) {
            return found;
        }
    }
    return null;
}

func CpBenchVar(ss: ref<UserSettings>, group: CName, name: CName) -> ref<ConfigVar> {
    let v = ss.GetVar(group, name);
    if IsDefined(v) {
        return v;
    }
    return CpBenchFindIn(ss.GetRootGroup(), NameToString(group), name);
}

func CpBenchListValue(ss: ref<UserSettings>, group: CName, name: CName) -> String {
    let any = CpBenchVar(ss, group, name);
    if !IsDefined(any) {
        return "missing";
    }
    let vs = any as ConfigVarListString;
    if IsDefined(vs) {
        return vs.GetValue();
    }
    let v = any as ConfigVarListName;
    if IsDefined(v) {
        return NameToString(v.GetValue());
    }
    return "type " + NameToString(any.GetClassName());
}

func CpBenchTeleportToPoint(player: ref<PlayerPuppet>, name: String) -> Bool {
    let ft = GameInstance.GetScriptableSystemsContainer(player.GetGame()).Get(n"FastTravelSystem") as FastTravelSystem;
    if !IsDefined(ft) {
        return false;
    }
    for point in ft.GetFastTravelPoints() {
        if Equals(GetLocalizedText(point.GetPointDisplayName()), name) {
            GameInstance.GetTeleportationFacility(player.GetGame()).TeleportToNode(player, point.GetMarkerRef());
            return true;
        }
    }
    return false;
}

func CpBenchListPoints(player: ref<PlayerPuppet>) -> Void {
    let ft = GameInstance.GetScriptableSystemsContainer(player.GetGame()).Get(n"FastTravelSystem") as FastTravelSystem;
    if !IsDefined(ft) {
        CpReport("{\"event\":\"FTPOINT\",\"error\":\"no FastTravelSystem\"}");
        return;
    }
    for point in ft.GetFastTravelPoints() {
        CpReport("{\"event\":\"FTPOINT\",\"name\":\"" + GetLocalizedText(point.GetPointDisplayName()) + "\",\"district\":\""
            + GetLocalizedText(point.GetDistrictDisplayName()) + "\"}");
    }
}

func CpBenchFreeze(game: GameInstance, freeze: Bool) -> Void {
    let time = GameInstance.GetTimeSystem(game);
    if freeze {
        time.SetTimeDilation(n"cpbench", 0.0);
    } else {
        time.UnsetTimeDilation(n"cpbench");
    }
}

func CpBenchMtl(kind: String, name: String, frames: Int32) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":" + ToString(frames) + "}");
}

func CpBenchMode() -> String {
    let tag = CpAutotestTag();
    return StrLen(tag) > 0 ? tag : "current";
}

func CpBenchBool(ss: ref<UserSettings>, group: CName, name: CName) -> String {
    let v = CpBenchVar(ss, group, name) as ConfigVarBool;
    return IsDefined(v) ? ToString(v.GetValue()) : "missing";
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let game = player.GetGame();
    let phasesPerSpot = 9;
    if step == 0 {
        let ss = GameInstance.GetSettingsSystem(game);
        let rt = n"/graphics/raytracing";
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + CpBenchMode() + "\",\"settings\":\"rt="
            + CpBenchBool(ss, rt, n"RayTracing") + " pt=" + CpBenchBool(ss, rt, n"RayTracedPathTracing") + " lighting="
            + CpBenchListValue(ss, rt, n"RayTracedLighting") + " scaling="
            + CpBenchListValue(ss, n"/graphics/presets", n"ResolutionScaling") + " metalfx="
            + CpBenchListValue(ss, n"/graphics/presets", n"MetalFX") + "\"}");
        CpBenchListPoints(player);
        return 1.0;
    }
    let s = step - 1;
    let spot = s / phasesPerSpot;
    if spot >= CpBenchSpots() {
        CpBenchFreeze(game, false);
        CpCheck("rtbench_done", true, CpBenchMode() + ", " + ToString(CpBenchSpots()) + " spots");
        return -1.0;
    }
    let k = s % phasesPerSpot;
    if k == 0 {
        // Arrive: spot 0 stays where the save is; the others are fast travel points at night.
        player.cpBenchSkip = false;
        if spot > 0 {
            let name = CpBenchSpotName(spot);
            if !CpBenchTeleportToPoint(player, name) {
                // Not a fast travel point this save knows: skip the spot's steps.
                player.cpBenchSkip = true;
                CpReport("{\"event\":\"BENCH_SKIP\",\"spot\":" + ToString(spot) + ",\"name\":\"" + name + "\"}");
                return 0.1;
            }
            GameInstance.GetTimeSystem(game).SetGameTimeByHMS(23, 0, 0);
            // A new area streams in for a while after the teleport (GPU frame times ~100 ms at first).
            return 25.0;
        }
        return 5.0;
    }
    if player.cpBenchSkip {
        return 0.1;
    }
    let tag = "s" + ToString(spot) + "-" + CpBenchMode();
    switch k {
        case 1:
            let pos = player.GetWorldPosition();
            CpReport("{\"event\":\"BENCH_SPOT\",\"spot\":" + ToString(spot) + ",\"mode\":\"" + CpBenchMode()
                + "\",\"x\":" + ToString(pos.X) + ",\"y\":" + ToString(pos.Y) + ",\"z\":" + ToString(pos.Z) + ",\"yaw\":"
                + ToString(player.GetWorldYaw()) + "}");
            CpBenchFreeze(game, true);
            return 3.0;
        case 2:
            CpShot(tag + "-still-a");
            return 2.0;
        case 3:
            CpShot(tag + "-still-b");
            return 1.5;
        case 4:
            CpBenchMtl("perf", tag, 180);
            return 10.0;
        case 5:
            if spot == 0 {
                CpBenchMtl("trace", tag, 1);
                return 5.0;
            }
            return 0.1;
        case 6:
            CpBenchFreeze(game, false);
            // 13: kVK_ANSI_W, held 1.5 s.
            CpReport("{\"event\":\"PRESS\",\"key\":13,\"hold\":1500}");
            return 1.2;
        case 7:
            CpShot(tag + "-motion");
            return 3.0;
        default:
            return 0.1;
    }
}
