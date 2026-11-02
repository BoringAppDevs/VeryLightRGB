using RgbTray.Hardware;

namespace RgbTray;

internal static class StartupRecovery
{
    private static readonly int[] RetryMilliseconds = { 2000, 5000, 10000, 20000, 30000, 30000 };
    public static int RetryDelay(int completedRetries) => completedRetries >= 0 && completedRetries < RetryMilliseconds.Length ? RetryMilliseconds[completedRetries] : 0;
    public static bool MissingManagedDevices(Settings settings, IReadOnlyList<DeviceDescriptor> devices)
    {
        string prefix = settings.Backend == HardwareBackend.Native ? "native:" : "openrgb:";
        var found = devices.Select(d => d.Id).ToHashSet(StringComparer.Ordinal);
        return settings.Devices.Any(d => d.Managed && d.Id.StartsWith(prefix, StringComparison.Ordinal) && !found.Contains(d.Id));
    }
}
