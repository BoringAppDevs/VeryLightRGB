using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Xml.Linq;

namespace RgbTray;

internal static class Startup
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private static string UserSid
    {
        get { using var identity = WindowsIdentity.GetCurrent(); return identity.User?.Value ?? throw new InvalidOperationException("Не визначено поточного користувача Windows."); }
    }
    public static string TaskName => "RgbTray-" + UserSid;
    public static bool Enabled
    {
        get
        {
            try { if (WithFolder(folder => ReadTaskEnabled(folder))) return true; }
            catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException) { }
            using var key = Registry.CurrentUser.OpenSubKey(RunKey);
            return key?.GetValue("RgbTray") is string;
        }
        set
        {
            if (value)
            {
                string executable = Environment.ProcessPath ?? throw new InvalidOperationException("Не знайдено шлях програми.");
                if (!File.Exists(executable) || !string.Equals(Path.GetExtension(executable), ".exe", StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Некоректний шлях автозапуску.");
                string sid = UserSid;
                WithFolder(folder =>
                {
                    object registered = folder.RegisterTask(TaskName, BuildTaskXml(executable, sid), 6, sid, null, 3, null);
                    try { if (!(bool)((dynamic)registered).Enabled) throw new IOException("Завдання автозапуску не ввімкнене."); }
                    finally { Marshal.ReleaseComObject(registered); }
                    return true;
                });
            }
            else WithFolder(folder => { try { folder.DeleteTask(TaskName, 0); } catch (Exception ex) when (ex.HResult == unchecked((int)0x80070002)) { } return true; });
            using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true);
            key?.DeleteValue("RgbTray", false);
            Log.Write(value ? $"Autostart enabled: scheduled task {TaskName}; interactive user; delay 10s; restart 3 times / 1 minute." : "Autostart disabled.");
        }
    }
    private static bool ReadTaskEnabled(dynamic folder)
    {
        object task;
        try { task = folder.GetTask(TaskName); }
        catch (Exception ex) when (ex.HResult == unchecked((int)0x80070002)) { return false; }
        try { return (bool)((dynamic)task).Enabled; }
        finally { Marshal.ReleaseComObject(task); }
    }
    private static T WithFolder<T>(Func<dynamic, T> action)
    {
        object service = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service") ?? throw new InvalidOperationException("Планувальник Windows недоступний."))!;
        object? folder = null;
        try { ((dynamic)service).Connect(); folder = ((dynamic)service).GetFolder(@"\"); return action(folder); }
        finally { if (folder is not null) Marshal.ReleaseComObject(folder); Marshal.ReleaseComObject(service); }
    }
    internal static string BuildTaskXml(string executable, string sid)
    {
        if (!Path.IsPathFullyQualified(executable) || string.IsNullOrWhiteSpace(sid)) throw new ArgumentException("Некоректний шлях або користувач автозапуску.");
        XNamespace ns = "http://schemas.microsoft.com/windows/2004/02/mit/task";
        XElement E(string name, params object[] content) => new(ns + name, content);
        return new XDocument(E("Task", new XAttribute("version", "1.2"),
            E("RegistrationInfo", E("Description", "RGB Tray — підсвітка після входу у Windows")),
            E("Triggers", E("LogonTrigger", E("Enabled", true), E("UserId", sid), E("Delay", "PT10S"))),
            E("Principals", E("Principal", new XAttribute("id", "CurrentUser"), E("UserId", sid), E("LogonType", "InteractiveToken"), E("RunLevel", "LeastPrivilege"))),
            E("Settings", E("MultipleInstancesPolicy", "IgnoreNew"), E("DisallowStartIfOnBatteries", false), E("StopIfGoingOnBatteries", false),
                E("AllowHardTerminate", false), E("StartWhenAvailable", true), E("RunOnlyIfNetworkAvailable", false), E("AllowStartOnDemand", true), E("Enabled", true),
                E("ExecutionTimeLimit", "PT0S"), E("Priority", 7), E("RestartOnFailure", E("Interval", "PT1M"), E("Count", 3))),
            E("Actions", new XAttribute("Context", "CurrentUser"), E("Exec", E("Command", executable), E("Arguments", $"--tray --autostart --data-dir \"{Settings.DataDirectory}\""), E("WorkingDirectory", Path.GetDirectoryName(executable)!))))).ToString();
    }
}
