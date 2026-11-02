using System.Text.Json;

namespace RgbTray;

internal enum LightingEffect { Solid, SequentialFill, CircularDarkBand, Breathing, Rainbow, ColorCycle }
internal enum HardwareBackend { Native, OpenRgb }

internal class LightingStyle
{
    public string Color { get; set; } = "#FF8040";
    public string SecondaryColor { get; set; } = "#536CFF";
    public int Brightness { get; set; } = 50;
    public bool Enabled { get; set; } = true;
    public LightingEffect Effect { get; set; } = LightingEffect.Solid;
    public int StepMilliseconds { get; set; } = 180;
    public int DarkWidth { get; set; } = 4;
    public LightingStyle Copy() => new() { Color = Color, SecondaryColor = SecondaryColor, Brightness = Brightness, Enabled = Enabled, Effect = Effect, StepMilliseconds = StepMilliseconds, DarkWidth = DarkWidth };
    public virtual void Validate()
    {
        _ = Settings.ParseColor(Color); _ = Settings.ParseColor(SecondaryColor);
        if (Brightness is < 0 or > 100) throw new InvalidDataException("Яскравість має бути 0–100.");
        if (!Enum.IsDefined(Effect)) throw new InvalidDataException("Невідомий ефект.");
        if (StepMilliseconds is < 100 or > 1000) throw new InvalidDataException("Час кроку має бути 100–1000 мс.");
        if (DarkWidth is < 1 or > 120) throw new InvalidDataException("Ширина темного блоку має бути 1–120.");
    }
    public Color OutputColor()
    {
        var c = Settings.ParseColor(Color);
        return Enabled ? System.Drawing.Color.FromArgb(c.R * Brightness / 100, c.G * Brightness / 100, c.B * Brightness / 100) : System.Drawing.Color.Black;
    }
}

internal sealed class DeviceSettings
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public bool Managed { get; set; } = true;
    public bool UseGlobal { get; set; } = true;
    public bool HasCustomStyle { get; set; }
    public int LedCount { get; set; } = 12;
    public bool Reverse { get; set; }
    public int Offset { get; set; }
    public LightingStyle Style { get; set; } = new();
    public DeviceSettings Copy() => new() { Id = Id, Name = Name, Managed = Managed, UseGlobal = UseGlobal, HasCustomStyle = HasCustomStyle, LedCount = LedCount, Reverse = Reverse, Offset = Offset, Style = Style.Copy() };
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(Id) || Id.Length > 256 || Name is null || Name.Length > 100) throw new InvalidDataException("Некоректна назва пристрою.");
        if (LedCount is < 1 or > 4096 || Offset is < 0 or > 4095) throw new InvalidDataException("Некоректна довжина або зміщення LED.");
        if (Style is null) throw new InvalidDataException("Відсутні налаштування пристрою.");
        Style.Validate();
    }
}

internal sealed class Settings : LightingStyle
{
    public int SchemaVersion { get; set; } = 2;
    public int LedCount { get; set; } = 12;
    public HardwareBackend Backend { get; set; } = HardwareBackend.Native;
    public int OpenRgbPort { get; set; } = 6742;
    public int FramesPerSecond { get; set; } = 30;
    public List<DeviceSettings> Devices { get; set; } = new();
    private static string? dataDirectory;
    public static string DataDirectory => dataDirectory ?? Path.GetFullPath(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RgbTray"));
    internal static void ConfigureDataDirectory(string path)
    {
        if (!Path.IsPathFullyQualified(path)) throw new ArgumentException("Папка налаштувань має бути абсолютним шляхом.");
        dataDirectory = Path.GetFullPath(path);
    }
    private static string FilePath => Path.Combine(DataDirectory, "settings.json");
    internal static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    public static Settings Load() => File.Exists(FilePath) ? Deserialize(File.ReadAllText(FilePath)) : new();
    public static Settings Deserialize(string json)
    {
        if (json.Length > 2 * 1024 * 1024) throw new InvalidDataException("Файл налаштувань завеликий.");
        var value = JsonSerializer.Deserialize<Settings>(json) ?? throw new InvalidDataException("Порожній settings.json");
        value.Validate(); return value;
    }
    public override void Validate()
    {
        base.Validate();
        if (SchemaVersion != 2 || !Enum.IsDefined(Backend) || LedCount is < 1 or > 120 || OpenRgbPort is < 1 or > 65535 || FramesPerSecond is < 10 or > 60)
            throw new InvalidDataException("Некоректні системні налаштування.");
        if (Devices is null || Devices.Count > 512) throw new InvalidDataException("Забагато пристроїв.");
        foreach (var d in Devices) { if (d is null) throw new InvalidDataException("Порожній пристрій."); d.Validate(); }
        if (Devices.Select(d => d.Id).Distinct(StringComparer.Ordinal).Count() != Devices.Count) throw new InvalidDataException("Повторні ID пристроїв.");
    }
    public void Save()
    {
        Validate(); Directory.CreateDirectory(DataDirectory);
        var temp = FilePath + ".tmp";
        var json = JsonSerializer.Serialize(this, JsonOptions);
        if (json.Length > 2 * 1024 * 1024) throw new InvalidDataException("Файл налаштувань завеликий.");
        File.WriteAllText(temp, json);
        File.Move(temp, FilePath, true);
    }
    public DeviceSettings GetDevice(Hardware.DeviceDescriptor descriptor)
    {
        var found = Devices.FirstOrDefault(d => d.Id == descriptor.Id);
        if (found is not null) return found;
        found = new() { Managed = !descriptor.Id.StartsWith("native:msi:argb:", StringComparison.Ordinal), Id = descriptor.Id, Name = descriptor.Name.Length > 100 ? descriptor.Name[..100] : descriptor.Name, LedCount = descriptor.ConfigurableLength ? Math.Min(LedCount, descriptor.MaxLeds) : descriptor.LedCount, Style = Copy() };
        Devices.Add(found); return found;
    }
    public static Color ParseColor(string? hex)
    {
        if (hex is null || hex.Length != 7 || hex[0] != '#' || !int.TryParse(hex.AsSpan(1), System.Globalization.NumberStyles.HexNumber, null, out var rgb))
            throw new FormatException("Колір: #RRGGBB, наприклад #FF8040.");
        return System.Drawing.Color.FromArgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    }
}

internal static class Log
{
    private static readonly object Gate = new();
    public static void Write(string message)
    {
        lock (Gate) try
        {
            Directory.CreateDirectory(Settings.DataDirectory);
            var path = Path.Combine(Settings.DataDirectory, "rgbtray.log");
            if (File.Exists(path) && new FileInfo(path).Length > 512 * 1024) File.Move(path, path + ".old", true);
            File.AppendAllText(path, $"{DateTimeOffset.Now:O} {message}{Environment.NewLine}");
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}
