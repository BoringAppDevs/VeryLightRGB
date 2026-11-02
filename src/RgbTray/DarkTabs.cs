namespace RgbTray;

internal sealed class DarkTabs : TabControl
{
    public DarkTabs() => SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Color.FromArgb(18, 21, 26));
        for (int i = 0; i < TabPages.Count; i++)
        {
            var bounds = GetTabRect(i);
            using var brush = new SolidBrush(i == SelectedIndex ? Color.FromArgb(29, 34, 42) : Color.FromArgb(18, 21, 26));
            e.Graphics.FillRectangle(brush, bounds);
            TextRenderer.DrawText(e.Graphics, TabPages[i].Text, Font, bounds, i == SelectedIndex ? Color.FromArgb(100, 230, 198) : ForeColor, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            if (Focused && i == SelectedIndex) ControlPaint.DrawFocusRectangle(e.Graphics, Rectangle.Inflate(bounds, -4, -4));
        }
    }
    protected override void OnSelectedIndexChanged(EventArgs e) { base.OnSelectedIndexChanged(e); Invalidate(); }
}
