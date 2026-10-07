module CpAutotest

// ArchiveXL main-menu checks. Each one needs ArchiveXL loaded by RED4ext (every address it uses verified) and its
// native classes registered; nothing here needs a packed .archive.
public func CpRunMenuChecks(scenario: String) -> Void {
    // Natives on the ArchiveXL class: RTTI registration (RedLib registrars), CString return and argument.
    let version = ArchiveXL.Version();
    CpCheck("archivexl_version", StrLen(version) > 0, version);
    CpCheck("archivexl_require_old", ArchiveXL.Require("1.0.0"), "");
    CpCheck("archivexl_require_future", !ArchiveXL.Require("99.0.0"), "");

    // CName return with a null wref: the PuppetState fallback body type.
    let body = ArchiveXL.GetBodyType(null);
    CpCheck("archivexl_body_type", Equals(body, n"BaseBody"), NameToString(body));

    // Records ArchiveXL creates in TweakDB once it is loaded (PuppetState, from the LoadTweakDB hook). Their scripted
    // function names prove the record flats were written.
    let bodyFunc = TweakDBInterface.GetCName(t"itemsFactoryAppearanceSuffix.BodyType.scriptedFunction", n"");
    CpCheck("archivexl_suffix_body", Equals(bodyFunc, n"GetBodyTypeSuffix"), NameToString(bodyFunc));
    let feetFunc = TweakDBInterface.GetCName(t"itemsFactoryAppearanceSuffix.FeetState.scriptedFunction", n"");
    CpCheck("archivexl_suffix_feet", Equals(feetFunc, n"GetFeetStateSuffix"), NameToString(feetFunc));
    let armsFunc = TweakDBInterface.GetCName(t"itemsFactoryAppearanceSuffix.ArmsState.scriptedFunction", n"");
    CpCheck("archivexl_suffix_arms", Equals(armsFunc, n"GetArmsStateSuffix"), NameToString(armsFunc));
}
