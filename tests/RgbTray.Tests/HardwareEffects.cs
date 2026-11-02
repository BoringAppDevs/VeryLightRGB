using System.Drawing;
using RgbTray;
using RgbTray.Effects;
using RgbTray.Hardware;

internal static class HardwareEffects
{
    public static void Run(Action<bool, string> check)
    {
        if (System.Diagnostics.Process.GetProcessesByName("RgbTray").Length != 0)
            throw new InvalidOperationException("Exit RGB Tray before running hardware tests.");
        var settings = Settings.Load();
        byte[] before;
        using (var hid = HidDevice.OpenMortar()) before = hid.Read(0x50, 290);
        try
        {
            // Exercise the per-device path rather than the legacy all-header wrapper.
            var headerDevice = NativeCompatibility.Header(0); var gpuDevice = NativeCompatibility.Gpu;
            var headerStyle = new LightingStyle { Color = "#204060", Brightness = 100 };
            var gpuStyle = new LightingStyle { Color = "#603020", Brightness = 100 };
            var independent = new LightingRuntime(new(HardwareBackend.Native, 6742, 30, new[] { new TargetPlan(headerDevice, headerStyle, 12, false, 0), new TargetPlan(gpuDevice, gpuStyle, 1, false, 0) }), _ => { });
            check(independent.Ready.Wait(5000) && independent.Ready.Result.All(r => r.Success), "Per-device static hardware setup failed.");
            check(independent.Completion.Wait(5000), "Static per-device worker did not release hardware.");
            using (var hid = HidDevice.OpenMortar())
            {
                var actual = hid.Read(0x50, 290);
                check(actual.AsSpan(17, 272).SequenceEqual(before.AsSpan(17, 272)), "Changing one selected header modified unselected native zones.");
                check(actual[2] == 0x20 && actual[3] == 0x40 && actual[4] == 0x60, "Selected header did not receive its independent color.");
            }
            using (var gpu = new GainwardLighting())
                check(gpu.Register(0x6C) == 0x60 && gpu.Register(0x6D) == 0x30 && gpu.Register(0x6E) == 0x20, "GPU did not receive a different independent color.");
            var runner = new EffectRunner(new(Color.FromArgb(40, 60, 80), 15, 180), _ => { });
            try
            {
                check(runner.Ready.Wait(5000), "Effect initialization timed out.");
                Thread.Sleep(500);
                bool blocked = false;
                try { Lighting.Exclusive(() => true); }
                catch (IOException) { blocked = true; }
                check(blocked, "Live effect must exclude competing hardware operations.");
                check(runner.Frames > 2, "Expected multiple successful frame exchanges.");
            }
            finally { check(runner.StopAsync(Color.Black).Wait(5000), "Effect stop timed out."); }
            check(runner.Error is null, "Effect failed during live test: " + runner.Error?.Message);
            check(Lighting.Exclusive(() => true), "Hardware mutex was not released on stop.");
            check(runner.StopAsync().IsCompletedSuccessfully, "Repeated stop must be safe.");
            byte[] after;
            using (var hid = HidDevice.OpenMortar()) after = hid.Read(0x50, 290);
            for (int h = 0; h < 3; h++)
            {
                int offset = 1 + h * 16;
                check(after[offset] == 2, "Stop should return MSI to hardware steady mode.");
                check(after.AsSpan(offset + 1, 12).ToArray().All(v => v == 0), "Stop(black) should leave all MSI headers dark.");
            }
            check(after.AsSpan(49, 240).SequenceEqual(before.AsSpan(49, 240)), "Animation touched unrelated MSI zones.");
            using (var gpu = new GainwardLighting())
                check(gpu.Register(0x6C) == 0 && gpu.Register(0x6D) == 0 && gpu.Register(0x6E) == 0, "Stop(black) should leave the GPU dark.");

            // Cancellation can arrive before the dedicated thread initializes HID.
            var immediate = new EffectRunner(new(Color.FromArgb(20, 20, 20), 15, 180), _ => { });
            check(immediate.StopAsync(Color.Black).Wait(5000), "Immediate cancellation timed out.");
            check(immediate.Error is null, "Immediate cancellation failed.");
            check(Lighting.Exclusive(() => true), "Immediate cancellation leaked the hardware mutex.");

            var circle = new EffectRunner(new(Color.FromArgb(30, 50, 70), 12, 100, LightingEffect.CircularDarkBand), _ => { });
            try
            {
                check(circle.Ready.Wait(5000), "Circular effect initialization timed out.");
                Thread.Sleep(500);
                check(circle.Frames > 2, "Circular effect did not exchange live frames.");
                check(circle.Error is null, "Circular effect failed: " + circle.Error?.Message);
                bool blocked = false;
                try { Lighting.Exclusive(() => true); }
                catch (IOException) { blocked = true; }
                check(blocked, "Circular effect must own the hardware mutex.");
            }
            finally { check(circle.StopAsync(Color.Black).Wait(5000), "Circular effect stop timed out."); }
            check(Lighting.Exclusive(() => true), "Circular effect leaked the hardware mutex.");
            using (var gpu = new GainwardLighting())
                check(gpu.Register(0x6C) == 0 && gpu.Register(0x6D) == 0 && gpu.Register(0x6E) == 0, "Circular effect should stop the GPU in black.");
        }
        finally
        {
            // Restore all prior native zones, including outputs excluded from settings.
            using (var hid = HidDevice.OpenMortar())
            {
                var previous = (byte[])before.Clone(); previous[^1] = 0; hid.Write(previous);
                bool confirmed = false;
                for (int i = 0; i < 6 && !confirmed; i++) { Thread.Sleep(15); confirmed = hid.Read(0x50, 290).AsSpan(1, 288).SequenceEqual(previous.AsSpan(1, 288)); }
                check(confirmed, "Could not restore native configuration after the hardware tests.");
            }
            var plan = RunPlan.Create(settings, NativeSession.Discover().Devices);
            var solid = plan with { Targets = plan.Targets.Select(t => t with { Style = new LightingStyle { Color = t.Style.Color, Brightness = t.Style.Brightness, Enabled = t.Style.Enabled } }).ToArray() };
            var restore = new LightingRuntime(solid, _ => { });
            var restored = restore.Ready.GetAwaiter().GetResult(); restore.Completion.GetAwaiter().GetResult();
            check(restored.All(r => r.Success), "Could not restore the saved per-device colors after hardware tests.");
        }
    }
}
