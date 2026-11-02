using RgbTray.Hardware;

namespace RgbTray.Effects;

// Compatibility facade for the original CLI/test API; playback uses one runtime.
internal sealed record EffectOptions(Color Color, int LedCount, int StepMilliseconds, LightingEffect Effect = LightingEffect.SequentialFill)
{
    public string Name => EffectCatalog.Name(Effect);
    public int Validate() => Effect switch
    {
        LightingEffect.SequentialFill => SequentialFill.CycleMilliseconds(LedCount, StepMilliseconds),
        LightingEffect.CircularDarkBand => CircularDarkBand.CycleMilliseconds(LedCount, StepMilliseconds),
        _ => throw new ArgumentException("Unknown legacy animated effect.", nameof(Effect))
    };
    public Color Render(double ms, Span<byte> rgb) => Effect == LightingEffect.CircularDarkBand
        ? CircularDarkBand.Render(ms, Color, LedCount, StepMilliseconds, rgb)
        : SequentialFill.Render(ms, Color, LedCount, StepMilliseconds, rgb);
}
internal sealed class EffectRunner
{
    private readonly LightingRuntime runtime;
    public Exception? Error => runtime.Error;
    public Task<List<DeviceResult>> Ready => runtime.Ready;
    public Task Completion => runtime.Completion;
    public long Frames => runtime.Frames;
    public EffectRunner(EffectOptions options, Action<Exception> onError)
    {
        _ = options.Validate();
        var style = new LightingStyle { Color = $"#{options.Color.R:X2}{options.Color.G:X2}{options.Color.B:X2}", Brightness = 100, Effect = options.Effect, StepMilliseconds = options.StepMilliseconds };
        var targets = Enumerable.Range(0, 3).Select(h => new TargetPlan(NativeCompatibility.Header(h), style.Copy(), options.LedCount, false, 0)).ToList();
        targets.Add(new(NativeCompatibility.Gpu, style.Copy(), 1, false, 0, options.LedCount));
        runtime = new(new(HardwareBackend.Native, 6742, 30, targets), results =>
        {
            var failure = results.FirstOrDefault(r => !r.Success);
            if (failure is not null) onError(new IOException(failure.Message));
        });
    }
    public Task StopAsync(Color? color = null) => runtime.StopAsync(color);
}
