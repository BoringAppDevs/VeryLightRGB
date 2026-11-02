namespace RgbTray.Effects;

internal static class CircularDarkBand
{
    public const int Width = 4;

    public static int CycleMilliseconds(int count, int stepMilliseconds)
    {
        if (count is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(count));
        if (stepMilliseconds is < 100 or > 1000) throw new ArgumentOutOfRangeException(nameof(stepMilliseconds));
        return count * stepMilliseconds;
    }

    public static Color Render(double elapsedMilliseconds, Color color, int count, int stepMilliseconds, Span<byte> rgb, int darkWidth = Width)
    {
        int cycle = CycleMilliseconds(count, stepMilliseconds);
        if (rgb.Length != count * 3) throw new ArgumentException("RGB buffer must match LED count.", nameof(rgb));
        double position = Math.Max(0, elapsedMilliseconds) % cycle / stepMilliseconds;
        int start = (int)Math.Floor(position);
        double fraction = position - start;
        double blend = fraction * fraction * (3 - 2 * fraction);
        if (darkWidth is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(darkWidth));
        int width = Math.Min(darkWidth, count);
        byte r = Scale(color.R, blend), g = Scale(color.G, blend), b = Scale(color.B, blend);
        for (int led = 0; led < count; led++)
        {
            bool darkNow = (led - start + count) % count < width;
            bool darkNext = (led - start - 1 + 2 * count) % count < width;
            int offset = led * 3;
            if (darkNow == darkNext)
            {
                rgb[offset] = darkNow ? (byte)0 : color.R;
                rgb[offset + 1] = darkNow ? (byte)0 : color.G;
                rgb[offset + 2] = darkNow ? (byte)0 : color.B;
            }
            else
            {
                // Complementary edge values preserve total light while the dark
                // block crosses LED 12 -> LED 1 as smoothly as any other boundary.
                rgb[offset] = darkNow ? r : (byte)(color.R - r);
                rgb[offset + 1] = darkNow ? g : (byte)(color.G - g);
                rgb[offset + 2] = darkNow ? b : (byte)(color.B - b);
            }
        }
        return color; // Phoenix is a single RGB zone; keep its logo steady.
    }

    private static byte Scale(byte value, double level) => (byte)Math.Round(value * level, MidpointRounding.AwayFromZero);
}
