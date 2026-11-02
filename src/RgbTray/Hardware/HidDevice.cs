using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace RgbTray.Hardware;

internal sealed class HidDevice : IDisposable
{
    private readonly SafeFileHandle handle;
    public ushort FeatureLength { get; }
    public string Serial { get; }

    private HidDevice(SafeFileHandle handle, ushort length, string serial)
        => (this.handle, FeatureLength, Serial) = (handle, length, serial);

    public static HidDevice OpenMortar()
    {
        var board = Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS", "BaseBoardProduct", "") as string;
        if (!NativeCompatibility.SupportsBoard(board))
            throw new InvalidOperationException("Ця версія підтримує тільки MSI MAG B850M MORTAR WIFI (MS-7E61).");
        HidD_GetHidGuid(out var guid);
        var list = SetupDiGetClassDevs(ref guid, null, IntPtr.Zero, 0x12);
        if (list == new IntPtr(-1)) throw new Win32Exception();
        try
        {
            for (uint i = 0; ; i++)
            {
                var info = new InterfaceData { Size = Marshal.SizeOf<InterfaceData>() };
                if (!SetupDiEnumDeviceInterfaces(list, IntPtr.Zero, ref guid, i, ref info))
                {
                    if (Marshal.GetLastWin32Error() == 259) break;
                    throw new Win32Exception();
                }
                SetupDiGetDeviceInterfaceDetail(list, ref info, IntPtr.Zero, 0, out var required, IntPtr.Zero);
                var detail = Marshal.AllocHGlobal((int)required);
                try
                {
                    Marshal.WriteInt32(detail, 8); // Unicode SP_DEVICE_INTERFACE_DETAIL_DATA on x64.
                    if (!SetupDiGetDeviceInterfaceDetail(list, ref info, detail, required, out _, IntPtr.Zero)) throw new Win32Exception();
                    var path = Marshal.PtrToStringUni(detail + 4)!;
                    if (!path.Contains("vid_0db0&pid_0076", StringComparison.OrdinalIgnoreCase)) continue;
                    var file = CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                    if (file.IsInvalid) { file.Dispose(); throw new Win32Exception(); }
                    bool keep = false;
                    try
                    {
                        var attrs = new Attributes { Size = Marshal.SizeOf<Attributes>() };
                        if (!HidD_GetAttributes(file, ref attrs) || attrs.Vendor != 0x0DB0 || attrs.Product != 0x0076) continue;
                        var serialBuffer = new byte[256];
                        if (!HidD_GetSerialNumberString(file, serialBuffer, serialBuffer.Length)) throw new Win32Exception();
                        var serial = Encoding.Unicode.GetString(serialBuffer).TrimEnd('\0');
                        if (!serial.StartsWith("7E61", StringComparison.Ordinal)) continue;
                        if (!HidD_GetPreparsedData(file, out var parsed)) throw new Win32Exception();
                        try
                        {
                            if (HidP_GetCaps(parsed, out var caps) != 0x00110000) throw new InvalidOperationException("Не вдалося прочитати HID capabilities.");
                            if (caps.FeatureLength < 761) throw new InvalidOperationException($"Невідомий MSI HID протокол ({caps.FeatureLength} bytes).");
                            keep = true;
                            return new HidDevice(file, caps.FeatureLength, serial);
                        }
                        finally { HidD_FreePreparsedData(parsed); }
                    }
                    finally { if (!keep) file.Dispose(); }
                }
                finally { Marshal.FreeHGlobal(detail); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(list); }
        throw new InvalidOperationException("Контролер MSI 0DB0:0076 / 7E61 не знайдено.");
    }

    public byte[] Read(byte report, int length, bool allowZoneResponse = false)
    {
        var data = new byte[length]; data[0] = report;
        if (!HidD_GetFeature(handle, data, data.Length)) throw new Win32Exception(Marshal.GetLastWin32Error(), $"MSI GetFeature 0x{report:X2}");
        // This board's firmware answers the post-frame 0x51 read with its 0x50
        // zone report. Configuration reads still require an exact report ID.
        if (data[0] != report && !(allowZoneResponse && report == 0x51 && data[0] == 0x50))
            throw new InvalidOperationException($"MSI request 0x{report:X2}: unexpected response {Convert.ToHexString(data.AsSpan(0, Math.Min(16, data.Length)))}.");
        return data;
    }

    public void Write(byte[] data)
    {
        if (!HidD_SetFeature(handle, data, data.Length)) throw new Win32Exception(Marshal.GetLastWin32Error(), $"MSI SetFeature 0x{data[0]:X2}");
    }
    public void Dispose() => handle.Dispose();

    [StructLayout(LayoutKind.Sequential)] private struct InterfaceData { public int Size; public Guid Guid; public int Flags; public IntPtr Reserved; }
    [StructLayout(LayoutKind.Sequential)] private struct Attributes { public int Size; public ushort Vendor, Product, Version; }
    [StructLayout(LayoutKind.Sequential)] private unsafe struct Caps
    {
        public ushort Usage, UsagePage, InputLength, OutputLength, FeatureLength;
        public fixed ushort Reserved[17];
        public ushort LinkNodes, InputButtons, InputValues, InputIndices, OutputButtons, OutputValues, OutputIndices, FeatureButtons, FeatureValues, FeatureIndices;
    }
    [DllImport("hid.dll")] private static extern void HidD_GetHidGuid(out Guid guid);
    [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_GetAttributes(SafeFileHandle file, ref Attributes attributes);
    [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_GetSerialNumberString(SafeFileHandle file, byte[] buffer, int length);
    [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_GetPreparsedData(SafeFileHandle file, out IntPtr data);
    [DllImport("hid.dll")] private static extern bool HidD_FreePreparsedData(IntPtr data);
    [DllImport("hid.dll")] private static extern int HidP_GetCaps(IntPtr data, out Caps caps);
    [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_GetFeature(SafeFileHandle file, [In, Out] byte[] data, int length);
    [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_SetFeature(SafeFileHandle file, byte[] data, int length);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr SetupDiGetClassDevs(ref Guid guid, string? enumerator, IntPtr parent, int flags);
    [DllImport("setupapi.dll", SetLastError = true)] private static extern bool SetupDiEnumDeviceInterfaces(IntPtr list, IntPtr device, ref Guid guid, uint index, ref InterfaceData info);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr list, ref InterfaceData info, IntPtr detail, uint size, out uint required, IntPtr device);
    [DllImport("setupapi.dll")] private static extern bool SetupDiDestroyDeviceInfoList(IntPtr list);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
}
