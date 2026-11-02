namespace RgbTray.Hardware;

internal sealed record DeviceDescriptor(string Id, string Name, int LedCount, int MaxLeds, bool ConfigurableLength, bool SingleZone, int ControllerIndex = -1, int ZoneIndex = -1)
{
    public override string ToString() => Name;
}
internal sealed record DiscoveryResult(IReadOnlyList<DeviceDescriptor> Devices, string Description);
internal sealed record TargetPlan(DeviceDescriptor Device, LightingStyle Style, int LedCount, bool Reverse, int Offset, int VirtualLedCount = 12)
{
    public bool Animated => Style.Enabled && (Style.Brightness > 0 || Style.Effect == LightingEffect.Rainbow) && Style.Effect != LightingEffect.Solid && !(Device.SingleZone && Style.Effect == LightingEffect.CircularDarkBand);
}
internal sealed record RunPlan(HardwareBackend Backend, int Port, int FramesPerSecond, IReadOnlyList<TargetPlan> Targets)
{
    public static RunPlan Create(Settings settings, IEnumerable<DeviceDescriptor> devices)
    {
        settings.Validate();
        var targets = new List<TargetPlan>();
        foreach (var descriptor in devices)
        {
            var config = settings.GetDevice(descriptor);
            if (!config.Managed) continue;
            int count = descriptor.ConfigurableLength ? Math.Clamp(config.LedCount, 1, descriptor.MaxLeds) : descriptor.LedCount;
            var style = (config.UseGlobal ? settings : config.Style).Copy();
            if (!settings.Enabled) style.Enabled = false;
            var named = string.IsNullOrWhiteSpace(config.Name) ? descriptor : descriptor with { Name = config.Name };
            targets.Add(new(named, style, count, config.Reverse, config.Offset % count, settings.LedCount));
        }
        return new(settings.Backend, settings.OpenRgbPort, settings.FramesPerSecond, targets);
    }
}

internal abstract class LightingOutput : IDisposable
{
    public TargetPlan Plan { get; }
    protected LightingOutput(TargetPlan plan) => Plan = plan;
    public abstract void Send(ReadOnlySpan<byte> rgb);
    public abstract void Restore(Color color);
    public virtual void Dispose() { }
}
internal interface IHardwareSession : IDisposable
{
    LightingOutput Open(TargetPlan target, byte[] initial);
    void CheckState();
}
internal static class HardwareFactory
{
    public static DiscoveryResult Discover(HardwareBackend backend, int port) => backend == HardwareBackend.Native ? NativeSession.Discover() : OpenRgbSession.Discover(port);
    public static IHardwareSession Open(RunPlan plan) => plan.Backend == HardwareBackend.Native ? new NativeSession() : new OpenRgbSession(plan.Port);
}
