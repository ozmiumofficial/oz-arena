using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Principal;
using System.Text.RegularExpressions;
using System.Threading;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using Microsoft.Win32;

namespace OzSetup
{
    /// <summary>The player's Quake III Arena, found as the engine finds it (win_shared.c Sys_SteamPath, Sys_GogGame;
    /// files.c's folder above OzArena.exe), and the install Setup put there before.</summary>
    static class Game
    {
        public const string Name = "Quake III Arena";
        public const string AppId = "OzArena";
        public const string Folder = "Oz Arena";
        const string SteamFolder = "Quake 3 Arena";

        public static bool HasBaseq3(string dir) => !string.IsNullOrEmpty(dir) && File.Exists(Path.Combine(dir, "baseq3", "pak0.pk3"));

        static string Tidy(string s) => string.IsNullOrWhiteSpace(s) ? null : s.Trim().Replace('/', '\\').TrimEnd('\\');

        static string Reg(RegistryHive hive, RegistryView view, string key, string value)
        {
            try
            {
                using (var b = RegistryKey.OpenBaseKey(hive, view))
                using (var k = b.OpenSubKey(key))
                    return Tidy(k?.GetValue(value) as string);
            }
            catch { return null; }
        }

        /// <summary>The game folder and where it was found ("Steam", "GOG", ...), or null.</summary>
        public static (string dir, string how)? Find()
        {
            string s = Reg(RegistryHive.LocalMachine, RegistryView.Registry32, @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 2200", "InstallLocation")
                    ?? Reg(RegistryHive.LocalMachine, RegistryView.Registry64, @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 2200", "InstallLocation");
            if (HasBaseq3(s))
                return (s, "Steam");

            string steam = Reg(RegistryHive.CurrentUser, RegistryView.Default, @"Software\Valve\Steam", "SteamPath")
                        ?? Reg(RegistryHive.LocalMachine, RegistryView.Registry32, @"SOFTWARE\Valve\Steam", "InstallPath");
            if (steam != null)
                foreach (var lib in SteamLibraries(steam))
                {
                    string d = Path.Combine(lib, "steamapps", "common", SteamFolder);
                    if (HasBaseq3(d))
                        return (d, "Steam");
                }

            s = Reg(RegistryHive.LocalMachine, RegistryView.Registry32, @"SOFTWARE\GOG.com\Games\1441704920", "path");
            if (HasBaseq3(s))
                return (s, "GOG");

            string pf86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            string pf = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            var plain = new List<string>
            {
                Path.Combine(pf86, Name), Path.Combine(pf, Name),
                Path.Combine(pf86, "GOG Galaxy", "Games", Name), Path.Combine(pf86, "GOG.com", Name),
            };
            foreach (var drive in DriveInfo.GetDrives().Where(d => d.DriveType == DriveType.Fixed && d.IsReady))
            {
                plain.Add(Path.Combine(drive.Name, Name));
                plain.Add(Path.Combine(drive.Name, "Games", Name));
                plain.Add(Path.Combine(drive.Name, "GOG Games", Name));
                plain.Add(Path.Combine(drive.Name, "SteamLibrary", "steamapps", "common", SteamFolder));
            }
            foreach (var d in plain)
                if (HasBaseq3(d))
                    return (d, "your games");
            return null;
        }

        /// <summary>Steam's own folder, then every library in steamapps\libraryfolders.vdf ("path" lines).</summary>
        static IEnumerable<string> SteamLibraries(string steam)
        {
            yield return steam;
            string vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
            string[] lines;
            try { lines = File.ReadAllLines(vdf); } catch { yield break; }
            foreach (var line in lines)
            {
                var m = Regex.Match(line, "^\\s*\"path\"\\s+\"(.*)\"\\s*$");
                if (m.Success)
                    yield return Tidy(m.Groups[1].Value.Replace("\\\\", "\\"));
            }
        }

        /// <summary>A folder the player picked: the game folder itself, its baseq3, or null.</summary>
        public static string Recognize(string picked)
        {
            if (HasBaseq3(picked))
                return Tidy(picked);
            if (string.Equals(Path.GetFileName(Tidy(picked)), "baseq3", StringComparison.OrdinalIgnoreCase) && HasBaseq3(Path.GetDirectoryName(Tidy(picked))))
                return Path.GetDirectoryName(Tidy(picked));
            return null;
        }

        /// <summary>Where Oz Arena is installed now, by this Setup's record or the Inno Setup installer's.</summary>
        public static string Installed()
        {
            foreach (var (hive, key) in new[] {
                (RegistryHive.CurrentUser, Uninstall.Key),
                (RegistryHive.LocalMachine, Uninstall.Key),
                (RegistryHive.CurrentUser, Uninstall.Key + "_is1"),
                (RegistryHive.LocalMachine, Uninstall.Key + "_is1") })
            {
                string d = Reg(hive, RegistryView.Registry64, key, "InstallLocation");
                if (d != null && File.Exists(Path.Combine(d, "OzArena.exe")))
                    return d;
            }
            return null;
        }

        /// <summary>The Quake III Arena folder an install reads baseq3 from: beside OzArena.exe or the folder above.</summary>
        public static bool InstallSeesGame(string dir) => HasBaseq3(dir) || HasBaseq3(Path.GetDirectoryName(Tidy(dir) ?? ""));

        public static string DefaultDir(string gameDir) => gameDir != null
            ? Path.Combine(gameDir, Folder)
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Folder);

        /// <summary>A folder the player chose to install in: a fresh "Oz Arena" inside it unless it's one already or empty.</summary>
        public static string InstallDirFor(string picked)
        {
            picked = Tidy(picked);
            string name = Path.GetFileName(picked) ?? "";
            if (name.Equals(Folder, StringComparison.OrdinalIgnoreCase) || name.Equals("OzArena", StringComparison.OrdinalIgnoreCase))
                return picked;
            if (File.Exists(Path.Combine(picked, "OzArena.exe")))
                return picked;
            bool empty = Directory.Exists(picked) && !Directory.EnumerateFileSystemEntries(picked).Any();
            return empty ? picked : Path.Combine(picked, Folder);
        }
    }

    /// <summary>One file of a release: the game zip or a pak that goes in oz\.</summary>
    class Asset
    {
        public string Name, Url, Local;
        public long Size;
        public string Sha256;  // GitHub's digest of the file (lowercase hex), null when it has none
        public bool IsZip => Name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>The release Setup installs: GitHub's latest, or the same files beside Setup (an offline install).</summary>
    class Release
    {
        public const string Repo = "ozmiumofficial/oz-arena";
        public string Version;
        public List<Asset> Assets = new List<Asset>();
        public bool Offline;
        public Asset Zip => Assets.FirstOrDefault(a => a.IsZip);
        public long Size => Assets.Sum(a => a.Size);

        static readonly Regex ZipName = new Regex(@"^OzArena-(.+)-windows\.zip$", RegexOptions.IgnoreCase);

        public static Release Fetch()
        {
            var offline = Beside();
            if (offline != null)
                return offline;
            var req = Http.Request("https://api.github.com/repos/" + Repo + "/releases/latest");
            req.Accept = "application/vnd.github+json";
            GitHubRelease doc;
            using (var resp = req.GetResponse())
            using (var s = resp.GetResponseStream())
                doc = (GitHubRelease)new DataContractJsonSerializer(typeof(GitHubRelease)).ReadObject(s);
            var rel = new Release { Version = doc.tag_name };
            foreach (var a in doc.assets ?? new GitHubAsset[0])
            {
                if (!ZipName.IsMatch(a.name) && !a.name.EndsWith(".pk3", StringComparison.OrdinalIgnoreCase))
                    continue;
                rel.Assets.Add(new Asset { Name = a.name, Url = a.browser_download_url, Size = a.size,
                    Sha256 = a.digest != null && a.digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) ? a.digest.Substring(7).ToLowerInvariant() : null });
            }
            if (rel.Zip == null)
                throw new Exception("The latest release on GitHub has no game zip yet. Try again in a little while.");
            return rel;
        }

        /// <summary>The release's files in Setup's own folder: the zip and the paks next to it.</summary>
        static Release Beside()
        {
            string here = Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName);
            var zip = Directory.GetFiles(here, "OzArena-*-windows.zip").OrderByDescending(f => f).FirstOrDefault();
            if (zip == null)
                return null;
            var rel = new Release { Offline = true, Version = ZipName.Match(Path.GetFileName(zip)).Groups[1].Value };
            rel.Assets.Add(new Asset { Name = Path.GetFileName(zip), Local = zip, Size = new FileInfo(zip).Length });
            foreach (var pk3 in Directory.GetFiles(here, "pak_oz_*.pk3"))
                rel.Assets.Add(new Asset { Name = Path.GetFileName(pk3), Local = pk3, Size = new FileInfo(pk3).Length });
            return rel;
        }
    }

#pragma warning disable CS0649 // filled by DataContractJsonSerializer
    /// <summary>The parts of GitHub's release JSON Setup reads.</summary>
    [DataContract]
    class GitHubRelease
    {
        [DataMember] public string tag_name;
        [DataMember] public GitHubAsset[] assets;
    }

    [DataContract]
    class GitHubAsset
    {
        [DataMember] public string name;
        [DataMember] public string browser_download_url;
        [DataMember] public long size;
        [DataMember] public string digest;  // "sha256:<hex>"
    }
#pragma warning restore CS0649

    static class Http
    {
        static Http()
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            ServicePointManager.DefaultConnectionLimit = 8;
        }

        public static HttpWebRequest Request(string url)
        {
            var req = (HttpWebRequest)WebRequest.Create(url);
            req.UserAgent = "OzArenaSetup/1.0 (+https://github.com/" + Release.Repo + ")";
            req.AllowAutoRedirect = true;
            req.Timeout = 30000;
            req.ReadWriteTimeout = 60000;
            return req;
        }
    }

    /// <summary>What the window shows while Setup works. The worker writes, the window's timer reads.</summary>
    class Progress
    {
        public volatile string Step = "";
        public long Done, Total;           // weighted bytes: downloads count whole, unpacking a quarter
        public long Downloaded, ToDownload; // real bytes, for the "of" line
        public long Resumed;                // of Downloaded, what an earlier run had: not this run's speed
        public volatile bool Downloading;
    }

    class Options
    {
        public string Dir;
        public bool Desktop = true, Links = true;
    }

    static class Installer
    {
        public const string Manifest = "oz-setup.txt";
        public const string Uninstaller = "Uninstall Oz Arena.exe";
        const string Staging = ".oz-setup";
        const double UnpackWeight = 0.25;

        public static bool IsAdmin => new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

        /// <summary>Can Setup write there? Tried in the nearest folder that exists, so nothing is left behind.</summary>
        public static bool Writable(string dir)
        {
            try
            {
                dir = Path.GetFullPath(dir);
                while (!Directory.Exists(dir))
                {
                    string up = Path.GetDirectoryName(dir);
                    if (up == null || up == dir)
                        return false;
                    dir = up;
                }
                string f = Path.Combine(dir, "~oz_setup_" + Guid.NewGuid().ToString("N") + ".tmp");
                File.WriteAllText(f, "x");
                File.Delete(f);
                return true;
            }
            catch { return false; }
        }

        /// <summary>An Oz Arena from this folder still running: Setup asks the player to close it, never closes it itself.</summary>
        public static bool GameRunning(string dir)
        {
            foreach (var name in new[] { "OzArena", "OzArena.ded" })
                foreach (var p in Process.GetProcessesByName(name))
                {
                    try
                    {
                        if (string.Equals(Path.GetDirectoryName(p.MainModule.FileName), Path.GetFullPath(dir).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase))
                            return true;
                    }
                    catch { return true; } // an elevated game we can't look into: be safe
                    finally { p.Dispose(); }
                }
            return false;
        }

        public static string InstalledVersion(string dir)
        {
            try { return File.ReadLines(Path.Combine(dir, Manifest)).FirstOrDefault(l => l.StartsWith("version="))?.Substring(8); }
            catch { return null; }
        }

        /// <summary>The paks already in place as the release has them (its size, and its checksum when the release
        /// gives one, or the offline file's): Setup doesn't download those again. A pak of the same size but other
        /// contents (a texture pack remade, v0.7.7) is fetched anew.</summary>
        public static bool Have(string dir, Asset a)
        {
            string path = Path.Combine(dir, "oz", a.Name);
            if (a.IsZip || !SameSize(path, a.Size))
                return false;
            if (a.Sha256 == null && a.Local != null)
                a.Sha256 = Sha256Of(a.Local);
            return a.Sha256 == null || Sha256Of(path) == a.Sha256;
        }

        // a file's SHA-256 (lowercase hex), kept per file, size and time: a pak is read once a run
        static readonly Dictionary<string, string> hashes = new Dictionary<string, string>();

        static string Sha256Of(string path)
        {
            var fi = new FileInfo(path);
            string key = fi.FullName + "|" + fi.Length + "|" + fi.LastWriteTimeUtc.Ticks;
            lock (hashes)
            {
                if (hashes.TryGetValue(key, out var have))
                    return have;
            }
            string hex;
            using (var sha = System.Security.Cryptography.SHA256.Create())
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
                hex = BitConverter.ToString(sha.ComputeHash(f)).Replace("-", "").ToLowerInvariant();
            lock (hashes)
                hashes[key] = hex;
            return hex;
        }

        static bool SameSize(string path, long size)
        {
            var fi = new FileInfo(path);
            return fi.Exists && fi.Length == size;
        }

        public static long DownloadSize(Release rel, string dir) => rel.Offline ? 0 : rel.Assets.Where(a => !Have(dir, a)).Sum(a => a.Size);

        /// <summary>Free space the install needs: the downloads, plus the zip unpacked beside its download.</summary>
        public static long SpaceNeeded(Release rel, string dir) => rel.Assets.Where(a => !Have(dir, a)).Sum(a => a.Size) + (rel.Offline ? 0 : rel.Zip.Size);

        public static long FreeSpace(string dir)
        {
            try { return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(dir))).AvailableFreeSpace; }
            catch { return long.MaxValue; }
        }

        public static void Install(Release rel, Options o, Progress p, CancellationToken ct)
        {
            string dir = Path.GetFullPath(o.Dir);
            string oz = Path.Combine(dir, "oz"), stage = Path.Combine(dir, Staging);
            Directory.CreateDirectory(oz);
            var st = Directory.CreateDirectory(stage);
            st.Attributes |= FileAttributes.Hidden;

            var paks = rel.Assets.Where(a => !a.IsZip).ToList();
            var fetch = paks.Where(a => !Have(dir, a)).ToList();
            if (!rel.Offline)
                fetch.Add(rel.Zip);
            long zipBytes = (long)(rel.Zip.Size * 1.02);
            p.ToDownload = rel.Offline ? 0 : fetch.Sum(a => a.Size);
            p.Total = (rel.Offline ? fetch.Sum(a => a.Size) : p.ToDownload) + (long)(zipBytes * UnpackWeight);

            // 1. the paks and the zip, into the staging folder (resumed if a run was stopped)
            foreach (var a in fetch)
            {
                ct.ThrowIfCancellationRequested();
                p.Step = Describe(a, rel.Offline);
                string part = Path.Combine(stage, a.Name + ".part"), whole = Path.Combine(stage, a.Name);
                if (a.Local != null)
                {
                    if (a.IsZip)
                        continue; // unpacked straight from where it sits
                    Copy(a.Local, part, p, ct);
                }
                else if (!SameSize(whole, a.Size))
                {
                    p.Downloading = true;
                    Download(a, part, p, ct);
                    p.Downloading = false;
                }
                else
                {
                    Interlocked.Add(ref p.Done, a.Size);
                    Interlocked.Add(ref p.Downloaded, a.Size);
                    Interlocked.Add(ref p.Resumed, a.Size);
                    continue;
                }
                if (File.Exists(whole))
                    File.Delete(whole);
                File.Move(part, whole);
            }
            p.Downloading = false;

            // 2. the game: the zip's OzArena\ folder unpacked into the install
            ct.ThrowIfCancellationRequested();
            p.Step = "Unpacking the game";
            var files = new List<string>();
            string zipPath = rel.Zip.Local ?? Path.Combine(stage, rel.Zip.Name);
            using (var zip = ZipFile.OpenRead(zipPath))
            {
                long total = zip.Entries.Sum(e => e.Length), weighted = (long)(zipBytes * UnpackWeight);
                double scale = total > 0 ? (double)weighted / total : 0;
                string top = zip.Entries.Select(e => e.FullName.Split('/')[0]).Distinct().Count() == 1 ? zip.Entries[0].FullName.Split('/')[0] + "/" : "";
                foreach (var e in zip.Entries)
                {
                    ct.ThrowIfCancellationRequested();
                    if (e.FullName.EndsWith("/"))
                        continue;
                    string rel1 = e.FullName.Substring(top.Length).Replace('/', '\\');
                    string dest = Path.GetFullPath(Path.Combine(dir, rel1));
                    if (!dest.StartsWith(dir + "\\", StringComparison.OrdinalIgnoreCase))
                        continue; // never outside the install
                    if (IsIdPak(rel1))
                        continue; // never the game's own files
                    Directory.CreateDirectory(Path.GetDirectoryName(dest));
                    using (var src = e.Open())
                    using (var dst = new FileStream(dest, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
                        Pump(src, dst, n => Interlocked.Add(ref p.Done, (long)(n * scale)), ct);
                    File.SetLastWriteTime(dest, e.LastWriteTime.DateTime);
                    files.Add(rel1);
                }
            }

            // 3. the world paks into oz\, beside the zip's
            p.Step = "Placing the world textures";
            foreach (var a in paks)
            {
                string staged = Path.Combine(stage, a.Name), dest = Path.Combine(oz, a.Name);
                if (File.Exists(staged))
                {
                    if (File.Exists(dest))
                        File.Delete(dest);
                    File.Move(staged, dest);
                }
                files.Add("oz\\" + a.Name);
            }

            // 4. an older version's Oz files this one doesn't ship (never the player's own)
            var keep = new HashSet<string>(files, StringComparer.OrdinalIgnoreCase);
            foreach (var f in Directory.GetFiles(oz, "*.pk3"))
            {
                string n = Path.GetFileName(f).ToLowerInvariant();
                if ((n.StartsWith("pak_oz_") || n.StartsWith("zz-oz-")) && !keep.Contains("oz\\" + Path.GetFileName(f)))
                    TryDelete(f);
            }
            foreach (var f in Directory.GetFiles(dir, "OzArena_*_x86_64.dll"))
                if (!keep.Contains(Path.GetFileName(f)))
                    TryDelete(f);

            // 5. what makes it an installed game: the uninstaller, the record, shortcuts, links
            p.Step = "Adding shortcuts";
            Directory.CreateDirectory(Path.Combine(dir, "Maps"));
            string self = Process.GetCurrentProcess().MainModule.FileName;
            string unins = Path.Combine(dir, Installer.Uninstaller);
            if (!string.Equals(self, unins, StringComparison.OrdinalIgnoreCase))
                File.Copy(self, unins, true);
            File.WriteAllLines(Path.Combine(dir, Manifest),
                new[] { "version=" + rel.Version, "desktop=" + (o.Desktop ? 1 : 0), "links=" + (o.Links ? 1 : 0) }
                .Concat(files.Select(f => "file=" + f)));
            Shell.Shortcuts(dir, o.Desktop);
            Uninstall.Register(dir, rel.Version, o.Links, rel.Size);

            TryDeleteDir(stage);
            p.Step = "Done";
            Interlocked.Exchange(ref p.Done, p.Total);
        }

        static bool IsIdPak(string rel) => Regex.IsMatch(Path.GetFileName(rel), @"^pak[0-8]\.pk3$", RegexOptions.IgnoreCase)
            || rel.Split('\\').Any(s => s.Equals("baseq3", StringComparison.OrdinalIgnoreCase));

        static string Describe(Asset a, bool offline)
        {
            string verb = offline ? "Copying" : "Downloading";
            if (a.IsZip)
                return verb + " the game";
            var m = Regex.Match(a.Name, @"^pak_oz_world(\d*)\.pk3$", RegexOptions.IgnoreCase);
            if (m.Success)
                return verb + " the world textures" + (m.Groups[1].Value.Length > 0 ? " (part " + (int.Parse(m.Groups[1].Value) + 1) + ")" : " (part 1)");
            return verb + " " + a.Name;
        }

        /// <summary>One file over HTTPS into part, resumed from what part already holds; retried on a dropped connection.</summary>
        static void Download(Asset a, string part, Progress p, CancellationToken ct)
        {
            long have = File.Exists(part) ? new FileInfo(part).Length : 0;
            if (have > a.Size)
            {
                File.Delete(part);
                have = 0;
            }
            Interlocked.Add(ref p.Done, have);
            Interlocked.Add(ref p.Downloaded, have);
            Interlocked.Add(ref p.Resumed, have);
            long resumed = have;
            for (int attempt = 0; have < a.Size; attempt++)
            {
                ct.ThrowIfCancellationRequested();
                long before = have;
                var req = Http.Request(a.Url);
                if (have > 0)
                    req.AddRange(have);
                try
                {
                    using (ct.Register(req.Abort))
                    using (var resp = (HttpWebResponse)req.GetResponse())
                    {
                        if (have > 0 && resp.StatusCode != HttpStatusCode.PartialContent)
                        {
                            // the server sent it whole: start over
                            Interlocked.Add(ref p.Done, -have);
                            Interlocked.Add(ref p.Downloaded, -have);
                            Interlocked.Add(ref p.Resumed, -resumed);
                            have = resumed = 0;
                        }
                        using (var src = resp.GetResponseStream())
                        using (var dst = new FileStream(part, have > 0 ? FileMode.Append : FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
                            Pump(src, dst, n => { have += n; Interlocked.Add(ref p.Done, n); Interlocked.Add(ref p.Downloaded, n); }, ct);
                    }
                    if (have > before)
                        attempt = -1; // it moved: a later drop gets its retries again
                    else if (attempt >= 6)
                        throw new Exception("The download of " + a.Name + " stopped. Check your connection and try again: it carries on where it stopped.");
                }
                catch (Exception e) when (!ct.IsCancellationRequested && (e is WebException || e is IOException))
                {
                    if (have > before)
                    {
                        attempt = -1;
                        continue;
                    }
                    if (attempt >= 6)
                        throw new Exception("The download of " + a.Name + " keeps failing (" + e.Message + "). Check your connection and try again: it carries on where it stopped.", e);
                    Thread.Sleep(1500 * (attempt + 1));
                }
                catch (WebException) when (ct.IsCancellationRequested)
                {
                    throw new OperationCanceledException(ct);
                }
            }
            if (new FileInfo(part).Length != a.Size)
                throw new Exception(a.Name + " didn't download whole. Try again: it carries on where it stopped.");
        }

        static void Copy(string from, string to, Progress p, CancellationToken ct)
        {
            using (var src = new FileStream(from, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
            using (var dst = new FileStream(to, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
                Pump(src, dst, n => Interlocked.Add(ref p.Done, n), ct);
        }

        static void Pump(Stream src, Stream dst, Action<int> moved, CancellationToken ct)
        {
            var buf = new byte[1 << 20];
            int n;
            while ((n = src.Read(buf, 0, buf.Length)) > 0)
            {
                ct.ThrowIfCancellationRequested();
                dst.Write(buf, 0, n);
                moved(n);
            }
        }

        public static void TryDelete(string f)
        {
            try { File.Delete(f); } catch { }
        }

        public static void TryDeleteDir(string d, bool recursive = true)
        {
            try { if (Directory.Exists(d)) Directory.Delete(d, recursive); } catch { }
        }
    }

    /// <summary>Apps &amp; features' entry and the ozarena:// links, per user (HKCU) as the Inno installer's "lowest" mode.</summary>
    static class Uninstall
    {
        public const string Key = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\" + Game.AppId;
        const string Scheme = @"Software\Classes\ozarena";

        public static void Register(string dir, string version, bool links, long bytes)
        {
            string exe = Path.Combine(dir, "OzArena.exe");
            using (var k = Registry.CurrentUser.CreateSubKey(Key))
            {
                k.SetValue("DisplayName", "Oz Arena");
                k.SetValue("DisplayVersion", version.TrimStart('v'));
                k.SetValue("Publisher", "Ozmium");
                k.SetValue("DisplayIcon", exe + ",0");
                k.SetValue("InstallLocation", dir);
                k.SetValue("UninstallString", "\"" + Path.Combine(dir, Installer.Uninstaller) + "\" /uninstall");
                k.SetValue("URLInfoAbout", "https://github.com/" + Release.Repo);
                k.SetValue("HelpLink", "https://github.com/" + Release.Repo + "/issues");
                k.SetValue("EstimatedSize", (int)Math.Min(bytes / 1024, int.MaxValue), RegistryValueKind.DWord);
                k.SetValue("NoModify", 1, RegistryValueKind.DWord);
                k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }
            if (links)
            {
                using (var k = Registry.CurrentUser.CreateSubKey(Scheme))
                {
                    k.SetValue("", "URL:Oz Arena link");
                    k.SetValue("URL Protocol", "");
                }
                using (var k = Registry.CurrentUser.CreateSubKey(Scheme + @"\DefaultIcon"))
                    k.SetValue("", exe + ",0");
                using (var k = Registry.CurrentUser.CreateSubKey(Scheme + @"\shell\open\command"))
                    k.SetValue("", "\"" + exe + "\" +oz_link \"%1\"");
            }
            else
                Unregister(Scheme);
        }

        static void Unregister(string key)
        {
            try { Registry.CurrentUser.DeleteSubKeyTree(key, false); } catch { }
        }

        /// <summary>Takes away everything Setup put in: the manifest's files, shortcuts, the entry, the links.
        /// The player's own files (Maps, %APPDATA%\Oz Arena) stay.</summary>
        public static void Remove(string dir, Progress p, CancellationToken ct)
        {
            dir = Path.GetFullPath(dir);
            var files = new List<string>();
            try { files = File.ReadLines(Path.Combine(dir, Installer.Manifest)).Where(l => l.StartsWith("file=")).Select(l => l.Substring(5)).ToList(); }
            catch { }
            // without a manifest (an older install): the files any release ships
            if (files.Count == 0)
                files = new[] { "OzArena.exe", "OzArena.ded.exe", "OpenAL64.dll", "SDL2.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt", "README.txt" }
                    .Concat(Directory.GetFiles(dir, "OzArena_*_x86_64.dll").Select(Path.GetFileName)).ToList();
            p.Total = files.Count + 1;
            p.Step = "Removing Oz Arena";
            foreach (var f in files)
            {
                ct.ThrowIfCancellationRequested();
                string full = Path.GetFullPath(Path.Combine(dir, f));
                if (full.StartsWith(dir + "\\", StringComparison.OrdinalIgnoreCase))
                    Installer.TryDelete(full);
                Interlocked.Increment(ref p.Done);
            }
            // the zip's Oz paks and tools, even ones a newer zip added past the manifest
            foreach (var f in SafeFiles(Path.Combine(dir, "oz"), "pak_oz_*.pk3").Concat(SafeFiles(Path.Combine(dir, "oz"), "zz-oz-*.pk3")))
                Installer.TryDelete(f);
            Installer.TryDeleteDir(Path.Combine(dir, ".oz-setup"));
            foreach (var sub in new[] { "tools", "oz", "Maps" })
                RemoveIfEmpty(Path.Combine(dir, sub));
            Installer.TryDelete(Path.Combine(dir, Installer.Manifest));
            Installer.TryDelete(Path.Combine(dir, Installer.Uninstaller));
            Shell.RemoveShortcuts();
            Unregister(Key);
            Unregister(Scheme);
            RemoveIfEmpty(dir);
            Interlocked.Increment(ref p.Done);
            p.Step = "Done";
        }

        static IEnumerable<string> SafeFiles(string d, string pattern) => Directory.Exists(d) ? Directory.GetFiles(d, pattern) : new string[0];

        static void RemoveIfEmpty(string d)
        {
            try
            {
                if (Directory.Exists(d) && !Directory.EnumerateFileSystemEntries(d).Any())
                    Directory.Delete(d);
            }
            catch { }
        }
    }

    static class Shell
    {
        static string StartMenu => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Oz Arena.lnk");
        static string Desktop => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Oz Arena.lnk");

        public static void Shortcuts(string dir, bool desktop)
        {
            string exe = Path.Combine(dir, "OzArena.exe");
            Link(StartMenu, exe, dir);
            if (desktop)
                Link(Desktop, exe, dir);
            else
                Installer.TryDelete(Desktop);
        }

        public static void RemoveShortcuts()
        {
            Installer.TryDelete(StartMenu);
            Installer.TryDelete(Desktop);
        }

        static void Link(string lnk, string target, string workDir)
        {
            try
            {
                dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
                dynamic l = sh.CreateShortcut(lnk);
                l.TargetPath = target;
                l.WorkingDirectory = workDir;
                l.IconLocation = target + ",0";
                l.Description = "Oz Arena";
                l.Save();
            }
            catch { }
        }

        /// <summary>Starts the game as the player, never with Setup's administrator rights.</summary>
        public static void Launch(string dir)
        {
            string exe = Path.Combine(dir, "OzArena.exe");
            if (Installer.IsAdmin && Environment.GetCommandLineArgs().Contains("/elevated"))
                Process.Start("explorer.exe", "\"" + exe + "\"");
            else
                Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = dir, UseShellExecute = true });
        }
    }
}
