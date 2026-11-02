namespace RgbTray.Hardware;

internal static class MsiLighting
{
    public static byte[] ZoneConfig(byte[] current, int header, Color color, int? directCount = null)
    {
        if (header is < 0 or > 2) throw new ArgumentOutOfRangeException(nameof(header));
        if (directCount is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(directCount));
        if (current.Length != 290 || current[0] != 0x50) throw new ArgumentException("Expected MSI 290-byte zone report.");
        int o = 1 + header * 16;
        if (current[o] > 10) throw new InvalidDataException("Невідомий режим MSI; запис скасовано.");
        var result = (byte[])current.Clone();
        var template = directCount.HasValue ? DirectConfig(current, directCount.Value) : StaticConfig(current, color);
        template.AsSpan(o, 16).CopyTo(result.AsSpan(o, 16));
        result[^1] = 0;
        return result;
    }
    public static byte[] DirectConfig(byte[] current, int ledCount)
    {
        if (ledCount is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(ledCount));
        // Validate the existing report before touching the same three ARGB zones.
        var result = StaticConfig(current, Color.Black);
        for (int h = 0; h < 3; h++)
        {
            int o = 1 + h * 16;
            result[o] = 9; // Advanced / software per-LED control, MSI 761 protocol.
            result[o + 1] = 255; result[o + 5] = 255; result[o + 9] = 255;
            result[o + 10] = 255; result[o + 11] = 255; result[o + 12] = 255;
            result[o + 13] = 3; result[o + 14] = 0x15;
            result[o + 15] = (byte)ledCount;
        }
        return result;
    }

    public static byte[] FramePacket(int header, ReadOnlySpan<byte> rgb)
    {
        if (header is < 0 or > 2) throw new ArgumentOutOfRangeException(nameof(header));
        if (rgb.Length is < 3 or > 360 || rgb.Length % 3 != 0) throw new ArgumentException("Expected 1–120 RGB LEDs.", nameof(rgb));
        var packet = new byte[727];
        packet[0] = 0x51; packet[1] = 0x09; packet[2] = 0x04;
        packet[3] = (byte)header; packet[6] = (byte)(rgb.Length / 3);
        rgb.CopyTo(packet.AsSpan(7));
        return packet;
    }

    public static object Diagnose()
    {
        using var hid = HidDevice.OpenMortar();
        var config = hid.Read(0x50, 290);
        return new { hid.Serial, hid.FeatureLength, Config = Convert.ToHexString(config), Zones = Enumerable.Range(0, 4).Select(i => new { Header = i + 1, Mode = config[1 + i * 16], Flags = config[15 + i * 16], Leds = config[16 + i * 16] }).ToArray() };
    }

    public static byte[] StaticConfig(byte[] current, Color color)
    {
        if (current.Length != 290 || current[0] != 0x50) throw new ArgumentException("Expected MSI 290-byte zone report.");
        var result = (byte[])current.Clone();
        for (int header = 0; header < 3; header++)
        {
            int offset = 1 + header * 16;
            if (current[offset] > 10) throw new InvalidOperationException("Невідома конфігурація MSI; запис скасовано.");
            result[offset] = 2; // Hardware Steady mode, no render loop.
            for (int c = 0; c < 4; c++)
            {
                result[offset + 1 + c * 3] = color.R;
                result[offset + 2 + c * 3] = color.G;
                result[offset + 3 + c * 3] = color.B;
            }
            result[offset + 13] = 1; // One user color.
            result[offset + 14] = 0x35; // Independent zone, user colors, 100% brightness, enabled.
        }
        result[^1] = 0; // RAM only; never persist to controller flash.
        return result;
    }

    public static void Apply(Color color)
    {
        using var hid = HidDevice.OpenMortar();
        var desired = StaticConfig(hid.Read(0x50, 290), color);
        hid.Write(desired);
        for (int attempt = 0; attempt < 6; attempt++)
        {
            if (attempt > 0) Thread.Sleep(15);
            var actual = hid.Read(0x50, 290);
            if (actual.AsSpan(1, 48).SequenceEqual(desired.AsSpan(1, 48))) return;
        }
        throw new IOException("MSI: перевірка сталого кольору не пройшла.");
    }
}
