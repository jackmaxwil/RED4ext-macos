module CpAutotest

// TweakXL: scenarios/tweakxl.yaml was applied to TweakDB before the main menu.
public func CpRunMenuChecks(scenario: String) -> Void {
    let flat = TweakDBInterface.GetInt(t"CpAutotest.NewFlat", 0);
    CpCheck("tweakxl_new_flat", flat == 2077, ToString(flat));
    let item = TweakDBInterface.GetItemRecord(t"CpAutotest.NewItem");
    CpCheck("tweakxl_new_record", IsDefined(item), "");
    if IsDefined(item) {
        let quality = item.Quality().GetID();
        CpCheck("tweakxl_record_override", quality == t"Quality.Legendary", TDBID.ToStringDEBUG(quality));
    }
    let stat = EnumValueFromName(n"gamedataStatType", n"CpAutotestStat");
    CpCheck("tweakxl_custom_stat", stat > 0l, ToString(stat));
    let scripted = TweakDBInterface.GetInt(t"CpAutotest.ScriptFlat", 0);
    CpCheck("tweakxl_scriptable_tweak", scripted == 42, ToString(scripted));
}

// Runs through TweakXL's scriptable-tweak path (RTTI-registered ScriptableTweak, native TweakDBManager.SetFlat).
// The value is a tweak flat created by tweakxl.yaml, so SetFlat only updates it.
public class CpAutotestScriptTweak extends ScriptableTweak {
    protected cb func OnApply() -> Void {
        TweakDBManager.SetFlat(t"CpAutotest.ScriptFlat", 42);
    }
}
