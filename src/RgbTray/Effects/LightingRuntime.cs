using System.Diagnostics;
using RgbTray.Hardware;

namespace RgbTray.Effects;

internal sealed class LightingRuntime
{
    private readonly RunPlan plan;
    private readonly Func<RunPlan, IHardwareSession> factory;
    private readonly Action<IReadOnlyList<DeviceResult>> changed;
    private readonly bool hardwareLease;
    private readonly ManualResetEventSlim stop = new(false);
    private readonly TaskCompletionSource<List<DeviceResult>> ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private bool finished, restore = true;
    private Color? stopColor;
    private long frames;
    public Exception? Error { get; private set; }
    public Task<List<DeviceResult>> Ready => ready.Task;
    public Task Completion => completion.Task;
    public long Frames => Interlocked.Read(ref frames);
    public LightingRuntime(RunPlan plan, Action<IReadOnlyList<DeviceResult>> changed, Func<RunPlan, IHardwareSession>? factory = null)
    {
        this.plan = plan; this.changed = changed; this.factory = factory ?? HardwareFactory.Open; hardwareLease = factory is null;
        if (!Enum.IsDefined(plan.Backend) || plan.FramesPerSecond is < 10 or > 60 || plan.Targets.Count > 256) throw new ArgumentException("Invalid lighting plan.");
        new Thread(Run) { IsBackground = true, Name = "RGB lighting", Priority = ThreadPriority.BelowNormal }.Start();
    }
    public Task StopAsync(Color? finalColor = null, bool restoreColor = true)
    {
        lock (stop) { if (!finished) { stopColor = finalColor; restore = restoreColor; stop.Set(); } }
        return completion.Task;
    }
    private void Run()
    {
        try { if (hardwareLease) Lighting.Exclusive(() => { Execute(); return true; }, plan.Backend == HardwareBackend.Native); else Execute(); }
        catch (Exception ex) { Error = ex; ready.TrySetException(ex); Log.Write("Lighting runtime: " + ex); }
        finally { ready.TrySetCanceled(); lock (stop) { finished = true; stop.Dispose(); } completion.TrySetResult(); }
    }
    private void Execute()
    {
        if (plan.Targets.Count == 0) { ready.TrySetResult(new()); return; }
        using var session = factory(plan);
        var outputs = new List<State>();
        var results = new List<DeviceResult>();
        bool animated = false;
        try
        {
            foreach (var target in plan.Targets)
            {
                if (stop.IsSet) break;
                try
                {
                    var renderer = new FrameRenderer(target);
                    var initial = renderer.Render(0);
                    var output = session.Open(target, initial);
                    outputs.Add(new(output, renderer, (byte[])initial.Clone()));
                    results.Add(new(target.Device.Name, true, target.Animated ? EffectCatalog.Name(target.Style.Effect) : "Сталий колір", target.Device.Id));
                    animated |= target.Animated;
                }
                catch (Exception ex) { results.Add(new(target.Device.Name, false, ex.Message, target.Device.Id)); Log.Write($"{target.Device.Id}: {ex.Message}"); }
            }
            ready.TrySetResult(results.ToList());
            if (!animated || outputs.Count == 0) return;
            Log.Write($"Lighting started: {outputs.Count} outputs; {plan.FramesPerSecond} FPS limit; {plan.Backend}.");
            var clock = Stopwatch.StartNew();
            double checkAt = 2000;
            while (!stop.IsSet && outputs.Any(o => o.Active && o.Output.Plan.Animated))
            {
                double began = clock.Elapsed.TotalMilliseconds;
                if (began >= checkAt) { session.CheckState(); checkAt = began + 2000; }
                bool sent = false;
                foreach (var state in outputs.Where(o => o.Active && o.Output.Plan.Animated))
                {
                    try
                    {
                        var rgb = state.Renderer.Render(began);
                        if (rgb.AsSpan().SequenceEqual(state.Previous)) continue;
                        state.Output.Send(rgb); rgb.CopyTo(state.Previous, 0); sent = true;
                    }
                    catch (Exception ex)
                    {
                        state.Active = false;
                        int index = results.FindIndex(r => r.Id == state.Output.Plan.Device.Id);
                        results[index] = new(state.Output.Plan.Device.Name, false, ex.Message, state.Output.Plan.Device.Id);
                        Log.Write($"Output stopped {state.Output.Plan.Device.Id}: {ex.Message}"); changed(results.ToList());
                    }
                }
                if (sent) Interlocked.Increment(ref frames);
                if (stop.Wait(Math.Max(1, (int)Math.Ceiling(1000.0 / plan.FramesPerSecond - (clock.Elapsed.TotalMilliseconds - began))))) break;
            }
        }
        catch (Exception ex)
        {
            Error = ex;
            if (!ready.Task.IsCompleted) ready.TrySetException(ex);
            var failed = results.Select(r => r.Success ? r with { Success = false, Message = ex.Message } : r).ToList();
            changed(failed); Log.Write("Session stopped: " + ex);
        }
        finally
        {
            bool doRestore; Color? final;
            lock (stop) { doRestore = restore; final = stopColor; }
            if (animated && doRestore)
            {
                try
                {
                    session.CheckState();
                    foreach (var state in outputs.Where(o => o.Active))
                        try { state.Output.Restore(final ?? state.Output.Plan.Style.OutputColor()); }
                        catch (Exception ex) { Log.Write("Restore failed: " + ex.Message); }
                }
                catch (Exception ex) { Log.Write("Restore skipped: " + ex.Message); }
            }
            foreach (var state in outputs) state.Output.Dispose();
            Log.Write($"Lighting session finished; changed frames: {Frames}.");
        }
    }
    private sealed class State
    {
        public LightingOutput Output { get; }
        public FrameRenderer Renderer { get; }
        public byte[] Previous { get; }
        public bool Active { get; set; } = true;
        public State(LightingOutput output, FrameRenderer renderer, byte[] previous) => (Output, Renderer, Previous) = (output, renderer, previous);
    }
}
