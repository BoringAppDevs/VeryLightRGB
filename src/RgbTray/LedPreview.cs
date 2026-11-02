using System.Diagnostics;
using RgbTray.Effects;
using RgbTray.Hardware;

namespace RgbTray;

internal sealed class LedPreview : Control
{
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 50 };
    private readonly Stopwatch clock = new();
    private FrameRenderer? renderer;
    private bool animated;
    public LedPreview()
    {
        DoubleBuffered = true; Height = 68; BackColor = Color.FromArgb(24, 29, 36);
        timer.Tick += (_, _) => Invalidate(); VisibleChanged += (_, _) => UpdateTimer();
    }
    public void SetTarget(TargetPlan target)
    {
        renderer = new(target); animated = target.Animated; clock.Restart(); UpdateTimer(); Invalidate();
    }
    private void UpdateTimer() { if (Visible && animated) timer.Start(); else timer.Stop(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (renderer is null) return;
        var rgb = renderer.Render(clock.Elapsed.TotalMilliseconds);
        int count = Math.Min(rgb.Length / 3, 64);
        float gap = 3, width = Math.Max(2, (ClientSize.Width - 24 - gap * (count - 1)) / (float)count);
        for (int led = 0; led < count; led++)
        {
            int source = led * (rgb.Length / 3) / count * 3;
            using var brush = new SolidBrush(Color.FromArgb(rgb[source], rgb[source + 1], rgb[source + 2]));
            var rect = new RectangleF(12 + led * (width + gap), 16, width, 30);
            e.Graphics.FillRectangle(brush, rect); e.Graphics.DrawRectangle(Pens.DimGray, rect.X, rect.Y, rect.Width, rect.Height);
        }
    }
    protected override void Dispose(bool disposing) { if (disposing) timer.Dispose(); base.Dispose(disposing); }
}
