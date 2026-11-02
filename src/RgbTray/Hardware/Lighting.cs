using System.Diagnostics;

namespace RgbTray.Hardware;

internal sealed record DeviceResult(string Device, bool Success, string Message, string? Id = null);

internal static class Lighting
{
    public static void CheckConflict()
    {
        foreach (var name in new[] { "SignalRgb", "OpenRGB", "LedKeeper", "LEDKeeper2", "LightKeeperService", "LightKeeperService2" })
        {
            var processes = Process.GetProcessesByName(name);
            try { if (processes.Length != 0) throw new InvalidOperationException($"Закрий {name}, щоб звільнити керування RGB."); }
            finally { foreach (var process in processes) process.Dispose(); }
        }
    }
    public static T Exclusive<T>(Func<T> action, bool checkNativeConflict = true)
    {
        if (checkNativeConflict) CheckConflict();
        using var mutex = new Mutex(false, @"Local\RgbTray.Hardware");
        bool owned;
        try { owned = mutex.WaitOne(0); }
        catch (AbandonedMutexException) { owned = true; }
        if (!owned) throw new IOException("RGB зайняте. Зупини активний ефект перед діагностикою або одноразовою командою.");
        try
        {
            return action();
        }
        finally { mutex.ReleaseMutex(); }
    }
    public static List<DeviceResult> Apply(Color color) => Exclusive(() =>
    {
            var results = new List<DeviceResult>();
            void Run(string name, Action action)
            {
                try { action(); results.Add(new(name, true, "Колір записано та перевірено")); }
                catch (Exception ex) { results.Add(new(name, false, ex.Message)); }
            }
            Run("MSI · 3 ARGB", () => MsiLighting.Apply(color));
            Run("Gainward · Phoenix", () => { using var gpu = new GainwardLighting(); gpu.Apply(color); });
            foreach (var r in results) Log.Write($"{r.Device}: {r.Success} {r.Message} RGB={color.R:X2}{color.G:X2}{color.B:X2}");
            return results;
    });
}
