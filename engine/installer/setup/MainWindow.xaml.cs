using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using System.Windows.Threading;
using Path = System.IO.Path;

namespace OzSetup
{
    public partial class MainWindow : Window
    {
        enum Mode { Install, Uninstall }
        enum After { Play, Close, Retry, Remove, Home }

        readonly Mode mode;
        readonly Options opts = new Options();
        string gameDir, gameHow, uninstallDir;
        bool dirPicked, autoStart, working, closeWhenStopped;
        /// <summary>/update: the game's Update now (oz_update.c) started Setup and is closing. Setup waits for it,
        /// updates the folder with the choices it was installed with, and starts the game again.</summary>
        bool updateMode;
        /// <summary>/offline: install from the files beside Setup, never asking GitHub.</summary>
        bool offlineOnly;
        Release release;
        Progress prog;
        CancellationTokenSource cts;
        readonly DispatcherTimer ticker = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(120) };
        long lastBytes;
        DateTime lastTick;
        double speed;
        After mainAction, otherAction;

        public MainWindow()
        {
            InitializeComponent();
            var args = Environment.GetCommandLineArgs().Skip(1).ToList();
            mode = args.Contains("/uninstall") ? Mode.Uninstall : Mode.Install;
            foreach (var a in args)
            {
                if (a.StartsWith("/dir=")) { opts.Dir = a.Substring(5); dirPicked = true; }
                else if (a.StartsWith("/desktop=")) opts.Desktop = a.EndsWith("1");
                else if (a.StartsWith("/links=")) opts.Links = a.EndsWith("1");
                else if (a == "/go") autoStart = true;
                else if (a == "/update") updateMode = true;
                else if (a == "/offline") offlineOnly = true;
                else if (a == "/filesonly") opts.FilesOnly = true;
            }
            ticker.Tick += (s, e) => Tick();
            Loaded += OnLoaded;
            PreviewKeyDown += OnKey;
            Closing += OnClosing;
        }

        void OnLoaded(object sender, RoutedEventArgs e)
        {
            // the arena drifts slowly, as the menus' backdrop
            var zoom = new DoubleAnimation(1.08, 1.0, TimeSpan.FromSeconds(28)) { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever,
                EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut } };
            bgZoom.BeginAnimation(ScaleTransform.ScaleXProperty, zoom);
            bgZoom.BeginAnimation(ScaleTransform.ScaleYProperty, zoom);
            var shimmer = new DoubleAnimation(-80, 520, TimeSpan.FromSeconds(1.6)) { RepeatBehavior = RepeatBehavior.Forever };
            shimmerX.BeginAnimation(TranslateTransform.XProperty, shimmer);
            Intro();

            if (mode == Mode.Uninstall)
                StartUninstallMode();
            else
                StartInstallMode();
        }

        /// <summary>The panel slides in from the right and the logo fades up, as a menu page opens.</summary>
        void Intro()
        {
            var t = new TranslateTransform(40, 0);
            panel.RenderTransform = t;
            panel.Opacity = 0;
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            t.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(40, 0, TimeSpan.FromMilliseconds(520)) { EasingFunction = ease });
            panel.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(420)));
            logo.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(900)));
            for (int i = 0; i < 3; i++)
            {
                var g = (Grid)steps.Children[i];
                var tt = new TranslateTransform(-30, 0);
                g.RenderTransform = tt;
                var delay = TimeSpan.FromMilliseconds(150 + i * 90);
                tt.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(-30, 0, TimeSpan.FromMilliseconds(450)) { BeginTime = delay, EasingFunction = ease });
                g.Opacity = 0;
                g.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(400)) { BeginTime = delay });
            }
        }

        // ---------------------------------------------------------------- steps on the left

        void Steps(int active, params (string title, string hint)[] rows)
        {
            for (int i = 0; i < 3; i++)
            {
                var g = (Grid)steps.Children[i];
                g.Children.Clear();
                bool on = i + 1 == active, done = i + 1 < active;
                var row = new Grid { Height = on ? 70 : 44 };
                if (on)
                {
                    // the selected row's red bar and wash, as the main menu's PLAY
                    row.Children.Add(new Rectangle
                    {
                        Fill = new LinearGradientBrush(Color.FromArgb(0x90, 0xb3, 0x17, 0x0a), Color.FromArgb(0, 0xb3, 0x17, 0x0a), 0),
                        Width = 360, HorizontalAlignment = HorizontalAlignment.Left,
                    });
                    row.Children.Add(new Rectangle { Width = 3, HorizontalAlignment = HorizontalAlignment.Left, Fill = (Brush)FindResource("RedHot") });
                }
                var stack = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = on ? VerticalAlignment.Top : VerticalAlignment.Center, Margin = new Thickness(22, on ? 8 : 0, 0, 0) };
                stack.Children.Add(new Tracked
                {
                    Text = done ? "✓" : "0" + (i + 1),
                    FontFamily = (FontFamily)FindResource("BarlowXBold"),
                    FontSize = done ? 12 : 9.5,
                    Tracking = 0.2,
                    Foreground = (Brush)FindResource(done ? "Green" : on ? "Text2" : "Dim"),
                    Width = 30,
                    VerticalAlignment = VerticalAlignment.Center,
                });
                stack.Children.Add(new Tracked
                {
                    Text = rows[i].title,
                    FontFamily = (FontFamily)FindResource("BarlowBlack"),
                    FontSize = on ? 32 : 25,
                    Tracking = 0.1,
                    Foreground = on ? Brushes.White : new SolidColorBrush(Color.FromArgb(done ? (byte)0xb0 : (byte)0x70, 0xe6, 0xdd, 0xd2)),
                    Effect = on ? new System.Windows.Media.Effects.DropShadowEffect { Color = Color.FromRgb(0xdb, 0x24, 0x10), BlurRadius = 22, ShadowDepth = 0, Opacity = 0.6 } : null,
                });
                row.Children.Add(stack);
                if (on)
                    row.Children.Add(new TextBlock
                    {
                        Text = rows[i].hint, FontSize = 12.5, Foreground = (Brush)FindResource("Muted"),
                        VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(52, 0, 0, 8),
                    });
                g.Children.Add(row);
            }
        }

        void InstallSteps(int active) => Steps(active,
            ("FIND GAME", "Your Quake III Arena, and where Oz Arena goes"),
            ("INSTALL", "The game, the fighters and the remastered world"),
            ("PLAY", "Into the arena"));

        void UninstallSteps(int active) => Steps(active,
            ("CONFIRM", "What goes and what stays"),
            ("REMOVE", "Oz Arena's files, shortcuts and links"),
            ("DONE", "Your game and your career stay"));

        void Show(Panel page)
        {
            foreach (var p in new Panel[] { pgHome, pgWork, pgMessage })
                p.Visibility = p == page ? Visibility.Visible : Visibility.Collapsed;
            page.Opacity = 0;
            page.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(260)));
        }

        // ---------------------------------------------------------------- install

        void StartInstallMode()
        {
            InstallSteps(1);
            Show(pgHome);
            var found = Game.Find();
            if (found != null)
                (gameDir, gameHow) = found.Value;
            string installed = Game.Installed();
            if (!dirPicked)
                opts.Dir = installed ?? Game.DefaultDir(gameDir);
            // an earlier install's choices (an update keeps the ones of the folder it updates)
            string earlier = updateMode ? opts.Dir : dirPicked ? null : installed;
            if (earlier != null)
                try
                {
                    foreach (var l in File.ReadLines(Path.Combine(earlier, Installer.Manifest)))
                    {
                        if (l == "desktop=0") opts.Desktop = false;
                        if (l == "links=0") opts.Links = false;
                    }
                }
                catch { }
            chkDesktop.IsChecked = opts.Desktop;
            chkLinks.IsChecked = opts.Links;
            RefreshHome();
            FetchRelease();
        }

        void FetchRelease()
        {
            relName.Text = "CHECKING THE LATEST RELEASE";
            relInfo.Text = "Asking GitHub for the newest Oz Arena...";
            btnInstall.IsEnabled = false;
            bool offline = offlineOnly;
            Task.Run(() => Release.Fetch(offline)).ContinueWith(t =>
            {
                if (t.IsFaulted)
                {
                    release = null;
                    var web = t.Exception.GetBaseException() as System.Net.WebException;
                    var answer = web != null ? web.Response as System.Net.HttpWebResponse : null;
                    int status = answer != null ? (int)answer.StatusCode : 0;
                    // GitHub answers 403 or 429 once an address has asked too often in an hour: the
                    // connection is fine, and "check your connection" sent players looking at it
                    bool limited = status == 403 || status == 429;
                    relName.Text = offline ? "NOTHING BESIDE SETUP" : limited ? "GITHUB SAYS WAIT" : "CAN'T REACH GITHUB";
                    relInfo.Text = limited
                        ? "GitHub is limiting requests from this address for now. Try again later."
                        : (web != null
                        ? "Setup downloads Oz Arena from GitHub, and couldn't get there. Check your connection."
                        : t.Exception.GetBaseException().Message);
                    installLabel.Text = "TRY AGAIN";
                    btnInstall.IsEnabled = true;
                    return;
                }
                release = t.Result;
                RefreshHome();
                if (autoStart && updateMode)
                    UpdateWhenClosed();
                else if (autoStart)
                    Install_Click(null, null);
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }

        void RefreshHome()
        {
            var green = (Brush)FindResource("Green");
            var amber = (Brush)FindResource("Amber");
            if (gameDir != null)
            {
                gameStatus.Text = gameHow == "Steam" ? "Found in your Steam library" : gameHow == "GOG" ? "Found in your GOG games"
                    : gameHow == "picked" ? "Quake III Arena, where you showed Setup" : "Found in " + gameHow;
                gamePath.Text = gameDir;
                gameDot.Fill = gameDotGlow.Fill = green;
            }
            else
            {
                gameStatus.Text = "Quake III Arena not found";
                gamePath.Text = "Not in your Steam libraries, GOG games or the usual folders.";
                gameDot.Fill = gameDotGlow.Fill = amber;
            }

            dirText.Text = opts.Dir;
            bool sees = Game.InstallSeesGame(opts.Dir);
            dirNote.Text = sees
                ? "A folder of its own inside your game's folder: Oz Arena reads the game from there, and its files never mix with the game's."
                : gameDir != null && gameHow == "Steam"
                    ? "Oz Arena finds your Steam copy from anywhere, so this folder works too."
                    : "Before you play, put your copy's baseq3 folder (pak0.pk3 to pak8.pk3) next to OzArena.exe, or install inside your Quake III Arena folder.";
            dirNote.Foreground = (Brush)FindResource(sees || (gameDir != null && gameHow == "Steam") ? "Muted" : "Amber");

            string have = Installer.InstalledVersion(opts.Dir);
            bool installedHere = File.Exists(Path.Combine(opts.Dir, "OzArena.exe"));
            // no PLAY while an update lies unpacked beside the game: its files may be of two versions
            btnPlayHome.Visibility = installedHere && !Installer.Unfinished(opts.Dir) ? Visibility.Visible : Visibility.Collapsed;
            if (release == null)
                return;
            relName.Text = "OZ ARENA " + release.Version.ToUpperInvariant();
            long dl = Installer.DownloadSize(release, opts.Dir), need = Installer.SpaceNeeded(release, opts.Dir), free = Installer.FreeSpace(opts.Dir);
            string drive = Path.GetPathRoot(Path.GetFullPath(opts.Dir)).TrimEnd('\\');
            string what = release.Offline ? "Installs from the files beside Setup" : dl > 0 ? Size(dl) + " to download" : "Nothing to download";
            relInfo.Text = what + "  ·  " + Size(free) + " free on " + drive + (free < need ? "  ·  needs " + Size(need) : "");
            relInfo.Foreground = (Brush)FindResource(free < need ? "RedHot" : "Muted");
            if (installedHere && Release.Same(have, release.Version))
                installLabel.Text = "REPAIR";
            else if (installedHere)
                installLabel.Text = "UPDATE";
            else
                installLabel.Text = "INSTALL";
            enterHint.Text = installLabel.Text;
            btnInstall.IsEnabled = free >= need;
        }

        static string Size(long b)
        {
            if (b >= 1L << 30) return (b / (double)(1L << 30)).ToString(b >= 10L << 30 ? "0" : "0.0") + " GB";
            if (b >= 1L << 20) return (b / (double)(1L << 20)).ToString("0") + " MB";
            return Math.Max(1, b / 1024) + " KB";
        }

        void Locate_Click(object sender, RoutedEventArgs e)
        {
            string picked = FolderPicker.Pick(new WindowInteropHelper(this).Handle, "Show Setup your Quake III Arena folder (the one with baseq3)", gameDir);
            if (picked == null)
                return;
            string game = Game.Recognize(picked);
            if (game == null)
            {
                Message(After.Home, After.Close, "NOT THAT ONE", "NO GAME THERE", "RedHot",
                    "That folder has no baseq3\\pak0.pk3. Pick the Quake III Arena folder itself: the one holding baseq3.",
                    picked, "BACK", "CLOSE");
                return;
            }
            gameDir = game;
            gameHow = "picked";
            if (!dirPicked)
                opts.Dir = Game.DefaultDir(gameDir);
            RefreshHome();
        }

        void Change_Click(object sender, RoutedEventArgs e)
        {
            string start = Directory.Exists(opts.Dir) ? opts.Dir : Path.GetDirectoryName(opts.Dir);
            string picked = FolderPicker.Pick(new WindowInteropHelper(this).Handle, "Where should Oz Arena go?", start);
            if (picked == null)
                return;
            opts.Dir = Game.InstallDirFor(picked);
            dirPicked = true;
            RefreshHome();
        }

        void Install_Click(object sender, RoutedEventArgs e)
        {
            if (working)
                return;
            if (release == null)
            {
                FetchRelease();
                return;
            }
            opts.Desktop = chkDesktop.IsChecked == true;
            opts.Links = chkLinks.IsChecked == true;
            if (Installer.GameRunning(opts.Dir))
            {
                Message(After.Home, After.Close, "OZ ARENA IS RUNNING", "CLOSE THE GAME", "Amber",
                    "Oz Arena is open from this folder. Quit it, then come back and press " + installLabel.Text.ToLowerInvariant() + " again.",
                    opts.Dir, "BACK", "CLOSE");
                return;
            }
            if (!Installer.Writable(opts.Dir) && !Installer.IsAdmin)
            {
                var up = new[] { "/dir=" + opts.Dir, "/desktop=" + (opts.Desktop ? 1 : 0), "/links=" + (opts.Links ? 1 : 0), "/go" }.ToList();
                if (updateMode)
                    up.Add("/update");
                if (offlineOnly)
                    up.Add("/offline");
                Elevate(up.ToArray());
                return;
            }
            Work((updateMode ? "UPDATING TO " : "INSTALLING ") + release.Version.ToUpperInvariant(), ct => Installer.Install(release, opts, prog, ct), () =>
            {
                if (updateMode)
                {
                    Play_Click(null, null); // updated: back into the game
                    return;
                }
                InstallSteps(3);
                Message(After.Play, After.Close, "INSTALLED", "READY TO FIGHT", "Green",
                    "Oz Arena " + release.Version + " is in " + opts.Dir + ".",
                    "Your settings, career and replays live in %APPDATA%\\Oz Arena. Start Menu has Oz Arena" + (opts.Desktop ? ", and so does your desktop." : "."),
                    "PLAY", "CLOSE");
            });
            InstallSteps(2);
        }

        /// <summary>The update: once the game that started Setup has closed (it quits as Setup starts), or
        /// straight back into the game when the folder already has this release.</summary>
        void UpdateWhenClosed()
        {
            if (Release.Same(Installer.InstalledVersion(opts.Dir), release.Version) && File.Exists(Path.Combine(opts.Dir, "OzArena.exe")) && !Installer.Unfinished(opts.Dir))
            {
                Play_Click(null, null);
                return;
            }
            relInfo.Text = "Waiting for Oz Arena to close...";
            btnInstall.IsEnabled = false;
            Task.Run(() =>
            {
                for (int i = 0; i < 150 && Installer.GameRunning(opts.Dir); i++)
                    Thread.Sleep(200);
            }).ContinueWith(_ =>
            {
                RefreshHome();
                Install_Click(null, null); // still running after 30 s: Install says so, and the player closes it
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }

        /// <summary>Setup again with administrator rights, for a folder that needs them. That one does the files
        /// only (/filesonly) and shows the window; this one hides, and does what is the player's own as the
        /// files are done: the shortcuts, the ozarena:// links and the Apps &amp; features entry. Windows may
        /// have asked for another administrator's name and password, and then the elevated Setup's desktop,
        /// Start Menu and registry are that administrator's, not the player's.</summary>
        void Elevate(params string[] args)
        {
            try
            {
                string exe = Process.GetCurrentProcess().MainModule.FileName;
                var all = (mode == Mode.Uninstall ? new[] { "/uninstall" } : new string[0]).Concat(args).Concat(new[] { "/filesonly", "/elevated" });
                var child = Process.Start(new ProcessStartInfo(exe, string.Join(" ", all.Select(a => "\"" + a + "\""))) { Verb = "runas", UseShellExecute = true });
                if (child == null)
                {
                    Application.Current.Shutdown();
                    return;
                }
                Hide();
                bool removing = mode == Mode.Uninstall;
                string dir = removing ? uninstallDir : opts.Dir;
                long bytes = release != null ? release.Size : 0;
                DateTime started = DateTime.UtcNow.AddSeconds(-2);
                Task.Run(() =>
                {
                    string record = Path.Combine(dir, Installer.Manifest), game = Path.Combine(dir, "OzArena.exe");
                    DateTime seen = DateTime.MinValue;
                    for (bool gone = false; !gone;)
                    {
                        try { gone = child.WaitForExit(300); }
                        catch { gone = true; }
                        try
                        {
                            if (removing)
                            {
                                if (gone && !File.Exists(game))
                                    Uninstall.Forget();
                            }
                            else if (File.Exists(record) && File.Exists(game))
                            {
                                // the record is written as the files are done, before the window says so
                                DateTime written = File.GetLastWriteTimeUtc(record);
                                if (written > started && written != seen)
                                {
                                    Installer.PerUser(dir, bytes);
                                    seen = written;
                                }
                            }
                        }
                        catch { }
                    }
                }).ContinueWith(_ => Application.Current.Shutdown(), TaskScheduler.FromCurrentSynchronizationContext());
            }
            catch (Win32Exception)
            {
                Message(After.Home, After.Close, "ADMINISTRATOR RIGHTS", "WINDOWS SAID NO", "Amber",
                    "Writing to " + (mode == Mode.Uninstall ? uninstallDir : opts.Dir) + " needs administrator rights. Allow it when Windows asks, or pick another folder.",
                    "", mode == Mode.Uninstall ? "TRY AGAIN" : "BACK", "CLOSE");
                if (mode == Mode.Uninstall)
                    mainAction = After.Remove;
            }
        }

        // ---------------------------------------------------------------- the work page

        void Work(string caption, Action<CancellationToken> job, Action done)
        {
            working = true;
            prog = new Progress();
            cts = new CancellationTokenSource();
            workCaption.Text = caption;
            pctText.Text = "0";
            stepText.Text = "Getting ready";
            bytesText.Text = speedText.Text = " ";
            fill.Width = 0;
            btnCancel.IsEnabled = true;
            btnCancel.Visibility = Visibility.Visible;
            workNote.Visibility = mode == Mode.Install && !(release?.Offline ?? true) ? Visibility.Visible : Visibility.Collapsed;
            enterHint.Text = "—";
            lastBytes = 0;
            lastTick = DateTime.UtcNow;
            speed = 0;
            Show(pgWork);
            ticker.Start();
            var token = cts.Token;
            Task.Run(() => job(token)).ContinueWith(t =>
            {
                ticker.Stop();
                Tick();
                working = false;
                if (closeWhenStopped)
                {
                    Close();
                    return;
                }
                if (t.IsCanceled || t.Exception?.GetBaseException() is OperationCanceledException)
                {
                    if (mode == Mode.Install)
                    {
                        InstallSteps(1);
                        Show(pgHome);
                        RefreshHome();
                    }
                    else
                        Close();
                    return;
                }
                if (t.IsFaulted)
                {
                    var ex = t.Exception.GetBaseException();
                    string why = ex is UnauthorizedAccessException ? "Windows wouldn't let Setup write a file. Close Oz Arena if it's open, and try again."
                        : ex is IOException && ex.Message.Contains("space") ? "The drive ran out of space."
                        : ex.Message;
                    Message(mode == Mode.Install ? After.Retry : After.Remove, After.Close, "SETUP STOPPED", "SOMETHING WENT WRONG", "RedHot",
                        why, mode == Mode.Install ? "What Setup already downloaded is kept: trying again carries on from there." : "", "TRY AGAIN", "CLOSE");
                    return;
                }
                done();
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }

        void Tick()
        {
            if (prog == null)
                return;
            long done = Interlocked.Read(ref prog.Done), total = Math.Max(1, Interlocked.Read(ref prog.Total));
            double f = Math.Min(1, done / (double)total);
            pctText.Text = ((int)Math.Floor(f * 100)).ToString();
            fill.Width = Math.Round(track.ActualWidth * f);
            stepText.Text = prog.Step;
            if (mode == Mode.Uninstall)
                return;
            long dl = Interlocked.Read(ref prog.Downloaded), want = prog.ToDownload;
            long fresh = dl - Interlocked.Read(ref prog.Resumed); // this run's bytes: a resumed file's head isn't speed
            var now = DateTime.UtcNow;
            double dt = (now - lastTick).TotalSeconds;
            if (dt > 0.25)
            {
                double inst = (fresh - lastBytes) / dt;
                speed = speed <= 0 ? inst : speed * 0.8 + inst * 0.2;
                lastBytes = fresh;
                lastTick = now;
            }
            if (want > 0)
                bytesText.Text = Size(dl) + " of " + Size(want);
            if (prog.Downloading && speed > 1024)
            {
                double left = (want - dl) / speed;
                speedText.Text = Size((long)speed) + "/s  ·  " + (left < 60 ? "under a minute left" : left < 3600 ? Math.Ceiling(left / 60) + " min left" : (left / 3600).ToString("0.0") + " h left");
            }
            else
                speedText.Text = prog.Downloading ? "Connecting..." : " ";
        }

        void Cancel_Click(object sender, RoutedEventArgs e)
        {
            btnCancel.IsEnabled = false;
            stepText.Text = "Stopping...";
            cts?.Cancel();
        }

        void Play_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Shell.Launch(opts.Dir);
                Close();
            }
            catch (Exception ex)
            {
                Message(After.Home, After.Close, "COULDN'T START", "THE GAME DIDN'T OPEN", "RedHot", ex.Message, opts.Dir, "BACK", "CLOSE");
            }
        }

        // ---------------------------------------------------------------- messages

        void Message(After main, After other, string caption, string title, string captionBrush, string body, string detail, string mainLabel, string otherLabel)
        {
            mainAction = main;
            otherAction = other;
            msgCaption.Text = caption;
            msgCaption.Foreground = (Brush)FindResource(captionBrush);
            msgTitle.Text = title;
            msgBody.Text = body;
            msgDetail.Text = detail;
            msgDetail.Visibility = string.IsNullOrEmpty(detail) ? Visibility.Collapsed : Visibility.Visible;
            msgMainLabel.Text = mainLabel;
            msgOtherLabel.Text = otherLabel;
            enterHint.Text = mainLabel;
            Show(pgMessage);
        }

        void MsgMain_Click(object sender, RoutedEventArgs e) => Do(mainAction);
        void MsgOther_Click(object sender, RoutedEventArgs e) => Do(otherAction);

        void Do(After a)
        {
            switch (a)
            {
                case After.Play: Play_Click(null, null); break;
                case After.Close: Close(); break;
                case After.Retry: Install_Click(null, null); break;
                case After.Remove: Remove_Click(); break;
                case After.Home:
                    if (mode == Mode.Install)
                    {
                        InstallSteps(1);
                        Show(pgHome);
                        RefreshHome();
                    }
                    else
                        StartUninstallMode();
                    break;
            }
        }

        // ---------------------------------------------------------------- uninstall

        void StartUninstallMode()
        {
            var args = Environment.GetCommandLineArgs().Skip(1).ToList();
            int i = args.IndexOf("/uninstall");
            uninstallDir = opts.Dir ?? (i >= 0 && i + 1 < args.Count && !args[i + 1].StartsWith("/") ? args[i + 1]
                : Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName));
            uninstallDir = Path.GetFullPath(uninstallDir);
            opts.Dir = uninstallDir;
            footerText.Text = "Your Quake III Arena, your settings, your career and your maps stay where they are.";
            UninstallSteps(1);
            Message(After.Remove, After.Close, "UNINSTALL", "REMOVE OZ ARENA", "Muted",
                "Takes Oz Arena out of " + uninstallDir + ", with its shortcuts and links.",
                "Your Quake III Arena isn't touched. Your settings, career and replays (%APPDATA%\\Oz Arena) and any maps in the Maps folder stay.",
                "REMOVE", "KEEP IT");
            if (autoStart)
                Remove_Click();
        }

        void Remove_Click()
        {
            if (Installer.GameRunning(uninstallDir))
            {
                Message(After.Remove, After.Close, "OZ ARENA IS RUNNING", "CLOSE THE GAME", "Amber",
                    "Oz Arena is open from this folder. Quit it, then press remove again.", uninstallDir, "REMOVE", "CLOSE");
                return;
            }
            if (!Installer.Writable(uninstallDir) && !Installer.IsAdmin)
            {
                Elevate("/dir=" + uninstallDir, "/go");
                return;
            }
            UninstallSteps(2);
            Work("REMOVING", ct => Uninstall.Remove(uninstallDir, prog, ct, opts.FilesOnly), () =>
            {
                UninstallSteps(3);
                btnMsgMain.Visibility = Visibility.Collapsed;
                Message(After.Close, After.Close, "UNINSTALLED", "SEE YOU SOON", "Green",
                    "Oz Arena is gone from " + uninstallDir + ".",
                    "Your settings and career are still in %APPDATA%\\Oz Arena if you come back.", "", "CLOSE");
                enterHint.Text = "CLOSE";
            });
            btnCancel.Visibility = Visibility.Collapsed;
        }

        // ---------------------------------------------------------------- window

        void OnKey(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                e.Handled = true;
                if (working)
                    Cancel_Click(null, null);
                else
                    Close();
            }
            else if (e.Key == Key.Enter && !working)
            {
                e.Handled = true;
                if (pgHome.Visibility == Visibility.Visible && btnInstall.IsEnabled)
                    Install_Click(null, null);
                else if (pgMessage.Visibility == Visibility.Visible && btnMsgMain.Visibility == Visibility.Visible)
                    Do(mainAction);
            }
        }

        void OnClosing(object sender, CancelEventArgs e)
        {
            // stop the work first: a download stopped mid-file resumes next time
            if (working)
            {
                e.Cancel = true;
                closeWhenStopped = true;
                Cancel_Click(null, null);
            }
        }

        void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
        void Close_Click(object sender, RoutedEventArgs e) => Close();
    }
}
