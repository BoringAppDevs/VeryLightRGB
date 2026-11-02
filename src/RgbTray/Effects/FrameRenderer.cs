using RgbTray.Hardware;

namespace RgbTray.Effects;

internal static class EffectCatalog
{
    public static readonly string[] Names = { "Сталий колір", "По черзі · заповнення", "По колу · темний блок", "Дихання", "Веселка", "Перехід двох кольорів" };
    public static string Name(LightingEffect effect) => Enum.IsDefined(effect) ? Names[(int)effect] : "Невідомий ефект";
}

internal sealed class FrameRenderer
{
    private readonly TargetPlan target;
    private readonly byte[] scratch;
    private readonly Color primary;
    private readonly Color secondary;
    public byte[] Frame { get; }
    public FrameRenderer(TargetPlan target)
    {
        target.Style.Validate();
        if (target.LedCount is < 1 or > 4096) throw new ArgumentOutOfRangeException(nameof(target));
        this.target = target;
        Frame = new byte[target.LedCount * 3];
        scratch = new byte[Math.Max(target.LedCount, target.VirtualLedCount) * 3];
        primary = target.Style.OutputColor();
        var other = target.Style.Copy(); other.Color = other.SecondaryColor; secondary = other.OutputColor();
    }
    public byte[] Render(double ms)
    {
        var style = target.Style;
        int count = target.LedCount;
        var work = scratch.AsSpan(0, count * 3);
        if (!style.Enabled || (style.Brightness == 0 && style.Effect != LightingEffect.Rainbow)) work.Clear();
        else switch (style.Effect)
        {
            case LightingEffect.Solid: Fill(work, primary); break;
            case LightingEffect.SequentialFill:
                if (target.Device.SingleZone)
                {
                    var average = SequentialFill.Render(ms, primary, target.VirtualLedCount, style.StepMilliseconds, scratch.AsSpan(0, target.VirtualLedCount * 3));
                    Fill(work, average);
                }
                else if (count <= 120) _ = SequentialFill.Render(ms, primary, count, style.StepMilliseconds, work);
                else RenderLargeFill(ms, work);
                break;
            case LightingEffect.CircularDarkBand:
                if (target.Device.SingleZone) Fill(work, primary);
                else if (count <= 120) _ = CircularDarkBand.Render(ms, primary, count, style.StepMilliseconds, work, style.DarkWidth);
                else RenderLargeBand(ms, work);
                break;
            case LightingEffect.Breathing:
                double level = (1 - Math.Cos(2 * Math.PI * Math.Max(0, ms) / (style.StepMilliseconds * 20.0))) / 2;
                Fill(work, Mix(Color.Black, primary, level)); break;
            case LightingEffect.ColorCycle:
                double mix = (1 - Math.Cos(2 * Math.PI * Math.Max(0, ms) / (style.StepMilliseconds * 20.0))) / 2;
                Fill(work, Mix(primary, secondary, mix)); break;
            case LightingEffect.Rainbow:
                for (int led = 0; led < count; led++)
                {
                    var c = Hue((Math.Max(0, ms) / 12000.0 + (double)led / count) % 1, 0.5);
                    work[led * 3] = c.R; work[led * 3 + 1] = c.G; work[led * 3 + 2] = c.B;
                }
                break;
            default: throw new InvalidDataException("Невідомий ефект.");
        }
        for (int led = 0; led < count; led++)
        {
            int source = target.Reverse ? count - 1 - led : led;
            source = (source + target.Offset) % count;
            work.Slice(source * 3, 3).CopyTo(Frame.AsSpan(led * 3, 3));
        }
        return Frame;
    }
    private void RenderLargeFill(double ms, Span<byte> rgb)
    {
        int count = target.LedCount, step = target.Style.StepMilliseconds;
        double phase = Math.Max(0, ms) % (550.0 + 2.0 * count * step);
        double drain = 200.0 + count * step + 350;
        bool draining = phase >= drain;
        double progress = Math.Max(0, (phase - (draining ? drain : 200)) / step);
        for (int led = 0; led < count; led++)
        {
            double x = Math.Clamp(progress - led, 0, 1), level = x * x * (3 - 2 * x);
            if (draining) level = 1 - level;
            var c = Mix(Color.Black, primary, level); rgb[led * 3] = c.R; rgb[led * 3 + 1] = c.G; rgb[led * 3 + 2] = c.B;
        }
    }
    private void RenderLargeBand(double ms, Span<byte> rgb)
    {
        int count = target.LedCount;
        double position = Math.Max(0, ms) % ((double)count * target.Style.StepMilliseconds) / target.Style.StepMilliseconds;
        int start = (int)position; double x = position - start, blend = x * x * (3 - 2 * x);
        var edge = Mix(Color.Black, primary, blend);
        for (int led = 0; led < count; led++)
        {
            bool a = (led - start + count) % count < target.Style.DarkWidth;
            bool b = (led - start - 1 + 2 * count) % count < target.Style.DarkWidth;
            var c = a == b ? (a ? Color.Black : primary) : a ? edge : Color.FromArgb(primary.R - edge.R, primary.G - edge.G, primary.B - edge.B);
            rgb[led * 3] = c.R; rgb[led * 3 + 1] = c.G; rgb[led * 3 + 2] = c.B;
        }
    }
    private static void Fill(Span<byte> rgb, Color color)
    { for (int i = 0; i < rgb.Length; i += 3) { rgb[i] = color.R; rgb[i + 1] = color.G; rgb[i + 2] = color.B; } }
    internal static Color Mix(Color a, Color b, double fraction) => Color.FromArgb((int)Math.Round(a.R + (b.R - a.R) * fraction), (int)Math.Round(a.G + (b.G - a.G) * fraction), (int)Math.Round(a.B + (b.B - a.B) * fraction));
    private static Color Hue(double hue, double brightness)
    {
        double h = hue * 6, x = 1 - Math.Abs(h % 2 - 1);
        var (r, g, b) = (int)h switch { 0 => (1.0, x, 0.0), 1 => (x, 1.0, 0.0), 2 => (0.0, 1.0, x), 3 => (0.0, x, 1.0), 4 => (x, 0.0, 1.0), _ => (1.0, 0.0, x) };
        return Color.FromArgb((int)Math.Round(r * brightness * 255), (int)Math.Round(g * brightness * 255), (int)Math.Round(b * brightness * 255));
    }
}
