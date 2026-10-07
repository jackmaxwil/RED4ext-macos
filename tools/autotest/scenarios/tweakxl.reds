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
}
