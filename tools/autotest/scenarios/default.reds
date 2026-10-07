module CpAutotest

// Main-menu checks for scenarios without their own file in tools/autotest/scenarios/.
public func CpRunMenuChecks(scenario: String) -> Void {
    CpCheck("menu_reached", true, scenario);
}
