using System.Reflection;
using System.Windows.Forms;
using RgbTray;
using RgbTray.Hardware;

internal static class UiTests
{
    public static void Run(Action<bool, string> check)
    {
        Exception? error = null;
        var thread = new Thread(() =>
        {
            try
            {
                var settings = new Settings();
                var first = NativeCompatibility.Header(0); var used = NativeCompatibility.Header(1); var third = NativeCompatibility.Header(2);
                settings.GetDevice(used).Managed = true;
                using var form = new MainForm(settings) { ShowInTaskbar = false, Opacity = 0 };
                form.SetDevices(new(new[] { first, used, third, NativeCompatibility.Gpu }, "Test discovery"));
                form.Show(); Application.DoEvents();
                T Field<T>(string name) where T : class => (T)typeof(MainForm).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(form)!;
                var targets = Field<ListBox>("targets"); var available = Field<CheckedListBox>("available"); var leds = Field<NumericUpDown>("leds");
                check(targets.Items.Count == 3 && available.Items.Count == 4, "Unselected available headers appeared as connected devices in the main list.");
                check(form.Controls.OfType<Control>().SelectMany(Descendants).OfType<TabPage>().All(p => p.Text != "Профілі"), "Removed profiles are still visible.");
                check(leds.Visible && leds.Enabled, "Global LED count is hidden behind advanced settings.");
                leds.Value = 16;
                check(settings.LedCount == 16 && settings.GetDevice(used).LedCount == 16 && settings.GetDevice(first).LedCount == 12, "Global LED length changed an unselected header or failed to update the selected one.");
                targets.SelectedIndex = 1;
                check(leds.Visible && leds.Enabled && leds.Value == 16, "Selected ARGB LED count is hidden or disabled with a shared style.");
                leds.Value = 20;
                check(settings.GetDevice(used).LedCount == 20 && settings.LedCount == 16, "Individual LED length overwrote the global default.");
                targets.SelectedIndex = 2;
                check(!leds.Visible, "Single-zone GPU exposes a fictional configurable LED count.");
                targets.SelectedIndex = 0;
                Field<ComboBox>("effect").SelectedIndex = (int)LightingEffect.Rainbow;
                check(!Field<TextBox>("hex").Visible && !Field<NumericUpDown>("speed").Visible && !Field<NumericUpDown>("brightness").Visible && !Field<FlowLayoutPanel>("swatches").Visible, "Rainbow exposes parameters which its fixed preset ignores.");
                Field<ComboBox>("effect").SelectedIndex = (int)LightingEffect.Solid;
                check(Field<TextBox>("hex").Visible && Field<NumericUpDown>("brightness").Visible, "Changing back from rainbow failed to restore color controls.");
                available.SetItemChecked(0, true); Application.DoEvents();
                check(settings.GetDevice(first).Managed && targets.Items.Count == 4, "Selecting an available header did not add it to the main list.");
                targets.SelectedIndex = 2;
                Field<CheckBox>("linked").Checked = false;
                check(!settings.GetDevice(used).UseGlobal && Field<TextBox>("hex").Enabled, "Unlinking a selected device did not enable independent editing.");
            }
            catch (Exception ex) { error = ex; }
        });
        thread.SetApartmentState(ApartmentState.STA); thread.Start(); thread.Join();
        if (error is not null) throw new Exception("UI regression test failed.", error);
    }
    private static IEnumerable<Control> Descendants(Control parent)
    {
        yield return parent;
        foreach (Control child in parent.Controls) foreach (var c in Descendants(child)) yield return c;
    }
}
