module CpAutotest

// Archive load order. cp-regress points ArchiveXL at two test archives that both replace the English UI strings:
// "agerman.archive" (German) and "_french.archive" (French). Windows sorts folder names case-insensitively
// (upper-cased: "AGERMAN" < "_FRENCH") and the first archive wins a conflict, so German must win. A plain byte sort
// would put "_french" first.
public func CpRunMenuChecks(scenario: String) -> Void {
    let text = GetLocalizedText("UI-UserActions-Equip");
    CpCheck("first_archive_wins_windows_order", Equals(text, "Ausrüsten"), text);
}
