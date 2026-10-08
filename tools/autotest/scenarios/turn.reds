module CpAutotest

// Denoiser artifacts while the camera turns (MetalFX Denoiser plugin): at the save's position, for each variant (off:
// the game's NRD and MetalFX; pass: no denoiser, the noisy lighting the denoiser gets; fx: MetalFX's temporal denoised
// scaler, fed RELAX's PrePass output (the default); fxraw: fed the raw noisy signal), twice: turn back to the start
// with time running, freeze time, flick right by 6 degrees every 20 ms (16 steps, 96 degrees) and ask the plugin for
// the upscaler output halfway (<name>-..-shot: new content enters at the right edge), then once more 3 s after the turn (settled).
// Run through tools/rtbench: RTBENCH_SCENARIO=turn tools/rtbench pt (CP_RESOLUTION=2560x1440 for lower frame rates)
public func CpRunMenuChecks(scenario: String) -> Void {}

@addField(PlayerPuppet)
public let cpTurnStart: Vector4;

@addField(PlayerPuppet)
public let cpTurnYaw: Float;

func CpTurnMtl(kind: String, name: String) -> Void {
    CpReport("{\"event\":\"MTL\",\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"frames\":0}");
}

func CpTurnVariant(round: Int32) -> String {
    switch round % 4 {
        case 1: return "pass";
        case 2: return "fx";
        case 3: return "fxraw";
    }
    return "off";
}

func CpTurnTo(player: ref<PlayerPuppet>, yaw: Float) -> Void {
    let rot: EulerAngles;
    rot.Yaw = yaw;
    GameInstance.GetTeleportationFacility(player.GetGame()).Teleport(player, player.cpTurnStart, rot);
}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    let mode = StrLen(CpAutotestTag()) > 0 ? CpAutotestTag() : "current";
    let time = GameInstance.GetTimeSystem(player.GetGame());
    let turnSteps = 16;
    let phases = turnSteps + 4;
    if step == 0 {
        CpReport("{\"event\":\"BENCH_MODE\",\"mode\":\"" + mode + "\",\"settings\":\"turn\"}");
        return 30.0;
    }
    if step == 1 {
        player.cpTurnStart = player.GetWorldPosition();
        player.cpTurnYaw = player.GetWorldYaw();
        return 1.0;
    }
    // Steps 2..: eight rounds (off, pass, fx, fxrh, twice) of [reset, freeze, 60 turn steps, settled shot].
    let round = (step - 2) / phases;
    let phase = (step - 2) % phases;
    if round >= 8 {
        CpTurnMtl("denoise", "off");
        CpTurnMtl("denoiseprepass", "on");
        time.UnsetTimeDilation(n"cpbench");
        CpTurnTo(player, player.cpTurnYaw);
        CpCheck("turn_done", true, mode);
        return -1.0;
    }
    let variant = CpTurnVariant(round);
    let tag = "s0-" + mode + "-" + variant + ToString(round / 4 + 1);
    if phase == 0 { // time runs while turning back: with time frozen the teleport would only land with the next turn
        time.UnsetTimeDilation(n"cpbench");
        CpTurnMtl("denoise", Equals(variant, "off") ? "off" : Equals(variant, "pass") ? "pass" : "fx");
        CpTurnMtl("denoiseprepass", Equals(variant, "fxraw") ? "off" : "on");
        CpTurnTo(player, player.cpTurnYaw);
        return 5.0;
    }
    if phase == 1 {
        time.SetTimeDilation(n"cpbench", 0.0);
        return 2.0;
    }
    if phase < phases - 2 {
        let i = phase - 1;
        CpTurnTo(player, player.cpTurnYaw - 6.0 * Cast<Float>(i));
        if i == turnSteps / 2 {
            CpTurnMtl("shot", tag + "-turning");
        }
        return 0.02;
    }
    if phase == phases - 2 {
        return 3.0;
    }
    CpTurnMtl("shot", tag + "-settled");
    return 1.5;
}
