using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

namespace RgbTray.Hardware;

internal sealed record OpenRgbZone(DeviceDescriptor Device);

internal sealed class OpenRgbClient : IDisposable
{
    private readonly TcpClient client = new() { NoDelay = true };
    private readonly NetworkStream stream;
    private bool disposed;
    private bool invalidated;
    internal const int MaxPacket = 4 * 1024 * 1024;
    public OpenRgbClient(int port)
    {
        if (port is < 1 or > 65535) throw new ArgumentOutOfRangeException(nameof(port));
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            client.ConnectAsync(IPAddress.Loopback, port, timeout.Token).AsTask().GetAwaiter().GetResult();
            stream = client.GetStream(); stream.ReadTimeout = 2000; stream.WriteTimeout = 2000;
            Send(0, 40, UInt(3));
            var version = RequestResponse(0, 40);
            if (version.Length != 4 || BinaryPrimitives.ReadUInt32LittleEndian(version) < 3) throw new NotSupportedException("Потрібен OpenRGB SDK protocol 3 або новіший (OpenRGB 0.7+).");
            Send(0, 50, Encoding.UTF8.GetBytes("RGB Tray\0"));
        }
        catch { client.Dispose(); throw; }
    }
    public List<DeviceDescriptor> Discover()
    {
        Send(0, 0, Array.Empty<byte>());
        var countData = RequestResponse(0, 0);
        if (countData.Length != 4) throw new InvalidDataException("Некоректний OpenRGB controller count.");
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(countData);
        if (count > 128) throw new InvalidDataException("Забагато контролерів OpenRGB.");
        var devices = new List<DeviceDescriptor>();
        for (uint index = 0; index < count; index++)
        {
            Send(index, 1, UInt(3));
            devices.AddRange(ParseController(RequestResponse(index, 1), (int)index));
            if (devices.Count > 256) throw new InvalidDataException("Забагато зон OpenRGB.");
        }
        if (devices.Select(d => d.Id).Distinct(StringComparer.Ordinal).Count() != devices.Count)
            throw new InvalidDataException("OpenRGB повернув пристрої без унікальних ідентифікаторів; налаштуй їхні location/serial.");
        return devices;
    }
    internal static List<DeviceDescriptor> ParseController(byte[] data, int index)
    {
        var reader = new PacketReader(data);
        if (reader.U32() != data.Length) throw new InvalidDataException("Некоректний розмір опису OpenRGB.");
        uint type = reader.U32();
        string name = reader.Text(), vendor = reader.Text();
        _ = reader.Text(); _ = reader.Text(); // Description and version.
        string serial = reader.Text(), location = reader.Text();
        int modes = reader.Count(512); _ = reader.U32();
        for (int m = 0; m < modes; m++) { _ = reader.Text(); reader.Skip(48); int colors = reader.Count(4096); reader.Skip(colors * 4); }
        int zones = reader.Count(256);
        var result = new List<DeviceDescriptor>();
        string identity = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(System.Text.Json.JsonSerializer.Serialize(new { type, vendor, name, serial, location }))))[..24];
        for (int z = 0; z < zones; z++)
        {
            string zone = reader.Text(); uint zoneType = reader.U32(); _ = reader.U32(); _ = reader.U32();
            uint leds = reader.U32();
            if (leds > 4096) throw new InvalidDataException("Зона OpenRGB перевищує 4096 LED.");
            int matrix = reader.Count(65535); reader.Skip(matrix);
            if (leds == 0) continue;
            string zoneIdentity = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(System.Text.Json.JsonSerializer.Serialize(new { zone, zoneType }))))[..16];
            result.Add(new($"openrgb:{identity}:{zoneIdentity}", $"{name} · {zone}", (int)leds, (int)leds, false, leds == 1, index, z));
        }
        int totalLeds = reader.Count(4096);
        for (int led = 0; led < totalLeds; led++) { _ = reader.Text(); _ = reader.U32(); }
        int totalColors = reader.Count(4096); reader.Skip(totalColors * 4);
        if (!reader.AtEnd) throw new InvalidDataException("Невідомі дані після опису OpenRGB protocol 3.");
        if (result.Select(d => d.Id).Distinct(StringComparer.Ordinal).Count() != result.Count) throw new InvalidDataException("OpenRGB має зони з однаковими назвами; їх неможливо надійно зіставити.");
        return result;
    }
    public void SetCustomMode(int controller) { CheckNotifications(); Send((uint)controller, 1100, Array.Empty<byte>()); }
    public void WriteZone(DeviceDescriptor descriptor, ReadOnlySpan<byte> rgb)
    {
        if (rgb.Length != descriptor.LedCount * 3) throw new InvalidDataException("Змінилася довжина зони OpenRGB; онови список пристроїв.");
        CheckNotifications();
        Send((uint)descriptor.ControllerIndex, 1051, ZonePacket(descriptor.ZoneIndex, rgb));
    }
    internal static byte[] ZonePacket(int zone, ReadOnlySpan<byte> rgb)
    {
        if (zone < 0 || rgb.Length is < 3 or > 12288 || rgb.Length % 3 != 0) throw new ArgumentException("Invalid OpenRGB zone frame.");
        int count = rgb.Length / 3;
        var body = new byte[10 + count * 4];
        BinaryPrimitives.WriteUInt32LittleEndian(body, (uint)body.Length);
        BinaryPrimitives.WriteInt32LittleEndian(body.AsSpan(4), zone);
        BinaryPrimitives.WriteUInt16LittleEndian(body.AsSpan(8), (ushort)count);
        for (int i = 0; i < count; i++) rgb.Slice(i * 3, 3).CopyTo(body.AsSpan(10 + i * 4, 3));
        return body;
    }
    public void CheckNotifications()
    {
        if (invalidated) throw new IOException("OpenRGB: список пристроїв недійсний; потрібен повторний пошук.");
        if (client.Client.Poll(0, SelectMode.SelectRead) && client.Available == 0) throw new IOException("OpenRGB SDK від’єднано.");
        if (stream.DataAvailable)
        {
            var message = Receive();
            invalidated = true;
            if (message.Id == 100) throw new IOException("Список OpenRGB змінився. Онови пристрої перед продовженням.");
            throw new InvalidDataException($"Неочікуване повідомлення OpenRGB: {message.Id}.");
        }
    }
    private byte[] RequestResponse(uint controller, uint id)
    {
        var response = Receive();
        if (response.Id == 100) throw new IOException("Список OpenRGB змінився під час підключення. Повтори пошук.");
        if (response.Id != id || response.Controller != controller) throw new InvalidDataException("OpenRGB повернув відповідь на інший запит.");
        return response.Data;
    }
    private (uint Controller, uint Id, byte[] Data) Receive()
    {
        Span<byte> header = stackalloc byte[16]; stream.ReadExactly(header);
        if (!header[..4].SequenceEqual("ORGB"u8)) throw new InvalidDataException("Невідома сигнатура OpenRGB.");
        uint length = BinaryPrimitives.ReadUInt32LittleEndian(header[12..]);
        if (length > MaxPacket) throw new InvalidDataException("Пакет OpenRGB завеликий.");
        var data = new byte[(int)length]; stream.ReadExactly(data);
        return (BinaryPrimitives.ReadUInt32LittleEndian(header[4..]), BinaryPrimitives.ReadUInt32LittleEndian(header[8..]), data);
    }
    private void Send(uint controller, uint id, byte[] data)
    {
        Span<byte> header = stackalloc byte[16]; "ORGB"u8.CopyTo(header);
        BinaryPrimitives.WriteUInt32LittleEndian(header[4..], controller);
        BinaryPrimitives.WriteUInt32LittleEndian(header[8..], id);
        BinaryPrimitives.WriteUInt32LittleEndian(header[12..], (uint)data.Length);
        stream.Write(header); if (data.Length > 0) stream.Write(data);
    }
    private static byte[] UInt(uint value) { var data = new byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(data, value); return data; }
    public void Dispose() { if (disposed) return; disposed = true; client.Dispose(); }

    private sealed class PacketReader
    {
        private readonly byte[] data;
        private int offset;
        public PacketReader(byte[] data) => this.data = data;
        public bool AtEnd => offset == data.Length;
        public void Skip(int size) { if (size < 0 || size > data.Length - offset) throw new InvalidDataException("Обрізаний пакет OpenRGB."); offset += size; }
        public uint U32() { int start = offset; Skip(4); return BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(start, 4)); }
        public int Count(int max) { int start = offset; Skip(2); int value = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(start, 2)); if (value > max) throw new InvalidDataException("Некоректна кількість елементів OpenRGB."); return value; }
        public string Text()
        {
            int length = Count(4096); if (length < 1) throw new InvalidDataException("Неправильний рядок OpenRGB.");
            int start = offset; Skip(length);
            if (data[offset - 1] != 0) throw new InvalidDataException("Рядок OpenRGB не завершено нулем.");
            return new UTF8Encoding(false, true).GetString(data, start, length - 1);
        }
    }
}

internal sealed class OpenRgbSession : IHardwareSession
{
    private readonly OpenRgbClient client;
    private readonly List<DeviceDescriptor> current;
    private readonly HashSet<int> initialized = new();
    public OpenRgbSession(int port)
    {
        client = new(port);
        try { current = client.Discover(); } catch { client.Dispose(); throw; }
    }
    public static DiscoveryResult Discover(int port)
    {
        using var client = new OpenRgbClient(port);
        return new(client.Discover(), "OpenRGB SDK · 127.0.0.1. Режим контролера спільний між його зонами. Довжини налаштовуються в OpenRGB.");
    }
    public LightingOutput Open(TargetPlan target, byte[] initial)
    {
        var actual = current.SingleOrDefault(d => d.Id == target.Device.Id) ?? throw new IOException("Пристрій OpenRGB зник; онови список.");
        if (actual.LedCount != target.LedCount) throw new IOException("Довжина OpenRGB змінилася; онови список.");
        if (initialized.Add(actual.ControllerIndex)) client.SetCustomMode(actual.ControllerIndex);
        client.WriteZone(actual, initial);
        return new Output(target, client, actual);
    }
    public void CheckState() => client.CheckNotifications();
    public void Dispose() => client.Dispose();
    private sealed class Output : LightingOutput
    {
        private readonly OpenRgbClient client;
        private readonly DeviceDescriptor actual;
        public Output(TargetPlan plan, OpenRgbClient client, DeviceDescriptor actual) : base(plan) { this.client = client; this.actual = actual; }
        public override void Send(ReadOnlySpan<byte> rgb) => client.WriteZone(actual, rgb);
        public override void Restore(Color color)
        {
            var rgb = new byte[actual.LedCount * 3];
            for (int i = 0; i < actual.LedCount; i++) { rgb[i * 3] = color.R; rgb[i * 3 + 1] = color.G; rgb[i * 3 + 2] = color.B; }
            Send(rgb);
        }
    }
}
