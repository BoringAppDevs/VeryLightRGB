namespace RgbTray.Hardware;

internal sealed class MsiDirectSession : IDisposable
{
    private readonly HidDevice hid;
    private readonly byte[][] packets;
    public long Frames { get; private set; }

    public MsiDirectSession(int ledCount)
    {
        hid = HidDevice.OpenMortar();
        try
        {
            var current = hid.Read(0x50, 290);
            // Clear the old (possibly longer) chain before changing its length.
            WriteConfig(MsiLighting.StaticConfig(current, Color.Black));
            var config = MsiLighting.DirectConfig(current, ledCount);
            WriteConfig(config);
            packets = Enumerable.Range(0, 3).Select(h => MsiLighting.FramePacket(h, new byte[ledCount * 3])).ToArray();
        }
        catch { hid.Dispose(); throw; }
    }

    private void WriteConfig(byte[] config)
    {
        hid.Write(config);
        byte[] actual = Array.Empty<byte>();
        // Firmware can expose the previous configuration briefly after SetFeature.
        // Wait for confirmation before sending the next configuration; never resend.
        for (int attempt = 0; attempt < 6; attempt++)
        {
            if (attempt > 0) Thread.Sleep(15);
            actual = hid.Read(0x50, 290);
            if (actual.AsSpan(1, 48).SequenceEqual(config.AsSpan(1, 48))) return;
        }
        throw new IOException($"MSI: режим діодів не підтверджено. Expected {Convert.ToHexString(config.AsSpan(1, 16))}, received {Convert.ToHexString(actual.AsSpan(1, 16))}.");
    }

    public void WriteFrame(ReadOnlySpan<byte> rgb)
    {
        if (rgb.Length != packets[0][6] * 3) throw new ArgumentException("Frame LED count changed.", nameof(rgb));
        foreach (var packet in packets)
        {
            rgb.CopyTo(packet.AsSpan(7));
            hid.Write(packet);
            _ = hid.Read(0x51, 727, allowZoneResponse: true); // Same post-frame read as SignalRGB; firmware returns 0x50 or 0x51.
        }
        Frames++;
    }
    public void Dispose() => hid.Dispose();
}
