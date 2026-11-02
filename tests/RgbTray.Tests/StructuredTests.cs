using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Drawing;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using RgbTray;
using RgbTray.Effects;
using RgbTray.Hardware;

internal static class StructuredTests
{
    public static void Run(Action<bool, string> check)
    {
        void Rejected(Action action, string message)
        {
            bool failed = false;
            try { action(); }
            catch (Exception ex) when (ex is ArgumentException or FormatException or InvalidDataException or JsonException or IOException or DecoderFallbackException or NotSupportedException) { failed = true; }
            check(failed, message);
        }
        var settings = Settings.Deserialize("{\"Color\":\"#27C8FF\",\"Brightness\":80,\"Enabled\":true,\"Effect\":2,\"LedCount\":12,\"StepMilliseconds\":200}");
        check(settings.Color == "#27C8FF" && settings.Effect == LightingEffect.CircularDarkBand && settings.Devices.Count == 0, "v0.3 preferences did not migrate.");
        var header = NativeCompatibility.Header(0); var gpu = NativeCompatibility.Gpu;
        var h = settings.GetDevice(header); var g = settings.GetDevice(gpu);
        check(!h.Managed, "An available ARGB header must not be mistaken for an occupied header."); h.Managed = true;
        g.UseGlobal = false; g.HasCustomStyle = true; g.Style = new() { Color = "#FF0000", Brightness = 100, Effect = LightingEffect.Solid };
        var plan = RunPlan.Create(settings, new[] { header, gpu });
        check(plan.Targets[0].Style.Color == "#27C8FF" && plan.Targets[1].Style.Color == "#FF0000", "Global and individual colors are not independent.");
        settings.Color = "#FFFFFF"; g.Style.Color = "#00FF00";
        check(plan.Targets[0].Style.Color == "#27C8FF" && plan.Targets[1].Style.Color == "#FF0000", "Running plan is not an immutable preference snapshot.");
        var json = JsonSerializer.Serialize(settings);
        var roundtrip = Settings.Deserialize(json);
        check(roundtrip.Color == "#FFFFFF" && roundtrip.Devices.Single(d => d.Id == gpu.Id).Style.Color == "#00FF00", "Preference roundtrip lost independent colors.");
        roundtrip.Devices[0].Name = "Changed outside";
        check(settings.Devices[0].Name != "Changed outside", "Loaded settings share mutable device settings.");
        check(!JsonDocument.Parse(json).RootElement.TryGetProperty("Profiles", out _), "Removed profiles must not be saved.");
        settings.Enabled = false;
        check(RunPlan.Create(settings, new[] { header, gpu }).Targets.All(t => !t.Style.Enabled && !t.Animated), "Master off did not disable an independent device.");
        settings.Enabled = true; settings.GetDevice(gpu).Managed = false;
        check(RunPlan.Create(settings, new[] { header, gpu }).Targets.Count == 1, "Unmanaged device must not enter the output plan.");
        settings.GetDevice(header).UseGlobal = false; settings.GetDevice(header).Style.Effect = LightingEffect.Breathing;
        settings.Effect = LightingEffect.Solid;
        check(RunPlan.Create(settings, new[] { header }).Targets[0].Animated, "A device-specific animation did not run independently of a static shared style.");
        Rejected(() => Settings.Deserialize("{\"Backend\":99}"), "Unknown backend accepted.");
        Rejected(() => Settings.Deserialize("{\"Color\":null}"), "Null color accepted.");
        Rejected(() => Settings.Deserialize("{\"Devices\":null}"), "Null device collection accepted.");
        Rejected(() => Settings.Deserialize("{\"DarkWidth\":0}"), "Invalid dark width accepted.");
        Rejected(() => Settings.Deserialize("{\"Devices\":[{\"Id\":\"x\"},{\"Id\":\"x\"}]}"), "Duplicate device IDs accepted.");
        Rejected(() => Settings.Deserialize(new string(' ', 2 * 1024 * 1024 + 1)), "Oversized settings accepted.");
        check(NativeCompatibility.SupportsBoard("MAG B850M MORTAR WIFI (MS-7E61)") && !NativeCompatibility.SupportsBoard("Other MSI board") && !NativeCompatibility.SupportsBoard(null), "Native board allowlist is too broad.");
        check(NativeCompatibility.SupportsGpu(0x2C0210DE, 0xF32310B0) && !NativeCompatibility.SupportsGpu(0x2C0210DE, 0x123410B0), "Unknown GPU subsystem accepted for native writes.");

        var report = new byte[290]; report[0] = 0x50; report[1] = report[17] = report[33] = 9; report[16] = 12; report[32] = 16; report[48] = 24; report[170] = 123;
        var zone = MsiLighting.ZoneConfig(report, 1, Color.FromArgb(10, 20, 30));
        for (int i = 0; i < 289; i++) if (i < 17 || i > 32) check(zone[i] == report[i], "Changing one header modified an unmanaged zone.");
        check(zone[17] == 2 && zone[32] == 16 && zone[289] == 0, "Static zone configuration changed length or flash flag.");
        var direct = MsiLighting.ZoneConfig(report, 0, Color.Black, 12);
        check(direct[1] == 9 && direct[16] == 12 && direct[32] == 16 && direct[170] == 123, "Independent direct mode changed unrelated data.");
        Rejected(() => MsiLighting.ZoneConfig(report, 3, Color.White), "Unknown native header accepted.");

        foreach (var e in Enum.GetValues<LightingEffect>())
        {
            var style = new LightingStyle { Color = "#C65CFF", SecondaryColor = "#27C8FF", Brightness = 80, Effect = e, StepMilliseconds = 200, DarkWidth = 3 };
            foreach (int count in new[] { 1, 12, 16, 200 })
            {
                var descriptor = new DeviceDescriptor("test", "Test", count, count, false, count == 1);
                var r = new FrameRenderer(new(descriptor, style.Copy(), count, false, 0));
                foreach (var ms in new[] { 0.0, 100.0, 250.0, 10000.0 }) check(r.Render(ms).Length == count * 3, "Renderer returned an invalid buffer length.");
                var off = style.Copy(); off.Enabled = false;
                check(new FrameRenderer(new(descriptor, off, count, false, 0)).Render(100).All(v => v == 0), "Disabled device still emits animated light.");
            }
        }
        var spatial = new LightingStyle { Color = "#FFFFFF", Brightness = 100, Effect = LightingEffect.CircularDarkBand };
        var normal = new FrameRenderer(new(header, spatial, 12, false, 0)).Render(0);
        var flipped = new FrameRenderer(new(header, spatial, 12, true, 0)).Render(0);
        check(Enumerable.Range(0, 12).All(i => normal.AsSpan(i * 3, 3).SequenceEqual(flipped.AsSpan((11 - i) * 3, 3))), "Reverse mapping is incorrect.");
        var shifted = new FrameRenderer(new(header, spatial, 12, false, 3)).Render(0);
        check(Enumerable.Range(0, 12).All(i => shifted.AsSpan(i * 3, 3).SequenceEqual(normal.AsSpan(((i + 3) % 12) * 3, 3))), "Start offset is incorrect.");
        check(new FrameRenderer(new(gpu, spatial, 1, false, 0)).Render(500).SequenceEqual(new byte[] { 255, 255, 255 }), "Spatial band should remain steady on a one-zone GPU.");
        var rainbow = new LightingStyle { Effect = LightingEffect.Rainbow, Brightness = 0, StepMilliseconds = 100, Color = "#FF0000" };
        var fixedRainbow = new FrameRenderer(new(header, rainbow, 12, false, 0)).Render(1200);
        rainbow.Brightness = 100; rainbow.StepMilliseconds = 1000; rainbow.Color = "#00FF00";
        check(fixedRainbow.SequenceEqual(new FrameRenderer(new(header, rainbow, 12, false, 0)).Render(1200)), "Rainbow depends on hidden color, brightness or speed settings.");
        check(new TargetPlan(header, new() { Effect = LightingEffect.Rainbow, Brightness = 0 }, 12, false, 0).Animated, "Fixed rainbow stopped because a hidden brightness setting was zero.");
        var controller = ControllerData(new[] { ("ARGB 1", 12), ("ARGB 2", 5) });
        var parsed = OpenRgbClient.ParseController(controller, 0);
        check(parsed.Count == 2 && parsed[0].LedCount == 12 && parsed[1].LedCount == 5, "OpenRGB v3 zones parsed incorrectly.");
        var reordered = OpenRgbClient.ParseController(ControllerData(new[] { ("ARGB 2", 5), ("ARGB 1", 12) }), 7);
        check(parsed[0].Id == reordered[1].Id && parsed[1].Id == reordered[0].Id && reordered[1].ControllerIndex == 7, "Device IDs depend on transient controller or zone indexes.");
        Rejected(() => OpenRgbClient.ParseController(controller[..^1], 0), "Truncated OpenRGB data accepted.");
        Rejected(() => OpenRgbClient.ParseController(ControllerData(new[] { ("Same", 1), ("Same", 1) }), 0), "Ambiguous zones accepted.");
        var wire = OpenRgbClient.ZonePacket(2, new byte[] { 10, 20, 30, 40, 50, 60 });
        check(wire.Length == 18 && BinaryPrimitives.ReadUInt32LittleEndian(wire) == 18 && BinaryPrimitives.ReadInt32LittleEndian(wire.AsSpan(4)) == 2 && wire[8] == 2, "OpenRGB zone envelope is incorrect.");
        check(wire.AsSpan(10).SequenceEqual(new byte[] { 10, 20, 30, 0, 40, 50, 60, 0 }), "OpenRGB RGBx channel order is wrong.");
        Rejected(() => OpenRgbClient.ZonePacket(0, new byte[4]), "Partial OpenRGB RGB triple accepted.");

        using (var server = new TestServer(controller, fragment: true))
        using (var client = new OpenRgbClient(server.Port))
        {
            var actual = client.Discover(); check(actual.Count == 2, "SDK discovery over fragmented TCP failed.");
            client.SetCustomMode(0); client.WriteZone(actual[1], new byte[15]);
            check(server.FrameReady.Wait(2000) && server.FrameCount == 1 && server.CustomModes == 1, "SDK did not send the expected custom-mode/zone commands.");
            check(server.OtherWrites == 0, "SDK sent an unrequested resize, flash, or hardware-management command.");
        }
        using (var server = new TestServer(controller, notify: true))
        using (var client = new OpenRgbClient(server.Port))
        {
            var actual = client.Discover(); client.WriteZone(actual[0], new byte[36]);
            check(server.FrameReady.Wait(2000), "Fake SDK did not receive the initial frame."); Thread.Sleep(50);
            Rejected(() => client.WriteZone(actual[0], new byte[36]), "Device-list change did not invalidate output indexes.");
            Rejected(() => client.WriteZone(actual[0], new byte[36]), "Invalidated session reused old indexes on a subsequent write.");
            check(server.FrameCount == 1, "A stale device index was used after hotplug notification.");
        }
        using (var server = new TestServer(controller, malformed: true)) Rejected(() => { using var c = new OpenRgbClient(server.Port); }, "Invalid network signature accepted.");
        TestRuntime(check);
    }

    private static void TestRuntime(Action<bool, string> check)
    {
        var a = new DeviceDescriptor("a", "A", 12, 12, false, false);
        var b = new DeviceDescriptor("b", "B", 1, 1, false, true);
        var solid = new LightingStyle { Color = "#FF0000", Brightness = 100 };
        var breathe = new LightingStyle { Color = "#0000FF", Brightness = 100, Effect = LightingEffect.Breathing, StepMilliseconds = 100 };
        var session = new MemorySession();
        var runtime = new LightingRuntime(new(HardwareBackend.Native, 6742, 30, new[] { new TargetPlan(a, solid, 12, false, 0), new TargetPlan(b, breathe, 1, false, 0) }), _ => { }, _ => session);
        try
        {
            check(runtime.Ready.Wait(2000) && runtime.Ready.Result.All(r => r.Success), "Independent runtime startup failed."); Thread.Sleep(300);
            check(session.Outputs["a"].Writes.Count == 1 && session.Outputs["b"].Writes.Count > 1, "Static device was rewritten by another device's animation.");
        }
        finally { check(runtime.StopAsync(restoreColor: false).Wait(2000), "Runtime stop timed out."); }
        check(session.Outputs.Values.All(o => o.Restores == 0), "Preference change caused an unwanted full-color restore flash.");
        check(runtime.StopAsync().IsCompletedSuccessfully && session.Disposed, "Repeated stop leaked resources.");
        var partial = new MemorySession("a");
        var p = new LightingRuntime(new(HardwareBackend.Native, 6742, 30, new[] { new TargetPlan(a, solid, 12, false, 0), new TargetPlan(b, breathe, 1, false, 0) }), _ => { }, _ => partial);
        try
        {
            check(p.Ready.Wait(2000) && p.Ready.Result.Count(r => r.Success) == 1, "A missing device did not produce partial results."); Thread.Sleep(200);
            check(partial.Outputs["b"].Writes.Count > 1, "One missing device blocked a healthy device.");
        }
        finally { check(p.StopAsync(Color.Black).Wait(2000), "Partial runtime stop failed."); }
        check(partial.Outputs["b"].Restores == 1, "Healthy device did not restore on exit.");
        var idle = new MemorySession(); var s = new LightingRuntime(new(HardwareBackend.Native, 6742, 30, new[] { new TargetPlan(a, solid, 12, false, 0) }), _ => { }, _ => idle);
        check(s.Completion.Wait(2000) && idle.Outputs["a"].Writes.Count == 1 && idle.Disposed, "Static-only mode did not close its hardware session and worker.");
    }

    internal static byte[] ControllerData((string Name, int Leds)[] zones)
    {
        using var stream = new MemoryStream(); using var w = new BinaryWriter(stream, Encoding.UTF8, true);
        void Text(string value) { var bytes = Encoding.UTF8.GetBytes(value + '\0'); w.Write((ushort)bytes.Length); w.Write(bytes); }
        w.Write(0u); w.Write(1u); Text("Test board"); Text("Test vendor"); Text("Fake protocol server"); Text("1"); Text("serial-123"); Text("usb:123");
        w.Write((ushort)1); w.Write(0u); Text("Direct"); for (int i = 0; i < 12; i++) w.Write(i == 1 ? 0x20u : 0u); w.Write((ushort)0);
        w.Write((ushort)zones.Length);
        foreach (var z in zones) { Text(z.Name); w.Write(1u); w.Write(0u); w.Write((uint)z.Leds); w.Write((uint)z.Leds); w.Write((ushort)0); }
        int count = zones.Sum(z => z.Leds); w.Write((ushort)count);
        for (int led = 0; led < count; led++) { Text("LED " + led); w.Write((uint)led); }
        w.Write((ushort)count); for (int led = 0; led < count; led++) w.Write(0u);
        long length = stream.Length; stream.Position = 0; w.Write((uint)length); w.Flush(); return stream.ToArray();
    }
    private sealed class MemorySession : IHardwareSession
    {
        private readonly string? fail;
        public ConcurrentDictionary<string, MemoryOutput> Outputs { get; } = new();
        public bool Disposed { get; private set; }
        public MemorySession(string? fail = null) => this.fail = fail;
        public LightingOutput Open(TargetPlan target, byte[] initial)
        {
            if (target.Device.Id == fail) throw new IOException("Simulated missing device.");
            var output = new MemoryOutput(target); output.Send(initial); Outputs[target.Device.Id] = output; return output;
        }
        public void CheckState() { }
        public void Dispose() => Disposed = true;
    }
    private sealed class MemoryOutput : LightingOutput
    {
        public ConcurrentQueue<byte[]> Writes { get; } = new();
        public int Restores { get; private set; }
        public MemoryOutput(TargetPlan plan) : base(plan) { }
        public override void Send(ReadOnlySpan<byte> rgb) => Writes.Enqueue(rgb.ToArray());
        public override void Restore(Color color) => Restores++;
    }
    private sealed class TestServer : IDisposable
    {
        private readonly TcpListener listener = new(IPAddress.Loopback, 0);
        private readonly Task task;
        private TcpClient? accepted;
        private int frames, custom, other;
        public int Port { get; }
        public int FrameCount => Volatile.Read(ref frames);
        public int CustomModes => Volatile.Read(ref custom);
        public int OtherWrites => Volatile.Read(ref other);
        public ManualResetEventSlim FrameReady { get; } = new(false);
        public TestServer(byte[] controller, bool fragment = false, bool notify = false, bool malformed = false)
        {
            listener.Start(); Port = ((IPEndPoint)listener.LocalEndpoint).Port;
            task = Task.Run(() =>
            {
                try
                {
                    using var connection = listener.AcceptTcpClient(); accepted = connection;
                    using var stream = connection.GetStream(); stream.ReadTimeout = 2000;
                    void Reply(uint device, uint id, byte[] body)
                    {
                        var bytes = new byte[16 + body.Length]; Encoding.ASCII.GetBytes(malformed ? "FAIL" : "ORGB").CopyTo(bytes, 0);
                        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), device); BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8), id); BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(12), (uint)body.Length); body.CopyTo(bytes, 16);
                        if (fragment) { for (int pos = 0; pos < bytes.Length; pos += 3) stream.Write(bytes, pos, Math.Min(3, bytes.Length - pos)); }
                        else stream.Write(bytes);
                    }
                    while (true)
                    {
                        var header = new byte[16]; stream.ReadExactly(header); uint device = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4)); uint id = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8)); uint length = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(12));
                        if (length > 65536) throw new InvalidDataException("Unexpected request size."); var body = new byte[(int)length]; stream.ReadExactly(body);
                        if (id == 40) Reply(device, id, BitConverter.GetBytes(5u));
                        else if (id == 50) { }
                        else if (id == 0) Reply(device, id, BitConverter.GetBytes(1u));
                        else if (id == 1) Reply(device, id, controller);
                        else if (id == 1100) Interlocked.Increment(ref custom);
                        else if (id == 1051) { Interlocked.Increment(ref frames); if (notify) Reply(0, 100, Array.Empty<byte>()); FrameReady.Set(); }
                        else Interlocked.Increment(ref other);
                    }
                }
                catch (Exception ex) when (ex is IOException or SocketException or ObjectDisposedException or InvalidOperationException) { }
            });
        }
        public void Dispose() { accepted?.Dispose(); listener.Stop(); if (!task.Wait(3000)) throw new Exception("Fake SDK server did not stop."); FrameReady.Dispose(); }
    }
}
