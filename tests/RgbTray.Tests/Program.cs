using System.Drawing;
using RgbTray;
using RgbTray.Hardware;
using RgbTray.Effects;

if (args.Contains("--export-native-fixtures"))
{
    var fixtures = new List<object>();
    foreach (var e in Enum.GetValues<LightingEffect>())
    foreach (int length in new[] { 1, 12, 16, 200 })
    foreach (double ms in new[] { 0.0, 100, 200, 300, 350, 500, 1800, 9999, 12000 })
    foreach (bool reverse in new[] { false, true })
    {
        var style = new LightingStyle { Color = "#C65CFF", SecondaryColor = "#27C8FF", Brightness = 80, Effect = e, StepMilliseconds = 200, DarkWidth = 4 };
        int offset = length > 1 ? 3 % length : 0;
        var descriptor = new DeviceDescriptor("fixture", "Fixture", length, length, false, length == 1);
        byte[] frame = new FrameRenderer(new(descriptor, style, length, reverse, offset, 12)).Render(ms);
        fixtures.Add(new { Effect = e, Count = length, Ms = ms, Reverse = reverse, Offset = offset, Rgb = Convert.ToHexString(frame) });
    }
    Directory.CreateDirectory("local");
    File.WriteAllText("local/native-render-fixtures.json", System.Text.Json.JsonSerializer.Serialize(fixtures));
    Console.WriteLine($"Exported {fixtures.Count} reference frames. No hardware accessed.");
    return;
}

int count = 0;
void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
    count++;
}
void Reject(Action action, string message)
{
    try { action(); }
    catch (ArgumentException) { count++; return; }
    catch (FormatException) { count++; return; }
    throw new Exception(message);
}

// Controller reports contain unrelated zones and a flash-save flag. Preserve every
// byte outside the intended fields, including LED lengths and inactive zones.
var original = Enumerable.Range(0, 290).Select(i => (byte)(i * 37 % 256)).ToArray();
original[0] = 0x50;
for (int h = 0; h < 3; h++) original[1 + h * 16] = 9;
var snapshot = (byte[])original.Clone();
foreach (var color in new[] { Color.Black, Color.White, Color.FromArgb(0x12, 0x34, 0x56) })
{
    var result = MsiLighting.StaticConfig(original, color);
    Check(original.SequenceEqual(snapshot), "Input report was mutated.");
    Check(result[^1] == 0, "Flash-save flag must stay disabled.");
    for (int i = 0; i < result.Length - 1; i++)
    {
        bool changedField = Enumerable.Range(0, 3).Any(h => i >= 1 + h * 16 && i <= 15 + h * 16);
        if (!changedField) Check(result[i] == original[i], $"Unrelated byte changed: {i}");
    }
    for (int h = 0; h < 3; h++)
    {
        int offset = 1 + h * 16;
        Check(result[offset] == 2 && result[offset + 13] == 1 && result[offset + 14] == 0x35, "Wrong steady mode.");
        for (int c = 0; c < 4; c++)
            Check(result.AsSpan(offset + 1 + c * 3, 3).SequenceEqual(new byte[] { color.R, color.G, color.B }), "Wrong RGB order.");
    }
}
Reject(() => MsiLighting.StaticConfig(new byte[185], Color.Red), "Must reject old MSI protocol.");
Reject(() => MsiLighting.StaticConfig(new byte[290], Color.Red), "Must reject unknown report ID.");
foreach (var invalid in new[] { "red", "#123", "#GGGGGG", "1234567", "#11223344" })
    Reject(() => Settings.ParseColor(invalid), "Invalid HEX accepted.");
var settings = new Settings { Color = "#804020", Brightness = 50 };
Check(settings.OutputColor().ToArgb() == Color.FromArgb(64, 32, 16).ToArgb(), "Brightness scaling failed.");
settings.Enabled = false;
Check(settings.OutputColor().ToArgb() == Color.Black.ToArgb(), "Off must output black.");
settings.Enabled = true; settings.Brightness = 0;
Check(settings.OutputColor().ToArgb() == Color.Black.ToArgb(), "Zero brightness must output black.");
// At phase boundaries the previous LED remains lit; the next LED eases up.
var rgb = new byte[3 * 3];
var full = Color.FromArgb(200, 100, 40);
const int step = 200;
void Frame(double ms) => SequentialFill.Render(ms, full, 3, step, rgb);
Frame(0); Check(rgb.All(v => v == 0), "Cycle must start black.");
Frame(199); Check(rgb.All(v => v == 0), "Dark pause must stay black.");
Frame(300); Check(rgb.SequenceEqual(new byte[] { 100, 50, 20, 0, 0, 0, 0, 0, 0 }), "First LED should be halfway lit.");
Frame(400); Check(rgb.SequenceEqual(new byte[] { 200, 100, 40, 0, 0, 0, 0, 0, 0 }), "Only the first LED should be fully lit.");
Frame(500); Check(rgb.SequenceEqual(new byte[] { 200, 100, 40, 100, 50, 20, 0, 0, 0 }), "Previously lit LED must not fade while next one lights.");
Frame(800); Check(rgb.SequenceEqual(Enumerable.Repeat(new byte[] { 200, 100, 40 }, 3).SelectMany(x => x)), "All LEDs must reach full color.");
Frame(1149); Check(rgb[6] == 200, "Full-color hold ended early.");
Frame(1150); Check(rgb.SequenceEqual(new byte[] { 200, 100, 40, 200, 100, 40, 200, 100, 40 }), "Drain must start fully lit without a hard reset.");
Frame(1250); Check(rgb.SequenceEqual(new byte[] { 100, 50, 20, 200, 100, 40, 200, 100, 40 }), "Only the first LED should be halfway dimmed.");
Frame(1350); Check(rgb.SequenceEqual(new byte[] { 0, 0, 0, 200, 100, 40, 200, 100, 40 }), "Later LEDs must stay lit while the first turns off.");
Frame(1450); Check(rgb.SequenceEqual(new byte[] { 0, 0, 0, 100, 50, 20, 200, 100, 40 }), "Second LED should fade out next.");
Frame(1650); Check(rgb.SequenceEqual(new byte[] { 0, 0, 0, 0, 0, 0, 100, 50, 20 }), "Last LED should fade out last.");
Frame(1750); Check(rgb.All(v => v == 0), "New cycle should start dark after all LEDs fade out.");
Frame(2050); Check(rgb[0] == 100 && rgb[3] == 0, "Second cycle must repeat the fill after the gradual drain.");
Check(SequentialFill.CycleMilliseconds(3, 200) == 1750, "Cycle length must include fill and drain.");
var gpuHalf = SequentialFill.Render(300, full, 3, step, rgb);
Check(gpuHalf.R == 33 && gpuHalf.G == 17 && gpuHalf.B == 7, "GPU should track aggregate fill brightness.");
var gpuDraining = SequentialFill.Render(1250, full, 3, step, rgb);
Check(gpuDraining.R == 167 && gpuDraining.G == 83 && gpuDraining.B == 33, "GPU must dim gradually with the LEDs instead of cutting off.");
Frame(0);
for (int ms = 200; ms <= 800; ms += 10)
{
    var before = (byte[])rgb.Clone(); Frame(ms);
    Check(rgb.Zip(before).All(pair => pair.First >= pair.Second), "LED intensity must be monotonic within a fill.");
}
Frame(1150);
for (int ms = 1160; ms <= 1750; ms += 10)
{
    var before = (byte[])rgb.Clone(); Frame(ms);
    Check(rgb.Zip(before).All(pair => pair.First <= pair.Second), "LED intensity must be monotonic within a drain.");
    int changedLeds = Enumerable.Range(0, 3).Count(led => !rgb.AsSpan(led * 3, 3).SequenceEqual(before.AsSpan(led * 3, 3)));
    Check(changedLeds <= 1, "Drain must not turn multiple LEDs off together.");
}
foreach (int leds in new[] { 1, 15, 120 })
{
    var black = new byte[leds * 3];
    _ = SequentialFill.Render(100000, Color.Black, leds, 180, black);
    Check(black.All(v => v == 0), "Off color must remain black in an effect.");
    var direct = MsiLighting.DirectConfig(original, leds);
    Check(original.SequenceEqual(snapshot), "Direct setup mutated input.");
    Check(direct[^1] == 0, "Direct mode must not save to flash.");
    for (int i = 49; i < 289; i++) Check(direct[i] == original[i], "Direct mode modified an unrelated zone.");
    for (int h = 0; h < 3; h++)
    {
        Check(direct[1 + h * 16] == 9 && direct[16 + h * 16] == leds, "Wrong direct-mode LED count.");
        var packet = MsiLighting.FramePacket(h, black);
        Check(packet.Length == 727 && packet[0] == 0x51 && packet[1] == 9 && packet[2] == 4 && packet[3] == h && packet[6] == leds, "Wrong MSI frame envelope.");
        Check(packet.AsSpan(7 + black.Length).ToArray().All(v => v == 0), "Frame padding must be zero.");
    }
}
var patterned = Enumerable.Range(0, 45).Select(i => (byte)i).ToArray();
Check(MsiLighting.FramePacket(2, patterned).AsSpan(7, 45).SequenceEqual(patterned), "Per-LED RGB payload changed order.");
Reject(() => MsiLighting.DirectConfig(original, 0), "Zero LEDs must be rejected.");
Reject(() => MsiLighting.DirectConfig(original, 121), "Over-capacity direct config must be rejected.");
Reject(() => MsiLighting.FramePacket(3, new byte[45]), "Unknown header must be rejected.");
Reject(() => MsiLighting.FramePacket(0, new byte[4]), "Partial RGB triple must be rejected.");
Reject(() => SequentialFill.CycleMilliseconds(15, 0), "Zero step must be rejected.");
var migrated = System.Text.Json.JsonSerializer.Deserialize<Settings>("{\"Color\":\"#FFFFFF\",\"Brightness\":100,\"Enabled\":true}")!;
Check(migrated.Effect == LightingEffect.Solid && migrated.LedCount == 12 && migrated.StepMilliseconds == 180, "Missing settings should use the TL-K12 default and solid mode.");
var savedOld = System.Text.Json.JsonSerializer.Deserialize<Settings>("{\"Effect\":1,\"LedCount\":15,\"StepMilliseconds\":100}")!;
Check(savedOld.Effect == LightingEffect.SequentialFill && savedOld.LedCount == 15 && savedOld.StepMilliseconds == 100, "Explicit saved settings must survive the new default.");

// A four-LED dark band must wrap without a reset or a change in total light.
foreach (int leds in new[] { 1, 3, 4, 5, 12, 16, 120 })
{
    const int duration = 200;
    var band = new byte[leds * 3];
    var reference = new byte[band.Length];
    var bandColor = Color.FromArgb(201, 103, 41); // Odd channels exercise complementary rounding.
    int cycle = CircularDarkBand.CycleMilliseconds(leds, duration);
    var options = new EffectOptions(bandColor, leds, duration, LightingEffect.CircularDarkBand);
    Check(options.Validate() == cycle && cycle == leds * duration, "Circular timing is incorrect.");
    _ = options.Render(0, reference);
    Check(reference.Where((_, i) => i % 3 == 0).Count(v => v == 0) == Math.Min(4, leds), "Initial band must have exactly four dark LEDs.");
    for (int ms = 0; ms < cycle; ms += 25)
    {
        var gpu = options.Render(ms, band);
        Check(gpu.ToArgb() == bandColor.ToArgb(), "GPU must stay steady during the circular effect.");
        Check(Enumerable.Range(0, leds).Sum(led => band[led * 3]) == (leds - Math.Min(4, leds)) * bandColor.R, "Circular fade changed total red brightness.");
        Check(Enumerable.Range(0, leds).Sum(led => band[led * 3 + 1]) == (leds - Math.Min(4, leds)) * bandColor.G, "Circular fade changed total green brightness.");
        Check(Enumerable.Range(0, leds).Sum(led => band[led * 3 + 2]) == (leds - Math.Min(4, leds)) * bandColor.B, "Circular fade changed total blue brightness.");
        if (ms % duration == 0)
        {
            int position = ms / duration;
            var dark = Enumerable.Range(0, leds).Where(led => band[led * 3] == 0).ToArray();
            var expectedDark = Enumerable.Range(0, Math.Min(4, leds)).Select(offset => (position + offset) % leds).Order().ToArray();
            Check(dark.SequenceEqual(expectedDark), "Dark block moved in the wrong order or did not wrap.");
        }
    }
    _ = options.Render(cycle, band);
    Check(band.SequenceEqual(reference), "Circular effect should loop exactly.");
    _ = options.Render(cycle - 0.001, band);
    Check(band.SequenceEqual(reference), "Circular boundary should be visually continuous.");
}
var transition = new byte[36];
_ = CircularDarkBand.Render(100, full, 12, 200, transition);
Check(transition.AsSpan(0, 3).SequenceEqual(new byte[] { 100, 50, 20 }) && transition.AsSpan(12, 3).SequenceEqual(new byte[] { 100, 50, 20 }), "Trailing LED must fade up as the leading LED fades down.");
Check(transition.AsSpan(3, 9).ToArray().All(v => v == 0), "Interior of the dark block must stay dark during movement.");
_ = CircularDarkBand.Render(150, Color.Black, 12, 200, transition);
Check(transition.All(v => v == 0), "Zero output color must also disable the circular effect.");
Reject(() => CircularDarkBand.CycleMilliseconds(0, 100), "Invalid circular LED count accepted.");
Reject(() => CircularDarkBand.Render(0, Color.White, 12, 100, new byte[3]), "Wrong circular frame length accepted.");
Reject(() => new EffectOptions(Color.White, 12, 100, LightingEffect.Solid).Validate(), "Solid mode must not create an animation thread.");
StructuredTests.Run(Check);
UiTests.Run(Check);
StartupTests.Run(Check);
if (args.Contains("--hardware-effects")) HardwareEffects.Run(Check);
Console.WriteLine($"PASS: {count} assertions. {(args.Contains("--hardware-effects") ? "Including opt-in hardware effect lifecycle tests." : "No hardware accessed.")}");
