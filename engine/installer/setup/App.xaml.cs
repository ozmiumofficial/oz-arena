using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;

namespace OzSetup
{
    public partial class App : Application
    {
        /// <summary>Set when this run is the uninstaller's copy in %TEMP%: it deletes itself as it exits.</summary>
        string tempSelf;

        protected override void OnStartup(StartupEventArgs e)
        {
            DispatcherUnhandledException += (s, x) =>
            {
                MessageBox.Show(x.Exception.GetBaseException().Message, "Oz Arena Setup", MessageBoxButton.OK, MessageBoxImage.Error);
                x.Handled = true;
                Shutdown(1);
            };

            var args = e.Args.ToList();
            string self = Process.GetCurrentProcess().MainModule.FileName;
            if (args.Contains("/uninstall") && !args.Contains("/fromtemp"))
            {
                // the uninstaller can't delete itself while it runs: a copy in %TEMP% does the work
                string dir = args.FirstOrDefault(a => a.StartsWith("/dir="))?.Substring(5) ?? Path.GetDirectoryName(self);
                string temp = Path.Combine(Path.GetTempPath(), "OzArenaUninstall-" + Guid.NewGuid().ToString("N").Substring(0, 8) + ".exe");
                try
                {
                    File.Copy(self, temp);
                    var rest = args.Where(a => !a.StartsWith("/dir=")).Concat(new[] { "/dir=" + dir, "/fromtemp" });
                    Process.Start(new ProcessStartInfo(temp, string.Join(" ", rest.Select(a => "\"" + a + "\""))) { UseShellExecute = false });
                    Shutdown(0);
                    return;
                }
                catch { } // can't copy: uninstall from here and leave the uninstaller behind
            }
            if (args.Contains("/fromtemp"))
                tempSelf = self;
            base.OnStartup(e);
            new MainWindow().Show();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            if (tempSelf != null)
                try
                {
                    Process.Start(new ProcessStartInfo("cmd.exe", "/c ping 127.0.0.1 -n 3 > nul & del /f /q \"" + tempSelf + "\"")
                    { CreateNoWindow = true, UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden });
                }
                catch { }
            base.OnExit(e);
        }
    }
}
