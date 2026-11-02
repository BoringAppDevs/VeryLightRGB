using Microsoft.Win32;
using RgbTray.Hardware;
using RgbTray.Effects;

namespace RgbTray;

internal sealed class TrayContext : ApplicationContext
{
    private readonly Settings settings;
    private readonly MainForm form;
    private readonly NotifyIcon tray;
    private readonly System.Windows.Forms.Timer debounce = new() { Interval = 250 };
    private readonly RegisteredWaitHandle showWait, exitWait;
    private bool busy, dirty, exiting, suspended, needsDiscovery = true;
    private int retry;
    private int revision;
    private LightingRuntime? runtime;
    private Task stopping = Task.CompletedTask;
    private DiscoveryResult discovery = new(Array.Empty<DeviceDescriptor>(), "Пошук пристроїв…");

    public TrayContext(EventWaitHandle show, EventWaitHandle exit, bool hidden)
    {
        settings = Settings.Load();
        form = new MainForm(settings); _ = form.Handle;
        form.ApplyRequested += QueueApply;
        form.StoreRequested += () => { try { settings.Save(); } catch (Exception ex) { form.SetStatus(ex.Message, false); } };
        form.DiscoveryRequested += (backend, port) => { settings.Backend = backend; settings.OpenRgbPort = port; needsDiscovery = true; QueueApply(); };
        form.IdentifyRequested += Identify;
        var menu = new ContextMenuStrip();
        menu.Items.Add("Відкрити RGB Tray…", null, (_, _) => form.Open());
        menu.Items.Add("Увімкнути / вимкнути все", null, (_, _) => form.Toggle());
        var effects = new ToolStripMenuItem("Спільний ефект");
        foreach (var value in Enum.GetValues<LightingEffect>()) effects.DropDownItems.Add(EffectCatalog.Name(value), null, (_, _) => form.SelectEffect(value));
        menu.Items.Add(effects);
        var colors = new ToolStripMenuItem("Спільний колір");
        foreach (var (name, color) in new[] { ("Теплий", "#FF8040"), ("М’ятний", "#64E6C6"), ("Білий", "#FFFFFF"), ("Фіолетовий", "#C65CFF") }) colors.DropDownItems.Add(name, null, (_, _) => form.SelectColor(color));
        menu.Items.Add(colors);
        menu.Items.Add("Оновити пристрої", null, (_, _) => { needsDiscovery = true; QueueApply(); });
        menu.Items.Add(new ToolStripSeparator()); menu.Items.Add("Вийти", null, (_, _) => RequestExit());
        tray = new NotifyIcon { Icon = MakeIcon(), Text = "RGB Tray", ContextMenuStrip = menu, Visible = true };
        form.Icon = tray.Icon; tray.DoubleClick += (_, _) => form.Open();
        debounce.Tick += async (_, _) => { debounce.Stop(); await ApplyAsync(); };
        showWait = ThreadPool.RegisterWaitForSingleObject(show, (_, _) => Dispatch(form.Open), null, Timeout.Infinite, false);
        exitWait = ThreadPool.RegisterWaitForSingleObject(exit, (_, _) => Dispatch(RequestExit), null, Timeout.Infinite, false);
        SystemEvents.PowerModeChanged += PowerChanged;
        if (!hidden) form.Open(); QueueApply();
    }
    private static Icon MakeIcon()
    {
        using var bmp = new Bitmap(32, 32); using var g = Graphics.FromImage(bmp);
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        using var brush = new SolidBrush(Color.FromArgb(100, 230, 198)); g.FillEllipse(brush, 3, 3, 26, 26);
        using var inner = new SolidBrush(Color.FromArgb(18, 21, 26)); g.FillEllipse(inner, 10, 10, 12, 12);
        var handle = bmp.GetHicon(); try { using var icon = Icon.FromHandle(handle); return (Icon)icon.Clone(); } finally { DestroyIcon(handle); }
    }
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr icon);
    private void Dispatch(Action action) { if (!exiting && !form.IsDisposed) { try { form.BeginInvoke(action); } catch (InvalidOperationException) { } } }
    private void PowerChanged(object sender, PowerModeChangedEventArgs e)
    {
        if (e.Mode == PowerModes.Suspend) Dispatch(async () => { suspended = true; debounce.Stop(); await StopAsync(Color.Black); });
        if (e.Mode == PowerModes.Resume) Dispatch(() => { suspended = false; needsDiscovery = true; retry = 0; debounce.Interval = 2000; debounce.Start(); });
    }
    private void QueueApply() { if (exiting) return; revision++; retry = 0; debounce.Stop(); debounce.Interval = 250; debounce.Start(); }
    private void RetryConnection()
    {
        int delay = StartupRecovery.RetryDelay(retry);
        if (exiting || suspended || delay == 0) return;
        retry++; needsDiscovery = true; debounce.Stop(); debounce.Interval = delay; debounce.Start();
        Log.Write($"Connection retry {retry} scheduled in {delay} ms; discovery will be repeated.");
    }
    private Task StopAsync(Color? color = null, bool restore = true)
    {
        var current = runtime; runtime = null;
        if (current is not null) stopping = current.StopAsync(color, restore);
        return stopping;
    }
    private async void RequestExit() { exiting = true; debounce.Stop(); if (!busy) { await StopAsync(); ExitThread(); } }
    private void Display(IReadOnlyList<DeviceResult> results)
    {
        bool ok = results.All(r => r.Success);
        form.SetResults(results);
        string text = results.Count == 0 ? "Вибери підключений вихід у вкладці «Пристрої»." : $"Керованих виходів: {results.Count(r => r.Success)} / {results.Count}. " + (settings.Enabled ? "Світло ввімкнене." : "Світло вимкнене.");
        if (!ok) { var failed = results.First(r => !r.Success); text += $"\n{failed.Device}: {failed.Message}"; }
        form.SetStatus(text, ok); tray.Text = ok ? $"RGB Tray · {(settings.Enabled ? "світло ввімкнене" : "вимкнено")}" : "RGB Tray · перевір пристрої";
    }
    private async Task ApplyAsync()
    {
        if (exiting || suspended) return;
        if (busy) { dirty = true; return; }
        busy = true; bool success = false; int requestedRevision = revision;
        try
        {
            await StopAsync(restore: false);
            if (exiting || suspended) return;
            if (needsDiscovery)
            {
                var backend = settings.Backend; int port = settings.OpenRgbPort;
                var found = await Task.Run(() => HardwareFactory.Discover(backend, port));
                if (requestedRevision != revision) { needsDiscovery = true; dirty = true; return; }
                discovery = found;
                needsDiscovery = false; form.SetDevices(discovery);
            }
            if (exiting || suspended) return;
            var plan = RunPlan.Create(settings, discovery.Devices); settings.Save();
            if (discovery.Devices.Count == 0) { form.SetStatus(discovery.Description, false); Log.Write("Discovery found no devices: " + discovery.Description); return; }
            bool missing = StartupRecovery.MissingManagedDevices(settings, discovery.Devices);
            LightingRuntime? started = null;
            started = new LightingRuntime(plan, results => Dispatch(() => { if (ReferenceEquals(runtime, started)) { Display(results); if (!busy && results.Any(r => !r.Success)) RetryConnection(); } }));
            runtime = started;
            var results = await started.Ready;
            if (started.Error is Exception error) throw new IOException("Не вдалося запустити підсвітку.", error);
            Display(results); success = !missing && results.All(r => r.Success);
            Log.Write($"Apply completed; backend={settings.Backend}; discovered={discovery.Devices.Count}; managed={results.Count}; successful={results.Count(r => r.Success)}; missingManaged={missing}.");
            if (missing) form.SetStatus("Не всі вибрані виходи готові. " + discovery.Description, false);
        }
        catch (Exception ex) { Log.Write(ex.ToString()); form.SetStatus(ex.Message, false); tray.Text = "RGB Tray · відкрий для деталей"; }
        finally
        {
            busy = false;
            if (exiting) { await StopAsync(); ExitThread(); }
            else if (dirty) { dirty = false; QueueApply(); }
            else if (!success) RetryConnection();
        }
    }
    private async void Identify(DeviceDescriptor device)
    {
        if (busy || exiting || suspended) return;
        busy = true; debounce.Stop();
        try
        {
            await StopAsync(restore: false);
            var config = settings.GetDevice(device);
            int count = device.ConfigurableLength ? Math.Clamp(config.LedCount, 1, device.MaxLeds) : device.LedCount;
            var target = new TargetPlan(device, new LightingStyle { Color = "#FFFFFF", Brightness = 25, Effect = LightingEffect.Breathing, StepMilliseconds = 100 }, count, false, 0);
            var identify = new LightingRuntime(new(settings.Backend, settings.OpenRgbPort, settings.FramesPerSecond, new[] { target }), _ => { });
            runtime = identify; Display(await identify.Ready);
            await Task.Delay(1200);
            await StopAsync(Color.Black);
        }
        catch (Exception ex) { form.SetStatus(ex.Message, false); Log.Write(ex.ToString()); }
        finally { busy = false; if (exiting) { await StopAsync(); ExitThread(); } else QueueApply(); }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            exiting = true; SystemEvents.PowerModeChanged -= PowerChanged; StopAsync().GetAwaiter().GetResult();
            showWait.Unregister(null); exitWait.Unregister(null); debounce.Dispose(); tray.Visible = false;
            var icon = tray.Icon; var menu = tray.ContextMenuStrip; tray.Dispose(); form.Dispose(); icon?.Dispose(); menu?.Dispose();
        }
        base.Dispose(disposing);
    }
}
