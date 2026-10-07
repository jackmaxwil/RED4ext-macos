module CpAutotest

// TweakXL hot reload in the world: edit the tweak file, press the reload key (\), check TweakDB has the new values.
public func CpRunMenuChecks(scenario: String) -> Void {}

public func CpRunWorldStep(player: ref<PlayerPuppet>, step: Int32) -> Float {
    switch step {
        case 0:
            player.cpActionLogger = new CpActionLogger();
            player.RegisterInputListener(player.cpActionLogger);
            let before = TweakDBInterface.GetInt(t"CpAutotest.ReloadFlat", -1);
            CpCheck("tweak_loaded", before == 1, ToString(before));
            // cp-run swaps in tweakxl-reload.next.yaml.
            CpReport("{\"event\":\"NEXT_TWEAK\"}");
            return 3.0;
        case 1:
            // Space (Jump): shows whether keys reach the game in the world at all (CpActionLogger reports it).
            CpReport("{\"event\":\"PRESS\",\"key\":49}");
            return 3.0;
        case 2:
            // 42: kVK_ANSI_Backslash.
            CpReport("{\"event\":\"PRESS\",\"key\":42}");
            return 4.0;
        case 3:
            let changed = TweakDBInterface.GetInt(t"CpAutotest.ReloadFlat", -1);
            let added = TweakDBInterface.GetInt(t"CpAutotest.ReloadAdded", -1);
            CpCheck("reload_changed_flat", changed == 2, ToString(changed));
            CpCheck("reload_added_flat", added == 7, ToString(added));
            CpShot("reloaded");
            return 3.0;
        case 4:
            // Separates the key binding from the reload itself.
            TweakXL.Reload();
            CpCheck("reload_direct", TweakDBInterface.GetInt(t"CpAutotest.ReloadFlat", -1) == 2, "");
            return 1.0;
    }
    return -1.0;
}

@addField(PlayerPuppet)
public let cpActionLogger: ref<CpActionLogger>;

// Reports every released input action, to show what a key press reached the game as.
public class CpActionLogger {
    protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
        if Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
            CpReport("{\"event\":\"ACTION\",\"name\":\"" + NameToString(ListenerAction.GetName(action)) + "\"}");
        }
        return false;
    }
}
