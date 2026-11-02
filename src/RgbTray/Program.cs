using RgbTray.Hardware;
using RgbTray.Effects;
using System.Text.Json;

namespace RgbTray;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        int dataIndex = Array.IndexOf(args, "--data-dir");
        if (dataIndex >= 0) Settings.ConfigureDataDirectory(dataIndex + 1 < args.Length ? args[dataIndex + 1] : "");
        if (args.Contains("--autostart")) Log.Write($"Autostart entry; pid={Environment.ProcessId}; data={Settings.DataDirectory}; settingsExist={File.Exists(Path.Combine(Settings.DataDirectory, "settings.json"))}.");
        ApplicationConfiguration.Initialize();
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log.Write("Unhandled application failure: " + e.ExceptionObject);
        if (args.Contains("--exit"))
        {
            if (EventWaitHandle.TryOpenExisting(@"Local\RgbTray.Exit", out var request))
                using (request) request.Set();
            return 0;
        }
        if (args.Contains("--diagnose") || args.Contains("--set") || args.Contains("--off") || args.Contains("--test-effect") || args.Contains("--map-headers") || args.Contains("--enable-startup") || args.Contains("--disable-startup") || args.Contains("--startup-info")) return CommandLine(args);
        using var singleton = new Mutex(true, @"Local\RgbTray.Application", out var first);
        using var show = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\RgbTray.Show");
        using var exit = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\RgbTray.Exit");
        if (!first) { if (!args.Contains("--tray")) show.Set(); Log.Write("Duplicate launch skipped; existing RGB Tray instance owns this session."); return 0; }
        using var currentProcess = System.Diagnostics.Process.GetCurrentProcess();
        Log.Write($"Application starting; version={Application.ProductVersion}; pid={Environment.ProcessId}; session={currentProcess.SessionId}; autostart={args.Contains("--autostart")}; tray={args.Contains("--tray")}; executable={Environment.ProcessPath}.");
        try { using var context = new TrayContext(show, exit, args.Contains("--tray")); Application.Run(context); Log.Write("Application exited normally."); return 0; }
        catch (Exception ex) { Log.Write(ex.ToString()); if (!args.Contains("--autostart")) MessageBox.Show(ex.Message, "RGB Tray", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
    }

    private static int CommandLine(string[] args)
    {
        var output = Array.IndexOf(args, "--output");
        string path = output >= 0 && output + 1 < args.Length ? Path.GetFullPath(args[output + 1]) : Path.Combine(Settings.DataDirectory, "diagnostics.json");
        object result; int code = 0;
        try
        {
            if (args.Contains("--enable-startup") || args.Contains("--disable-startup") || args.Contains("--startup-info"))
            {
                if (!args.Contains("--startup-info")) Startup.Enabled = args.Contains("--enable-startup");
                result = new { Enabled = Startup.Enabled, ScheduledTask = Startup.TaskName, DataDirectory = Settings.DataDirectory,
                    LocalAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), LocalAppDataEnvironment = Environment.GetEnvironmentVariable("LOCALAPPDATA"),
                    UserProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), User = Environment.UserName, SettingsExist = File.Exists(Path.Combine(Settings.DataDirectory, "settings.json")) };
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
                return 0;
            }
            var current = Settings.Load();
            if (current.Backend == HardwareBackend.Native) Lighting.CheckConflict();
            if (args.Contains("--map-headers"))
            {
                var found = NativeSession.Discover();
                string[] colors = { "#800000", "#008000", "#000080" };
                var targets = found.Devices.Where(d => d.Id.StartsWith("native:msi:argb:", StringComparison.Ordinal))
                    .Select(d => new TargetPlan(d, new LightingStyle { Color = colors[d.ZoneIndex], Brightness = 100 }, 12, false, 0)).ToArray();
                var runner = new LightingRuntime(new(HardwareBackend.Native, 6742, 30, targets), _ => { });
                var results = runner.Ready.GetAwaiter().GetResult(); runner.Completion.GetAwaiter().GetResult();
                code = results.Count == 3 && results.All(r => r.Success) ? 0 : 2;
                result = new { Colors = new[] { "ARGB 1: red", "ARGB 2: green", "ARGB 3: blue" }, Devices = results };
            }
            else if (args.Contains("--test-effect"))
            {
                var settings = Settings.Load();
                var devices = HardwareFactory.Discover(settings.Backend, settings.OpenRgbPort);
                var runner = new LightingRuntime(RunPlan.Create(settings, devices.Devices), _ => { });
                List<DeviceResult> results;
                try { results = runner.Ready.GetAwaiter().GetResult(); runner.Completion.Wait(TimeSpan.FromSeconds(6)); }
                finally { runner.StopAsync().GetAwaiter().GetResult(); }
                if (runner.Error is Exception error) throw new IOException("Тест ефекту не пройшов.", error);
                code = results.Count > 0 && results.All(r => r.Success) ? 0 : 2;
                result = new { Timestamp = DateTimeOffset.Now, Backend = settings.Backend.ToString(), Devices = results, ChangedFrames = runner.Frames };
            }
            else if (args.Contains("--diagnose"))
            {
                if (current.Backend == HardwareBackend.OpenRgb)
                {
                    result = Lighting.Exclusive(() => HardwareFactory.Discover(current.Backend, current.OpenRgbPort), false);
                }
                else
                result = Lighting.Exclusive(() =>
                {
                    using var gpu = new GainwardLighting();
                    return new { Timestamp = DateTimeOffset.Now, Msi = MsiLighting.Diagnose(), Gainward = gpu.Diagnose() };
                });
            }
            else
            {
                int index = Array.IndexOf(args, "--set");
                var color = args.Contains("--off") ? Color.Black : Settings.ParseColor(index >= 0 && index + 1 < args.Length ? args[index + 1] : "");
                var found = HardwareFactory.Discover(current.Backend, current.OpenRgbPort);
                var plan = RunPlan.Create(current, found.Devices);
                var forced = plan with { Targets = plan.Targets.Select(t => t with { Style = new LightingStyle { Color = $"#{color.R:X2}{color.G:X2}{color.B:X2}", Brightness = 100 } }).ToArray() };
                var player = new LightingRuntime(forced, _ => { });
                List<DeviceResult> devices;
                try { devices = player.Ready.GetAwaiter().GetResult(); player.Completion.GetAwaiter().GetResult(); }
                finally { player.StopAsync().GetAwaiter().GetResult(); }
                code = devices.Count > 0 && devices.All(r => r.Success) ? 0 : 2;
                result = new { Timestamp = DateTimeOffset.Now, Color = $"#{color.R:X2}{color.G:X2}{color.B:X2}", Devices = devices };
            }
        }
        catch (Exception ex) { result = new { Error = ex.Message }; Log.Write(ex.ToString()); code = 1; }
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        return code;
    }
}
