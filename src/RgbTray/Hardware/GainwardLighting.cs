using System.Runtime.InteropServices;

namespace RgbTray.Hardware;

internal sealed class GainwardLighting : IDisposable
{
    private readonly IntPtr library;
    private readonly IntPtr gpu;
    private readonly I2c read;
    private readonly I2c write;
    private readonly Simple? unload;
    private bool initialized;
    private bool disposed;

    public GainwardLighting()
    {
        library = NativeLibrary.Load(Path.Combine(Environment.SystemDirectory, "nvapi64.dll"));
        try
        {
            var query = Marshal.GetDelegateForFunctionPointer<Query>(NativeLibrary.GetExport(library, "nvapi_QueryInterface"));
            T Get<T>(uint id) where T : Delegate
            {
                var pointer = query(id);
                if (pointer == IntPtr.Zero) throw new NotSupportedException($"NVIDIA API 0x{id:X8} недоступний.");
                return Marshal.GetDelegateForFunctionPointer<T>(pointer);
            }
            unload = Get<Simple>(0xD22BDD7E);
            Check(Get<Simple>(0x0150E828)(), "Initialize");
            initialized = true;
            read = Get<I2c>(0x4D7B0709); write = Get<I2c>(0x283AC65A);
            var handles = new IntPtr[64];
            Check(Get<Enumerate>(0xE5AC921F)(handles, out var count), "EnumPhysicalGPUs");
            if (count is < 0 or > 64) throw new IOException("NVIDIA повернула некоректну кількість GPU.");
            var pci = Get<Pci>(0x2DDFB66E);
            for (int i = 0; i < count; i++)
            {
                Check(pci(handles[i], out var device, out var subsystem, out _, out _), "GetPCIIdentifiers");
                if (NativeCompatibility.SupportsGpu(device, subsystem)) { gpu = handles[i]; break; }
            }
            if (gpu == IntPtr.Zero) throw new InvalidOperationException("Gainward RTX 5080 Phoenix (10DE:2C02 / 10B0:F323) не знайдено.");
        }
        catch { if (initialized) unload?.Invoke(); NativeLibrary.Free(library); throw; }
    }

    public unsafe byte Register(byte register, byte? value = null)
    {
        // Fixed RGB controller, GPU internal port 1. No bus scan and no DDC access.
        byte data = value ?? 0;
        var info = new I2cInfo
        {
            Version = (uint)Marshal.SizeOf<I2cInfo>() | 3u << 16,
            Address = 0x92, Register = (IntPtr)(&register), RegisterSize = 1,
            Data = (IntPtr)(&data), Size = 1, DeprecatedSpeed = 0xFFFF,
            Port = 1, PortSet = 1
        };
        uint extra = 0;
        Check((value.HasValue ? write : read)(gpu, ref info, ref extra), $"I2C {(value.HasValue ? "write" : "read")} 0x{register:X2}");
        return data;
    }

    public object Diagnose() => new { Device = "10DE:2C02 / 10B0:F323", Address = "0x49", StructSize = Marshal.SizeOf<I2cInfo>(), Registers = new byte[] { 0xE0, 0x60, 0x6C, 0x6D, 0x6E, 0x6F }.ToDictionary(r => $"0x{r:X2}", r => Register(r)) };

    public void Apply(Color color)
    {
        // Register addresses and initialization verified against the installed SignalRGB plugin.
        Register(0xE0, 0); Register(0x60, 1); Register(0x6F, 100);
        Register(0x6C, color.R); Register(0x6D, color.G); Register(0x6E, color.B);
        if (Register(0x6C) != color.R || Register(0x6D) != color.G || Register(0x6E) != color.B || Register(0x60) != 1)
            throw new IOException("Gainward: перевірка записаного кольору не пройшла.");
    }

    public void WriteFrame(Color color)
    {
        // The mode and brightness are initialized once for the animation session.
        Register(0x6C, color.R); Register(0x6D, color.G); Register(0x6E, color.B);
    }

    private static void Check(int status, string operation)
    {
        if (status != 0) throw new IOException($"NVIDIA {operation}: status {status}.");
    }
    public void Dispose() { if (disposed) return; disposed = true; if (initialized) unload?.Invoke(); NativeLibrary.Free(library); }
    [StructLayout(LayoutKind.Sequential)] private struct I2cInfo
    {
        public uint Version, DisplayMask;
        public byte Ddc, Address;
        public IntPtr Register;
        public uint RegisterSize;
        public IntPtr Data;
        public uint Size, DeprecatedSpeed, Speed;
        public byte Port;
        public uint PortSet;
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr Query(uint id);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Simple();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Enumerate([Out] IntPtr[] handles, out int count);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Pci(IntPtr gpu, out uint device, out uint subsystem, out uint revision, out uint ext);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int I2c(IntPtr gpu, ref I2cInfo info, ref uint extra);
}
