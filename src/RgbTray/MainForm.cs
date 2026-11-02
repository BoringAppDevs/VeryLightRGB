using RgbTray.Effects;
using RgbTray.Hardware;

namespace RgbTray;

internal sealed class MainForm : Form
{
    private static readonly Color Background = Color.FromArgb(18, 21, 26), Surface = Color.FromArgb(29, 34, 42), Muted = Color.FromArgb(159, 171, 187), Accent = Color.FromArgb(100, 230, 198);
    private readonly Settings settings;
    private readonly ListBox targets = new();
    private readonly CheckedListBox available = new();
    private readonly Label availableHint = new();
    private readonly Label heading = new(), detail = new(), status = new(), discovery = new(), hint = new();
    private readonly Button master = new();
    private readonly CheckBox linked = new(), enabled = new(), reverse = new();
    private readonly CheckBox advanced = new();
    private readonly Label selectedStatus = new();
    private readonly Button identify = new();
    private readonly TextBox hex = new(), secondaryHex = new(), deviceName = new();
    private readonly NumericUpDown brightness = new(), speed = new(), width = new(), leds = new(), offset = new(), port = new(), fps = new();
    private readonly ComboBox effect = new(), backend = new();
    private readonly TableLayoutPanel stylePanel = Grid(), devicePanel = Grid(), lengthPanel = Grid();
    private readonly FlowLayoutPanel swatches = new() { AutoSize = true, Dock = DockStyle.Top };
    private readonly LedPreview preview = new();
    private readonly ToolTip tooltip = new();
    private List<DeviceDescriptor> descriptors = new();
    private List<DeviceDescriptor> visibleDescriptors = new();
    private Dictionary<string, DeviceResult> deviceResults = new();
    private bool updating;
    public event Action? ApplyRequested, StoreRequested;
    public event Action<HardwareBackend, int>? DiscoveryRequested;
    public event Action<DeviceDescriptor>? IdentifyRequested;
    private DeviceDescriptor? Selected => targets.SelectedIndex > 0 && targets.SelectedIndex <= visibleDescriptors.Count ? visibleDescriptors[targets.SelectedIndex - 1] : null;
    private DeviceSettings? SelectedConfig => Selected is { } d ? settings.GetDevice(d) : null;
    private LightingStyle SelectedStyle => SelectedConfig is { UseGlobal: false } d ? d.Style : settings;

    public MainForm(Settings settings)
    {
        this.settings = settings;
        Text = "RGB Tray"; Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi;
        BackColor = Background; ForeColor = Color.FromArgb(235, 240, 246);
        ClientSize = new(980, 720); MinimumSize = new(850, 650); StartPosition = FormStartPosition.CenterScreen;
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new(18) };
        root.RowStyles.Add(new(SizeType.AutoSize)); root.RowStyles.Add(new(SizeType.Percent, 100)); root.RowStyles.Add(new(SizeType.Absolute, 80)); Controls.Add(root);
        var top = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = false, Padding = new(0, 0, 0, 10) };
        top.Controls.Add(new Label { Text = "RGB", Font = new Font("Segoe UI", 25, FontStyle.Bold), AutoSize = true, Margin = new(0, 0, 30, 0) });
        master.Text = ""; ButtonStyle(master); master.AutoSize = true; master.Margin = new(0, 8, 10, 0); top.Controls.Add(master);
        top.Controls.Add(new Label { Text = "Кольори й ефекти для твого світла", AutoSize = true, ForeColor = Muted, Margin = new(4, 17, 0, 0) }); root.Controls.Add(top, 0, 0);
        var tabs = new DarkTabs { Dock = DockStyle.Fill, ItemSize = new(130, 32), SizeMode = TabSizeMode.Fixed };
        root.Controls.Add(tabs, 0, 1);
        TabPage lightingPage = Page("Підсвітка"), devicePage = Page("Пристрої"), systemPage = Page("Система"); tabs.TabPages.AddRange(new[] { lightingPage, devicePage, systemPage });
        BuildLighting(lightingPage); BuildDevices(devicePage); BuildSystem(systemPage);
        status.Dock = DockStyle.Fill; status.Padding = new(3, 10, 3, 0); status.ForeColor = Muted; status.Font = new Font("Segoe UI", 9); status.AccessibleName = "Стан пристроїв"; root.Controls.Add(status, 0, 2);
        master.Click += (_, _) => Toggle();
        targets.SelectedIndexChanged += (_, _) => RefreshSettings();
        FormClosing += (_, e) => { if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; Hide(); } };
        Resize += (_, _) => { if (WindowState == FormWindowState.Minimized) Hide(); };
        SetDevices(new(Array.Empty<DeviceDescriptor>(), "Пошук пристроїв…")); RefreshSettings();
    }
    private static TabPage Page(string name) => new(name) { BackColor = Background, ForeColor = Color.FromArgb(235, 240, 246), Padding = new(8) };
    private static TableLayoutPanel Grid() => new() { ColumnCount = 2, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Top, Padding = new(0, 5, 0, 5) };
    private static void ButtonStyle(Button b) { b.FlatStyle = FlatStyle.Flat; b.FlatAppearance.BorderSize = 0; b.BackColor = Surface; b.ForeColor = Color.White; b.Padding = new(8, 4, 8, 4); b.Cursor = Cursors.Hand; }
    private Button Button(string text, Action action)
    { var b = new Button { Text = text, AutoSize = true }; ButtonStyle(b); b.Click += (_, _) => Guard(action); return b; }
    private static void Field(Control c) { c.BackColor = Surface; c.ForeColor = Color.White; c.Margin = new(6); c.Anchor = AnchorStyles.Left | AnchorStyles.Right; }
    private static void Row(TableLayoutPanel grid, string text, Control control)
    {
        grid.ColumnStyles.Clear(); grid.ColumnStyles.Add(new(SizeType.Absolute, 155)); grid.ColumnStyles.Add(new(SizeType.Percent, 100));
        int row = grid.RowCount++; grid.RowStyles.Add(new(SizeType.AutoSize));
        grid.Controls.Add(new Label { Text = text, AutoSize = true, ForeColor = Muted, Anchor = AnchorStyles.Left, Margin = new(0, 9, 4, 9) }, 0, row);
        Field(control); grid.Controls.Add(control, 1, row);
    }
    private static FlowLayoutPanel Flow(params Control[] controls)
    { var flow = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, WrapContents = true, Margin = new(0, 3, 0, 3) }; flow.Controls.AddRange(controls); return flow; }
    private static void Number(NumericUpDown n, int min, int max, int step = 1) { n.Minimum = min; n.Maximum = max; n.Increment = step; n.Width = 130; }
    private static void Combo(ComboBox combo, object[] items) { combo.DropDownStyle = ComboBoxStyle.DropDownList; combo.FlatStyle = FlatStyle.Flat; combo.Items.AddRange(items); }

    private void BuildLighting(TabPage page)
    {
        var split = new SplitContainer { Dock = DockStyle.Fill, Size = new(900, 550), SplitterDistance = 230, Panel1MinSize = 190, Panel2MinSize = 440, FixedPanel = FixedPanel.Panel1 };
        page.Controls.Add(split);
        var left = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3 };
        left.RowStyles.Add(new(SizeType.AutoSize)); left.RowStyles.Add(new(SizeType.Percent, 100)); left.RowStyles.Add(new(SizeType.AutoSize)); split.Panel1.Controls.Add(left);
        left.Controls.Add(new Label { Text = "КЕРОВАНІ ВИХОДИ", ForeColor = Accent, AutoSize = true, Padding = new(3, 8, 3, 8) });
        targets.Dock = DockStyle.Fill; targets.BackColor = Surface; targets.ForeColor = ForeColor; targets.BorderStyle = BorderStyle.None; targets.ItemHeight = 42; targets.DrawMode = DrawMode.OwnerDrawFixed;
        targets.DrawItem += (_, e) => { if (e.Index < 0) return; using var brush = new SolidBrush((e.State & DrawItemState.Selected) != 0 ? Color.FromArgb(40, 75, 70) : Surface); e.Graphics.FillRectangle(brush, e.Bounds); TextRenderer.DrawText(e.Graphics, targets.Items[e.Index].ToString(), Font, Rectangle.Inflate(e.Bounds, -8, 0), ForeColor, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis); };
        left.Controls.Add(targets);
        left.Controls.Add(Button("Оновити пристрої", () => DiscoveryRequested?.Invoke(settings.Backend, settings.OpenRgbPort)));
        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new(18, 3, 8, 10) }; split.Panel2.Controls.Add(scroll);
        var editor = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1 }; scroll.Controls.Add(editor);
        heading.Font = new Font("Segoe UI", 18, FontStyle.Bold); heading.AutoSize = true; heading.Margin = new(0, 0, 0, 6); editor.Controls.Add(heading);
        detail.AutoSize = true; detail.ForeColor = Muted; detail.MaximumSize = new(560, 0); detail.Margin = new(0, 0, 0, 8); editor.Controls.Add(detail);
        selectedStatus.AutoSize = true; selectedStatus.ForeColor = Accent; selectedStatus.MaximumSize = new(560, 0); editor.Controls.Add(selectedStatus);
        linked.Text = "Використовувати спільний колір та ефект"; linked.AutoSize = true; editor.Controls.Add(linked);
        identify.Text = "Підсвітити вибраний"; ButtonStyle(identify); identify.AutoSize = true;
        identify.Click += (_, _) => { if (Selected is { } d) IdentifyRequested?.Invoke(d); };
        editor.Controls.Add(Flow(identify, Button("Застосувати спільне до всіх", () => { foreach (var d in settings.Devices) d.UseGlobal = true; RebuildTargets(); RefreshSettings(); ApplyRequested?.Invoke(); })));
        Number(leds, 1, 4096); leds.AccessibleName = "Кількість діодів у зоні";
        Row(lengthPanel, "Кількість діодів", leds); editor.Controls.Add(lengthPanel);
        preview.Dock = DockStyle.Top; editor.Controls.Add(preview);
        editor.Controls.Add(new Label { Text = "Попередній перегляд", AutoSize = true, ForeColor = Muted, Font = new Font("Segoe UI", 8), Margin = new(0, 3, 0, 5) });
        stylePanel.Dock = DockStyle.Top; editor.Controls.Add(stylePanel);
        enabled.Text = "Світло ввімкнене"; enabled.AutoSize = true; Row(stylePanel, "Живлення", enabled);
        Combo(effect, EffectCatalog.Names.Cast<object>().ToArray()); Row(stylePanel, "Ефект", effect);
        hex.Width = 100; hex.CharacterCasing = CharacterCasing.Upper; hex.MaxLength = 7; hex.AccessibleName = "Основний HEX колір";
        secondaryHex.Width = 100; secondaryHex.CharacterCasing = CharacterCasing.Upper; secondaryHex.MaxLength = 7;
        Row(stylePanel, "Основний колір", Flow(hex, Button("Палітра…", () => PickColor(false)), Button("Застосувати", () => CommitHex(false))));
        var palette = new[] { "#FF8040", "#FF375F", "#C65CFF", "#536CFF", "#27C8FF", "#64E6C6", "#FFE092", "#FFFFFF" };
        foreach (var c in palette) { string color = c; var b = Button("", () => ChangeStyle(s => s.Color = color)); b.AutoSize = false; b.Size = new(27, 27); b.Padding = Padding.Empty; b.BackColor = Settings.ParseColor(c); b.AccessibleName = "Колір " + c; tooltip.SetToolTip(b, c); swatches.Controls.Add(b); }
        Row(stylePanel, "Швидкі кольори", swatches);
        Row(stylePanel, "Другий колір", Flow(secondaryHex, Button("Палітра…", () => PickColor(true)), Button("Застосувати", () => CommitHex(true))));
        Number(brightness, 0, 100); Row(stylePanel, "Яскравість, %", brightness);
        Number(speed, 100, 1000, 20); Row(stylePanel, "Час кроку, мс", speed);
        Number(width, 1, 120); Row(stylePanel, "Темних діодів", width);
        hint.AutoSize = true; hint.MaximumSize = new(560, 0); hint.ForeColor = Muted; hint.Margin = new(0, 7, 0, 9); editor.Controls.Add(hint);
        advanced.Text = "Назва, напрямок і зміщення"; advanced.AutoSize = true; advanced.ForeColor = Muted;
        advanced.CheckedChanged += (_, _) => devicePanel.Visible = advanced.Checked && Selected is not null;
        editor.Controls.Add(advanced); editor.Controls.Add(devicePanel);
        deviceName.MaxLength = 100; Row(devicePanel, "Назва", deviceName);
        reverse.Text = "Зворотний напрямок"; reverse.AutoSize = true; Row(devicePanel, "Напрямок", reverse);
        Number(offset, 0, 4095); Row(devicePanel, "Зміщення початку", offset);
        linked.CheckedChanged += (_, _) => { if (updating || SelectedConfig is not { } d) return; if (!linked.Checked && !d.HasCustomStyle) { d.Style = settings.Copy(); d.HasCustomStyle = true; } d.UseGlobal = linked.Checked; RebuildTargets(); RefreshSettings(); ApplyRequested?.Invoke(); };
        enabled.CheckedChanged += (_, _) => ChangeStyle(s => s.Enabled = enabled.Checked);
        effect.SelectedIndexChanged += (_, _) => { if (effect.SelectedIndex >= 0) ChangeStyle(s => s.Effect = (LightingEffect)effect.SelectedIndex); };
        brightness.ValueChanged += (_, _) => ChangeStyle(s => s.Brightness = (int)brightness.Value);
        speed.ValueChanged += (_, _) => ChangeStyle(s => s.StepMilliseconds = (int)speed.Value);
        width.ValueChanged += (_, _) => ChangeStyle(s => s.DarkWidth = (int)width.Value);
        deviceName.Leave += (_, _) => { if (SelectedConfig is { } d && !updating) { d.Name = deviceName.Text.Trim(); RebuildTargets(); StoreRequested?.Invoke(); } };
        leds.ValueChanged += (_, _) =>
        {
            if (updating) return;
            if (SelectedConfig is null)
            {
                settings.LedCount = (int)leds.Value;
                foreach (var descriptor in visibleDescriptors.Where(d => d.ConfigurableLength)) { var d = settings.GetDevice(descriptor); d.LedCount = Math.Min(settings.LedCount, descriptor.MaxLeds); d.Offset %= d.LedCount; }
                RefreshSettings(); ApplyRequested?.Invoke();
            }
            else ChangeDevice(d => { d.LedCount = (int)leds.Value; d.Offset %= d.LedCount; });
        };
        reverse.CheckedChanged += (_, _) => ChangeDevice(d => d.Reverse = reverse.Checked);
        offset.ValueChanged += (_, _) => ChangeDevice(d => d.Offset = (int)offset.Value);
        scroll.Resize += (_, _) => { editor.Width = Math.Max(440, scroll.ClientSize.Width - scroll.Padding.Horizontal - 20); };
    }

    private void BuildDevices(TabPage page)
    {
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new(12) };
        layout.RowStyles.Add(new(SizeType.AutoSize)); layout.RowStyles.Add(new(SizeType.AutoSize));
        layout.RowStyles.Add(new(SizeType.Percent, 100)); layout.RowStyles.Add(new(SizeType.AutoSize)); layout.RowStyles.Add(new(SizeType.AutoSize));
        page.Controls.Add(layout);
        layout.Controls.Add(new Label { Text = "Вибери виходи, які використовуєш", Font = new Font(Font, FontStyle.Bold), AutoSize = true });
        layout.Controls.Add(new Label { Text = "Прапорець додає вихід на головну вкладку. ARGB-хедери — це доступні роз’єми, а не знайдені вентилятори. Їхній контролер не повідомляє, чи під’єднаний кабель.", AutoSize = true, MaximumSize = new(760, 0), ForeColor = Muted, Margin = new(0, 8, 0, 12) });
        available.Dock = DockStyle.Fill; available.BackColor = Surface; available.ForeColor = ForeColor; available.BorderStyle = BorderStyle.None; available.CheckOnClick = true; available.IntegralHeight = false;
        layout.Controls.Add(available);
        availableHint.AutoSize = true; availableHint.MaximumSize = new(760, 0); availableHint.ForeColor = Muted; availableHint.Margin = new(0, 10, 0, 8); layout.Controls.Add(availableHint);
        layout.Controls.Add(Flow(Button("Підсвітити вибраний", () => { if (available.SelectedIndex >= 0) IdentifyRequested?.Invoke(descriptors[available.SelectedIndex]); }),
            Button("Лише цей ARGB-хедер", () =>
            {
                if (available.SelectedIndex < 0) return;
                var selected = descriptors[available.SelectedIndex];
                if (!selected.Id.StartsWith("native:msi:argb:", StringComparison.Ordinal)) return;
                foreach (var d in descriptors.Where(d => d.Id.StartsWith("native:msi:argb:", StringComparison.Ordinal))) settings.GetDevice(d).Managed = d.Id == selected.Id;
                RebuildTargets(); RefreshSettings(); ApplyRequested?.Invoke();
            }), Button("Оновити", () => DiscoveryRequested?.Invoke(settings.Backend, settings.OpenRgbPort))));
        available.SelectedIndexChanged += (_, _) => { availableHint.Text = available.SelectedIndex < 0 ? "" : descriptors[available.SelectedIndex].ConfigurableLength ? "Підсвіти вихід, щоб визначити роз’єм. Кількість діодів задається на вкладці «Підсвітка» після вибору пристрою." : "Кількість діодів цієї зони визначена контролером. У відеокарти доступний один спільний колір."; };
        available.ItemCheck += (_, e) =>
        {
            if (updating || e.Index >= descriptors.Count) return;
            settings.GetDevice(descriptors[e.Index]).Managed = e.NewValue == CheckState.Checked;
            // The check event precedes the actual UI state change.
            BeginInvoke(() => { RebuildTargets(); RefreshSettings(); ApplyRequested?.Invoke(); });
        };
    }

    private void BuildSystem(TabPage page)
    {
        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new(15) }; page.Controls.Add(scroll);
        var grid = Grid(); scroll.Controls.Add(grid);
        Combo(backend, new object[] { "Нативно · MSI Mortar / Gainward Phoenix", "OpenRGB SDK · інше сумісне обладнання" }); Row(grid, "Керування", backend);
        Number(port, 1, 65535); Row(grid, "Локальний порт", port);
        Row(grid, "Підключення", Button("Підключитися та оновити", () => DiscoveryRequested?.Invoke((HardwareBackend)backend.SelectedIndex, (int)port.Value)));
        Number(fps, 10, 60, 5); Row(grid, "Кадрів за секунду", fps);
        fps.ValueChanged += (_, _) => { if (updating) return; settings.FramesPerSecond = (int)fps.Value; ApplyRequested?.Invoke(); };
        var startup = new CheckBox { Text = "Запускати з Windows", AutoSize = true, Checked = Startup.Enabled };
        bool resettingStartup = false;
        startup.CheckedChanged += (_, _) =>
        {
            if (resettingStartup) return;
            Guard(() =>
            {
                try { Startup.Enabled = startup.Checked; }
                catch { resettingStartup = true; try { startup.Checked = Startup.Enabled; } finally { resettingStartup = false; } throw; }
            });
        }; Row(grid, "Автозапуск", startup);
        Row(grid, "Журнал", Button("Відкрити папку", () => { Directory.CreateDirectory(Settings.DataDirectory); System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("explorer.exe", Settings.DataDirectory) { UseShellExecute = true }); }));
        discovery.AutoSize = true; discovery.MaximumSize = new(620, 0); discovery.ForeColor = Muted; Row(grid, "Сумісність", discovery);
        Row(grid, "Інші ПК", new Label { Text = "Нативно перевірені MSI MAG B850M MORTAR WIFI і Gainward RTX 5080 Phoenix. Інші моделі потребують свого модуля або OpenRGB. Для SDK запусти OpenRGB і ввімкни сервер на 127.0.0.1:6742. Тут використовується лише локальне підключення; OpenRGB не потрібен для нативного режиму.", AutoSize = true, MaximumSize = new(620, 0), ForeColor = Muted });
    }

    private void ChangeStyle(Action<LightingStyle> change)
    {
        if (updating) return;
        if (SelectedConfig is { UseGlobal: true }) return;
        change(SelectedStyle); RefreshSettings(); ApplyRequested?.Invoke();
    }
    private void ChangeDevice(Action<DeviceSettings> change)
    { if (updating || SelectedConfig is not { } d) return; change(d); RefreshSettings(); ApplyRequested?.Invoke(); }
    private void Guard(Action action)
    { try { action(); } catch (Exception ex) { Log.Write(ex.ToString()); SetStatus(ex.Message, false); } }
    private void CommitHex(bool secondary)
    {
        var text = secondary ? secondaryHex.Text : hex.Text;
        _ = Settings.ParseColor(text); ChangeStyle(s => { if (secondary) s.SecondaryColor = text; else s.Color = text; });
    }
    private void PickColor(bool secondary)
    {
        using var dialog = new ColorDialog { FullOpen = true, Color = Settings.ParseColor(secondary ? SelectedStyle.SecondaryColor : SelectedStyle.Color) };
        if (dialog.ShowDialog(this) == DialogResult.OK) ChangeStyle(s => { var c = dialog.Color; var value = $"#{c.R:X2}{c.G:X2}{c.B:X2}"; if (secondary) s.SecondaryColor = value; else s.Color = value; });
    }
    public void SetDevices(DiscoveryResult result)
    {
        descriptors = result.Devices.ToList(); foreach (var d in descriptors) _ = settings.GetDevice(d);
        discovery.Text = result.Description; RebuildTargets(); RefreshSettings();
    }
    private void RebuildTargets()
    {
        string? selectedId = Selected?.Id; int availableIndex = available.SelectedIndex; bool old = updating; updating = true;
        visibleDescriptors = descriptors.Where(d => settings.GetDevice(d).Managed).ToList();
        targets.Items.Clear(); targets.Items.Add("Усе разом");
        foreach (var d in visibleDescriptors) { var c = settings.GetDevice(d); string marker = deviceResults.TryGetValue(d.Id, out var r) && !r.Success ? "! " : "● "; targets.Items.Add(marker + (string.IsNullOrWhiteSpace(c.Name) ? d.Name : c.Name) + (c.UseGlobal ? "  ↗" : "")); }
        targets.SelectedIndex = selectedId is null ? 0 : Math.Max(0, visibleDescriptors.FindIndex(d => d.Id == selectedId) + 1);
        available.Items.Clear();
        foreach (var d in descriptors) { var c = settings.GetDevice(d); available.Items.Add((string.IsNullOrWhiteSpace(c.Name) ? d.Name : c.Name) + (d.ConfigurableLength ? "  ·  підключення невідоме" : "  ·  контролер знайдено"), c.Managed); }
        if (availableIndex >= 0 && availableIndex < available.Items.Count) available.SelectedIndex = availableIndex;
        updating = old;
    }
    public void RefreshSettings()
    {
        bool old = updating; updating = true;
        var device = Selected; var config = SelectedConfig; var style = SelectedStyle;
        heading.Text = config is null ? "Усе разом" : string.IsNullOrWhiteSpace(config.Name) ? device!.Name : config.Name;
        detail.Text = config is null ? "Спільний стиль для всіх прив’язаних пристроїв. Окремі стилі залишаються незалежними." : device!.SingleZone ? "Одна RGB-зона. Кольори, дихання та переходи доступні; темний блок тут дає сталий колір." : "Окрема адресована зона. Вентилятори на паралельному розгалужувачі повторюють один сигнал.";
        linked.Visible = config is not null;
        linked.Checked = config?.UseGlobal ?? false;
        bool editable = config is null || (config.Managed && !config.UseGlobal);
        SetEditable(stylePanel, editable);
        enabled.Checked = style.Enabled; effect.SelectedIndex = (int)style.Effect; hex.Text = style.Color; secondaryHex.Text = style.SecondaryColor;
        brightness.Value = style.Brightness; speed.Value = style.StepMilliseconds; width.Value = style.DarkWidth;
        speed.Enabled = editable && style.Effect != LightingEffect.Solid; width.Enabled = editable && style.Effect == LightingEffect.CircularDarkBand;
        secondaryHex.Enabled = editable && style.Effect == LightingEffect.ColorCycle;
        advanced.Visible = config is not null; devicePanel.Visible = config is not null && advanced.Checked;
        lengthPanel.Visible = device is null || !device.SingleZone;
        if (device is null) { leds.Maximum = 120; leds.Value = settings.LedCount; leds.Enabled = true; }
        identify.Enabled = config is not null;
        selectedStatus.Visible = device is not null && deviceResults.ContainsKey(device.Id);
        if (device is not null && deviceResults.TryGetValue(device.Id, out var result)) { selectedStatus.Text = result.Message; selectedStatus.ForeColor = result.Success ? Accent : Color.FromArgb(255, 171, 125); }
        if (device is not null && config is not null)
        {
            deviceName.Text = config.Name; leds.Maximum = device.MaxLeds;
            leds.Value = Math.Clamp(device.ConfigurableLength ? config.LedCount : device.LedCount, 1, device.MaxLeds); leds.Enabled = device.ConfigurableLength;
            offset.Maximum = Math.Max(0, (int)leds.Value - 1); offset.Value = Math.Min(config.Offset, offset.Maximum);
            reverse.Checked = config.Reverse; reverse.Enabled = offset.Enabled = !device.SingleZone;
        }
        master.Text = settings.Enabled ? "●  Вимкнути все" : "○  Увімкнути все";
        SetRowVisible(stylePanel, enabled, config is not null);
        SetRowVisible(stylePanel, secondaryHex.Parent!, style.Effect == LightingEffect.ColorCycle);
        SetRowVisible(stylePanel, hex.Parent!, style.Effect != LightingEffect.Rainbow);
        SetRowVisible(stylePanel, swatches, style.Effect != LightingEffect.Rainbow);
        SetRowVisible(stylePanel, brightness, style.Effect != LightingEffect.Rainbow);
        SetRowVisible(stylePanel, speed, style.Effect is not (LightingEffect.Solid or LightingEffect.Rainbow));
        SetRowVisible(stylePanel, width, style.Effect == LightingEffect.CircularDarkBand);
        hint.Text = config is { UseGlobal: true } ? "Прив’язано до спільного стилю. Щоб змінити окремо, зніми прапорець вище." : style.Effect switch { LightingEffect.CircularDarkBand => "Темний блок рухається по колу. Ширина задається числом темних діодів.", LightingEffect.SequentialFill => "Діоди плавно загоряються, потім гаснуть по одному.", LightingEffect.Rainbow => "Готова веселка: повний спектр, яскравість 50% і плавний оберт за 12 секунд.", LightingEffect.ColorCycle => "Плавний перехід між основним і другим кольором.", _ => "Менший час кроку — швидша анімація. Режим сталого кольору не потребує циклу кадрів." };
        backend.SelectedIndex = (int)settings.Backend; port.Value = settings.OpenRgbPort; fps.Value = settings.FramesPerSecond;
        var previewStyle = style.Copy(); if (!settings.Enabled) previewStyle.Enabled = false;
        var previewDevice = device ?? new DeviceDescriptor("preview", "Попередній перегляд", settings.LedCount, 120, true, false);
        preview.SetTarget(new(previewDevice, previewStyle, device is null ? settings.LedCount : (int)leds.Value, config?.Reverse ?? false, config?.Offset ?? 0, settings.LedCount));
        updating = old;
    }
    public void SelectColor(string color) { settings.Color = color; settings.Enabled = true; RefreshSettings(); ApplyRequested?.Invoke(); }
    public void SelectEffect(LightingEffect selected) { settings.Effect = selected; settings.Enabled = true; RefreshSettings(); ApplyRequested?.Invoke(); }
    public void Toggle() { settings.Enabled = !settings.Enabled; RefreshSettings(); ApplyRequested?.Invoke(); }
    public void SetStatus(string text, bool success) { status.Text = text; status.ForeColor = success ? Accent : Color.FromArgb(255, 171, 125); tooltip.SetToolTip(status, text); }
    public void SetResults(IReadOnlyList<DeviceResult> results)
    { deviceResults = results.Where(r => r.Id is not null).ToDictionary(r => r.Id!, r => r); RebuildTargets(); RefreshSettings(); }
    private static void SetEditable(Control parent, bool editable)
    {
        foreach (Control child in parent.Controls)
        {
            if (child is Label) continue;
            if (child is Panel) SetEditable(child, editable); else child.Enabled = editable;
        }
    }
    private static void SetRowVisible(TableLayoutPanel grid, Control control, bool visible)
    {
        int row = grid.GetPositionFromControl(control).Row;
        if (row < 0) return;
        foreach (Control child in grid.Controls) if (grid.GetPositionFromControl(child).Row == row) child.Visible = visible;
    }
    public void Open() { Show(); WindowState = FormWindowState.Normal; Activate(); }
    protected override void Dispose(bool disposing) { if (disposing) tooltip.Dispose(); base.Dispose(disposing); }
}
