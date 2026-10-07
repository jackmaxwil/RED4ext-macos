// Global (no module): RED4ext registers this native under its bare name. Declaring it inside a module would make
// the game look for "<Module>.RED4ext_TestReport" and fail to initialize the script data.
native func RED4ext_TestReport(line: String) -> Void;
