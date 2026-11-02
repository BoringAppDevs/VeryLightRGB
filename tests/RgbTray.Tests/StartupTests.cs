using System.Xml.Linq;
using RgbTray;
using RgbTray.Hardware;

internal static class StartupTests
{
    public static void Run(Action<bool, string> check)
    {
        const string executable = @"C:\Apps & Utilities\RGB Tray\RgbTray.exe", sid = "S-1-5-21-123-456-789-1001";
        XNamespace ns = "http://schemas.microsoft.com/windows/2004/02/mit/task";
        var xml = XDocument.Parse(Startup.BuildTaskXml(executable, sid));
        string Value(string name) => xml.Descendants(ns + name).First().Value;
        check(Value("Command") == executable && Value("WorkingDirectory") == @"C:\Apps & Utilities\RGB Tray", "Startup executable path with spaces/XML characters is corrupted.");
        check(Value("Arguments") == $"--tray --autostart --data-dir \"{Settings.DataDirectory}\"", "Scheduled startup must select tray mode and an explicit user configuration directory.");
        check(xml.Descendants(ns + "UserId").All(e => e.Value == sid) && Value("LogonType") == "InteractiveToken" && Value("RunLevel") == "LeastPrivilege", "Startup runs in the wrong session or unnecessarily requests elevated privileges.");
        check(Value("Delay") == "PT10S" && Value("MultipleInstancesPolicy") == "IgnoreNew", "Startup delay or duplicate-instance policy is incorrect.");
        check(Value("Interval") == "PT1M" && Value("Count") == "3" && Value("ExecutionTimeLimit") == "PT0S", "Crash recovery or unlimited tray runtime is missing.");
        check(Value("DisallowStartIfOnBatteries") == "false" && Value("StopIfGoingOnBatteries") == "false" && Value("RunOnlyIfNetworkAvailable") == "false", "Unrelated power/network conditions prevent startup.");

        var settings = new Settings(); var header = NativeCompatibility.Header(0); var gpu = NativeCompatibility.Gpu;
        settings.GetDevice(header).Managed = true; _ = settings.GetDevice(gpu);
        check(StartupRecovery.MissingManagedDevices(settings, Array.Empty<DeviceDescriptor>()), "No devices at early logon was incorrectly treated as success.");
        check(StartupRecovery.MissingManagedDevices(settings, new[] { header }), "A late GPU did not request another discovery.");
        check(StartupRecovery.MissingManagedDevices(settings, new[] { gpu }), "A late motherboard did not request another discovery.");
        check(!StartupRecovery.MissingManagedDevices(settings, new[] { header, gpu }), "Healthy selected devices keep reconnecting unnecessarily.");
        _ = settings.GetDevice(NativeCompatibility.Header(1));
        settings.Devices.Add(new() { Id = "openrgb:other", Managed = true });
        check(!StartupRecovery.MissingManagedDevices(settings, new[] { header, gpu }), "Unselected headers or another backend caused spurious missing-device retries.");
        settings.Backend = HardwareBackend.OpenRgb;
        check(!StartupRecovery.MissingManagedDevices(settings, new[] { new DeviceDescriptor("openrgb:other", "Other", 1, 1, false, true) }), "Stored native settings interfered with SDK startup.");
        check(Enumerable.Range(0, 6).Select(StartupRecovery.RetryDelay).SequenceEqual(new[] { 2000, 5000, 10000, 20000, 30000, 30000 }), "Connection retry grace period is incorrect.");
        check(StartupRecovery.RetryDelay(6) == 0 && StartupRecovery.RetryDelay(-1) == 0, "Retry policy is not bounded.");
    }
}
