namespace RgbTray.Effects;

internal static class SequentialFill
{
    public const int DarkMilliseconds = 200;
    public const int FullMilliseconds = 350;

    public static int CycleMilliseconds(int count, int stepMilliseconds)
    {
        if (count is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(count));
        if (stepMilliseconds is < 100 or > 1000) throw new ArgumentOutOfRangeException(nameof(stepMilliseconds));
        return DarkMilliseconds + 2 * count * stepMilliseconds + FullMilliseconds;
    }

    // Fill one LED at a time, then drain in the same order without a hard reset.
    // Wall-clock phase means slow USB frames never slow down or queue the effect.
    public static Color Render(double elapsedMilliseconds, Color color, int count, int stepMilliseconds, Span<byte> rgb)
    {
        int cycle = CycleMilliseconds(count, stepMilliseconds);
        if (rgb.Length != count * 3) throw new ArgumentException("RGB buffer must match LED count.", nameof(rgb));
        double phase = Math.Max(0, elapsedMilliseconds) % cycle;
        double drainStart = DarkMilliseconds + count * stepMilliseconds + FullMilliseconds;
        bool draining = phase >= drainStart;
        double progress = Math.Max(0, (phase - (draining ? drainStart : DarkMilliseconds)) / stepMilliseconds);
        double total = 0;
        for (int led = 0; led < count; led++)
        {
            double x = Math.Clamp(progress - led, 0, 1);
            double level = x * x * (3 - 2 * x);
            if (draining) level = 1 - level;
            total += level;
            rgb[led * 3] = Scale(color.R, level);
            rgb[led * 3 + 1] = Scale(color.G, level);
            rgb[led * 3 + 2] = Scale(color.B, level);
        }
        double gpuLevel = total / count; // The Phoenix exposes one RGB zone.
        return Color.FromArgb(Scale(color.R, gpuLevel), Scale(color.G, gpuLevel), Scale(color.B, gpuLevel));
    }
    private static byte Scale(byte value, double level) => (byte)Math.Round(value * level, MidpointRounding.AwayFromZero);
}
