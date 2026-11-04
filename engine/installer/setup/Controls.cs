using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;

namespace OzSetup
{
    /// <summary>Text with letter spacing, as the menus' tracked caps (WPF's TextBlock has none).</summary>
    public class Tracked : FrameworkElement
    {
        public static readonly DependencyProperty TextProperty = DependencyProperty.Register(nameof(Text), typeof(string), typeof(Tracked),
            new FrameworkPropertyMetadata("", FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
        /// <summary>Space between letters, in ems.</summary>
        public static readonly DependencyProperty TrackingProperty = DependencyProperty.Register(nameof(Tracking), typeof(double), typeof(Tracked),
            new FrameworkPropertyMetadata(0.2, FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender));
        public static readonly DependencyProperty FontFamilyProperty = TextElement.FontFamilyProperty.AddOwner(typeof(Tracked),
            new FrameworkPropertyMetadata(SystemFonts.MessageFontFamily, FrameworkPropertyMetadataOptions.Inherits | FrameworkPropertyMetadataOptions.AffectsMeasure));
        public static readonly DependencyProperty FontSizeProperty = TextElement.FontSizeProperty.AddOwner(typeof(Tracked),
            new FrameworkPropertyMetadata(12.0, FrameworkPropertyMetadataOptions.Inherits | FrameworkPropertyMetadataOptions.AffectsMeasure));
        public static readonly DependencyProperty FontWeightProperty = TextElement.FontWeightProperty.AddOwner(typeof(Tracked),
            new FrameworkPropertyMetadata(FontWeights.Normal, FrameworkPropertyMetadataOptions.Inherits | FrameworkPropertyMetadataOptions.AffectsMeasure));
        public static readonly DependencyProperty ForegroundProperty = TextElement.ForegroundProperty.AddOwner(typeof(Tracked),
            new FrameworkPropertyMetadata(Brushes.White, FrameworkPropertyMetadataOptions.Inherits | FrameworkPropertyMetadataOptions.AffectsRender));

        public string Text { get => (string)GetValue(TextProperty); set => SetValue(TextProperty, value); }
        public double Tracking { get => (double)GetValue(TrackingProperty); set => SetValue(TrackingProperty, value); }
        public FontFamily FontFamily { get => (FontFamily)GetValue(FontFamilyProperty); set => SetValue(FontFamilyProperty, value); }
        public double FontSize { get => (double)GetValue(FontSizeProperty); set => SetValue(FontSizeProperty, value); }
        public FontWeight FontWeight { get => (FontWeight)GetValue(FontWeightProperty); set => SetValue(FontWeightProperty, value); }
        public Brush Foreground { get => (Brush)GetValue(ForegroundProperty); set => SetValue(ForegroundProperty, value); }

        FormattedText Glyph(string s) => new FormattedText(s, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
            new Typeface(FontFamily, FontStyles.Normal, FontWeight, FontStretches.Normal), FontSize, Foreground,
            VisualTreeHelper.GetDpi(this).PixelsPerDip);

        protected override Size MeasureOverride(Size available)
        {
            string text = Text ?? "";
            if (text.Length == 0)
                return new Size(0, Glyph("A").Height);
            double w = 0;
            foreach (char c in text)
                w += Glyph(c.ToString()).WidthIncludingTrailingWhitespace;
            w += Tracking * FontSize * (text.Length - 1);
            return new Size(Math.Ceiling(w), Glyph(text).Height);
        }

        protected override void OnRender(DrawingContext dc)
        {
            double x = 0, gap = Tracking * FontSize;
            foreach (char c in Text ?? "")
            {
                var g = Glyph(c.ToString());
                dc.DrawText(g, new Point(x, 0));
                x += g.WidthIncludingTrailingWhitespace + gap;
            }
        }
    }

    /// <summary>The main menu's drifting embers: warm sparks rising and flickering out.</summary>
    public class Embers : FrameworkElement
    {
        class Spark { public double X, Y, R, Vy, Phase, Sway, Life, Age; }

        readonly List<Spark> sparks = new List<Spark>();
        readonly Random rng = new Random();
        readonly RadialGradientBrush hot, warm;
        TimeSpan last = TimeSpan.Zero;

        public Embers()
        {
            IsHitTestVisible = false;
            hot = Ember(Color.FromRgb(0xff, 0xc8, 0x6a), Color.FromRgb(0xff, 0x4a, 0x1c));
            warm = Ember(Color.FromRgb(0xff, 0x8a, 0x3a), Color.FromRgb(0xdb, 0x24, 0x10));
            Loaded += (s, e) => CompositionTarget.Rendering += Tick;
            Unloaded += (s, e) => CompositionTarget.Rendering -= Tick;
        }

        static RadialGradientBrush Ember(Color core, Color edge)
        {
            var b = new RadialGradientBrush();
            b.GradientStops.Add(new GradientStop(core, 0));
            b.GradientStops.Add(new GradientStop(Color.FromArgb(0x90, edge.R, edge.G, edge.B), 0.35));
            b.GradientStops.Add(new GradientStop(Color.FromArgb(0, edge.R, edge.G, edge.B), 1));
            b.Freeze();
            return b;
        }

        Spark Make(bool anywhere)
        {
            double w = Math.Max(ActualWidth, 1), h = Math.Max(ActualHeight, 1);
            return new Spark
            {
                X = rng.NextDouble() * w,
                Y = anywhere ? rng.NextDouble() * h : h + 10,
                R = 1.2 + Math.Pow(rng.NextDouble(), 3) * 4.5,
                Vy = 14 + rng.NextDouble() * 34,
                Phase = rng.NextDouble() * Math.PI * 2,
                Sway = 6 + rng.NextDouble() * 22,
                Life = 6 + rng.NextDouble() * 10,
                Age = anywhere ? rng.NextDouble() * 6 : 0,
            };
        }

        void Tick(object sender, EventArgs e)
        {
            var now = ((RenderingEventArgs)e).RenderingTime;
            if (now == last)
                return;
            double dt = last == TimeSpan.Zero ? 0 : Math.Min((now - last).TotalSeconds, 0.1);
            last = now;
            if (ActualWidth < 2)
                return;
            int want = (int)(ActualWidth * ActualHeight / 14000);
            while (sparks.Count < want)
                sparks.Add(Make(sparks.Count < want / 2 && dt == 0));
            for (int i = 0; i < sparks.Count; i++)
            {
                var s = sparks[i];
                s.Age += dt;
                s.Y -= s.Vy * dt;
                s.X += Math.Sin(s.Age * 0.9 + s.Phase) * s.Sway * dt;
                if (s.Age > s.Life || s.Y < -20)
                    sparks[i] = Make(false);
            }
            InvalidateVisual();
        }

        protected override void OnRender(DrawingContext dc)
        {
            foreach (var s in sparks)
            {
                double fadeIn = Math.Min(s.Age / 1.2, 1), fadeOut = Math.Min((s.Life - s.Age) / 2.5, 1);
                double flicker = 0.7 + 0.3 * Math.Sin(s.Age * 7 + s.Phase * 3);
                double a = Math.Max(0, fadeIn * fadeOut * flicker);
                if (a <= 0.01)
                    continue;
                dc.PushOpacity(a);
                double r = s.R * 3;
                dc.DrawEllipse(s.R > 3 ? warm : hot, null, new Point(s.X, s.Y), r, r);
                dc.Pop();
            }
        }
    }
}
