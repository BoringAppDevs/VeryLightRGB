namespace RgbTray.Hardware;

internal static class NativeCompatibility
{
    public const string Board = "MAG B850M MORTAR WIFI (MS-7E61)";
    public static bool SupportsBoard(string? product) => product == Board;
    public static bool SupportsGpu(uint device, uint subsystem) => device == 0x2C0210DE && subsystem == 0xF32310B0;
    public static DeviceDescriptor Header(int h) => new($"native:msi:argb:{h}", $"MSI · ARGB {h + 1}", 12, 120, true, false, ZoneIndex: h);
    public static DeviceDescriptor Gpu => new("native:gainward:5080:phoenix", "Gainward RTX 5080 Phoenix", 1, 1, false, true);
}

internal sealed class NativeSession : IHardwareSession
{
    private HidDevice? hid;
    private GainwardLighting? gpu;
    public static DiscoveryResult Discover()
    {
        var devices = new List<DeviceDescriptor>();
        var notes = new List<string>();
        var board = Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS", "BaseBoardProduct", "") as string;
        if (NativeCompatibility.SupportsBoard(board))
        {
            try { using var probe = HidDevice.OpenMortar(); devices.AddRange(Enumerable.Range(0, 3).Select(NativeCompatibility.Header)); }
            catch (Exception ex) { notes.Add("MSI: " + ex.Message); }
        }
        else notes.Add($"Материнська плата «{board}» не має перевіреного нативного модуля.");
        try { using var probe = new GainwardLighting(); devices.Add(NativeCompatibility.Gpu); }
        catch (Exception ex) { notes.Add("GPU: " + ex.Message); }
        return new(devices, devices.Count > 0 ? "Нативне керування. " + string.Join(" ", notes) : string.Join(" ", notes) + " Для іншого обладнання вибери OpenRGB у налаштуваннях.");
    }
    public LightingOutput Open(TargetPlan target, byte[] initial)
    {
        if (target.Device.Id == NativeCompatibility.Gpu.Id)
        {
            gpu ??= new GainwardLighting();
            gpu.Apply(Color.FromArgb(initial[0], initial[1], initial[2]));
            return new GpuOutput(target, gpu);
        }
        int header = target.Device.ZoneIndex;
        if (header is < 0 or > 2 || target.Device.Id != NativeCompatibility.Header(header).Id) throw new NotSupportedException("Невідомий нативний пристрій.");
        hid ??= HidDevice.OpenMortar();
        if (target.Animated)
        {
            WriteZone(header, Color.Black, null);
            WriteZone(header, Color.Black, target.LedCount);
        }
        else WriteZone(header, Color.FromArgb(initial[0], initial[1], initial[2]), null);
        var output = new HeaderOutput(target, this, header);
        if (target.Animated) output.Send(initial);
        return output;
    }
    private void WriteZone(int header, Color color, int? directCount)
    {
        var config = MsiLighting.ZoneConfig(hid!.Read(0x50, 290), header, color, directCount);
        hid.Write(config);
        for (int attempt = 0; attempt < 6; attempt++)
        {
            if (attempt > 0) Thread.Sleep(15);
            var actual = hid.Read(0x50, 290);
            if (actual.AsSpan(1 + header * 16, 16).SequenceEqual(config.AsSpan(1 + header * 16, 16))) return;
        }
        throw new IOException($"MSI ARGB {header + 1}: конфігурацію не підтверджено.");
    }
    public void CheckState() => Lighting.CheckConflict();
    public void Dispose() { hid?.Dispose(); gpu?.Dispose(); }
    private sealed class HeaderOutput : LightingOutput
    {
        private readonly NativeSession owner;
        private readonly int header;
        private readonly byte[] packet;
        public HeaderOutput(TargetPlan plan, NativeSession owner, int header) : base(plan)
        { this.owner = owner; this.header = header; packet = MsiLighting.FramePacket(header, new byte[plan.LedCount * 3]); }
        public override void Send(ReadOnlySpan<byte> rgb)
        {
            if (rgb.Length != Plan.LedCount * 3) throw new InvalidDataException("Змінилася довжина кадру MSI.");
            rgb.CopyTo(packet.AsSpan(7)); owner.hid!.Write(packet); _ = owner.hid.Read(0x51, 727, allowZoneResponse: true);
        }
        public override void Restore(Color color) => owner.WriteZone(header, color, null);
    }
    private sealed class GpuOutput : LightingOutput
    {
        private readonly GainwardLighting gpu;
        public GpuOutput(TargetPlan plan, GainwardLighting gpu) : base(plan) => this.gpu = gpu;
        public override void Send(ReadOnlySpan<byte> rgb) => gpu.WriteFrame(Color.FromArgb(rgb[0], rgb[1], rgb[2]));
        public override void Restore(Color color) => gpu.Apply(color);
    }
}
