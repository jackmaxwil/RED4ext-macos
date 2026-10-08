module CpAutotest

// Skin under the MetalFX Denoiser (player report: pale white skin with the denoiser in path tracing). At Kabuki Market
// (crowds), with time frozen: a screenshot with the game's NRD (off), the denoiser fed RELAX's PrePass output (fx, the
// default) and fed the raw signal (fxraw), and one frame's render targets and RELAX textures (dump; RTBENCH_DUMP picks
// the dispatches) to see how skin is stored in the G-buffer.
// Run through tools/rtbench: RTBENCH_SCENARIO=skin [RTBENCH_DUMP=1624964913] tools/rtbench pt; compare each screenshot
// with the off one (scripts/imgdiff.swift A B 0.08): fx should be within about 5 (mean error), like fxraw.
public func CpRunMenuChecks(scenario: String) -> Void {}

func CpSkinMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

func CpSkinTeleport(player: ref<PlayerPuppet>, name: String) -> Bool {
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

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    switch step {
        case 0:
            CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"skin\"}");
            return 20.0;
        case 1:
            CpCheck("skin_teleport", CpSkinTeleport(player, "Kabuki Market"), "Kabuki Market");
            return 40.0;
        case 2:
            time.SetTimeDilation(n"cpbench", 0.0);
            CpSkinMtl("denoise", "off");
            return 4.0;
        case 3:
            CpShot("s1-" + mode + "-off");
            CpSkinMtl("dump", "s1-" + mode + "-dump");
            return 8.0;
        case 4:
            CpSkinMtl("denoise", "fx");
            CpSkinMtl("denoiseprepass", "on");
            return 5.0;
        case 5:
            CpShot("s1-" + mode + "-fx");
            return 2.0;
        case 6:
            CpSkinMtl("denoiseprepass", "off");
            return 5.0;
        case 7:
            CpShot("s1-" + mode + "-fxraw");
            return 2.0;
    }
    CpSkinMtl("denoise", "off");
    CpSkinMtl("denoiseprepass", "on");
    time.UnsetTimeDilation(n"cpbench");
    CpCheck("skin_done", true, mode);
    return -1.0;
}
