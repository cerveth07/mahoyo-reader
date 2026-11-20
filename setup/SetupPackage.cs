using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Collections.Generic;
using Microsoft.Win32;

namespace Mahoyo.Book {
    enum SetupState { Missing, Ready, Disabled, Installed, Pending, Conflict }

    static class SetupPackage {
        const string Resource = "Mahoyo.Setup.FullGameZip";
        const string GameHash = "7a718cfca3fccafd9d7984e41206473a6152098d6e0f7374c52abf1581b6ad9a";
        const string ArchiveRoot = "mahoyo-yomibito/";
        const string TempPrefix = "Mahoyo-Yomibito-Setup-";

        internal static string GameFolder(string input) {
            if (String.IsNullOrWhiteSpace(input)) return "";
            string clean = input.Trim().Trim('"');
            if (String.Equals(Path.GetFileName(clean), "WoH.exe", StringComparison.OrdinalIgnoreCase))
                clean = Path.GetDirectoryName(Path.GetFullPath(clean));
            string full = Path.GetFullPath(clean);
            return full.Length > Path.GetPathRoot(full).Length ? full.TrimEnd('\\', '/') : full;
        }

        static string ConfigValue(string line, string key) {
            Match m = Regex.Match(line, "^\\s*\"" + key + "\"\\s+\"([^\"]+)\"");
            return m.Success ? m.Groups[1].Value.Replace(@"\\", @"\") : "";
        }

        internal static string FindGame() {
            string steam = Convert.ToString(Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", ""));
            if (String.IsNullOrEmpty(steam)) return "";
            var libraries = new List<string> { steam };
            string config = Path.Combine(steam, @"steamapps\libraryfolders.vdf");
            if (File.Exists(config)) {
                foreach (string line in File.ReadLines(config)) {
                    string lib = ConfigValue(line, "path");
                    if (lib.Length > 0 && !libraries.Contains(lib)) libraries.Add(lib);
                }
            }
            foreach (string library in libraries) {
                string folder = "WITCH ON THE HOLY NIGHT";
                string app = Path.Combine(library, @"steamapps\appmanifest_2052410.acf");
                if (File.Exists(app))
                    foreach (string line in File.ReadLines(app)) {
                        string name = ConfigValue(line, "installdir");
                        if (name.Length > 0) { folder = name; break; }
                    }
                string target = Path.Combine(library, "steamapps", "common", folder);
                if (File.Exists(Path.Combine(target, "WoH.exe"))) return GameFolder(target);
            }
            return "";
        }

        static string Digest(Stream input) {
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "").ToLowerInvariant();
        }

        static string Field(Dictionary<string,object> record, string key) {
            object value;
            return record != null && record.TryGetValue(key, out value) ? Convert.ToString(value) : "";
        }

        internal static SetupState Inspect(string root) {
            if (String.IsNullOrEmpty(root) || !File.Exists(Path.Combine(root, "WoH.exe"))) return SetupState.Missing;
            string dll = Path.Combine(root, "d3d9.dll");
            string receipt = Path.Combine(root, @"mahoyo-learning\installed.json");
            if (Directory.Exists(dll)) return SetupState.Conflict;
            if (!File.Exists(dll)) return File.Exists(receipt) ? SetupState.Disabled : SetupState.Ready;
            if (!File.Exists(receipt)) return SetupState.Conflict;
            try {
                var record = new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(receipt));
                string family=Field(record,"sample"), format=Field(record,"format"), state=Field(record,"state");
                if ((family!="full-game" && family!="chapter01") || Field(record,"game_sha256")!=GameHash ||
                    (format!="" && format!="mahoyo-learning-installed-v2") ||
                    (state!="" && state!="installed" && state!="installing")) return SetupState.Conflict;
                using(var stream = File.OpenRead(dll)) {
                    string hash = Digest(stream);
                    if (hash.Equals(Field(record,"dll_sha256"),StringComparison.OrdinalIgnoreCase))
                        return state=="installing" ? SetupState.Pending : SetupState.Installed;
                    if (state=="installing" && hash.Equals(Field(record,"previous_dll_sha256"),StringComparison.OrdinalIgnoreCase))
                        return SetupState.Pending;
                }
            } catch (ArgumentException) { }
              catch (InvalidOperationException) { }
            return SetupState.Conflict;
        }

        static void Unpack(string temp, bool install) {
            string boundary = Path.GetFullPath(temp) + Path.DirectorySeparatorChar;
            using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream(Resource))
            using (var zip = new ZipArchive(source, ZipArchiveMode.Read)) {
                foreach (var item in zip.Entries) {
                    if (!item.FullName.StartsWith(ArchiveRoot, StringComparison.Ordinal))
                        throw new InvalidDataException("安装包内文件路径无效。");
                    string relative = item.FullName.Substring(ArchiveRoot.Length);
                    if (relative.Length==0 || item.Name.Length==0 || (!install && relative!="Uninstall.ps1")) continue;
                    string file = Path.GetFullPath(Path.Combine(temp, relative.Replace('/', '\\')));
                    if (!file.StartsWith(boundary,StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("安装包内文件路径无效。");
                    Directory.CreateDirectory(Path.GetDirectoryName(file));
                    using(var entry = item.Open())
                    using(var dst = new FileStream(file, FileMode.CreateNew, FileAccess.Write)) entry.CopyTo(dst);
                }
            }
        }

        internal static string FriendlyError(string log) {
            if (log.IndexOf("Close the game",StringComparison.OrdinalIgnoreCase)>=0) return "请先退出《魔法使之夜》，再试一次。";
            if (log.Contains("different game executable")) return "这个游戏版本与读本不兼容。当前支持 Steam Windows 版 Build 13060638。";
            if (log.Contains("ownership") || log.Contains("another program") || log.Contains("Another d3d9.dll")) return "游戏目录中的 d3d9.dll 归属不符。请检查已有 MOD。";
            if (log.Contains("denied") || log.Contains("拒绝访问")) return "写入游戏目录时被拒绝。请检查该目录的写入权限后重试。";
            if (log.Contains("integrity") || log.Contains("manifest has") || log.Contains("missing")) return "安装文件未通过完整性检查，请重新取得安装器。";
            return "操作未完成，可查看下方的运行详情。";
        }

        internal static void Run(string root, bool install) {
            string key;
            using(var bytes = new MemoryStream(Encoding.UTF8.GetBytes(root.ToUpperInvariant()))) key = Digest(bytes);
            using(var mutex = new Mutex(false, @"Local\Mahoyo.Yomibito.Setup." + key)) {
                bool owned;
                try { owned = mutex.WaitOne(0); } catch(AbandonedMutexException) { owned = true; }
                if (!owned) throw new InvalidOperationException("另一个安装器正在处理这个游戏目录。");
                string tempBase = Path.GetFullPath(Path.GetTempPath());
                string temp = Path.Combine(tempBase, TempPrefix + Guid.NewGuid().ToString("N"));
                try {
                    Directory.CreateDirectory(temp);
                    Unpack(temp, install);
                    string script = Path.Combine(temp, install ? "Install.ps1" : "Uninstall.ps1");
                    string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
                    // Both values are absolute Windows paths. Environment arguments keep spaces,
                    // ampersands, apostrophes and a trailing drive-root slash literal in PowerShell.
                    var info = new ProcessStartInfo(powershell,
                        "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -Command \"[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); $ErrorActionPreference='Stop'; try { & $env:MAHOYO_SETUP_SCRIPT -GamePath $env:MAHOYO_SETUP_GAME } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }\"") {
                        UseShellExecute=false, CreateNoWindow=true, WindowStyle=ProcessWindowStyle.Hidden,
                        WorkingDirectory=temp, RedirectStandardOutput=true, RedirectStandardError=true,
                        StandardOutputEncoding=Encoding.UTF8, StandardErrorEncoding=Encoding.UTF8
                    };
                    info.EnvironmentVariables["MAHOYO_SETUP_SCRIPT"] = script;
                    info.EnvironmentVariables["MAHOYO_SETUP_GAME"] = root;
                    using(var process = Process.Start(info)) {
                        var stdout = process.StandardOutput.ReadToEndAsync();
                        var stderr = process.StandardError.ReadToEndAsync();
                        process.WaitForExit();
                        Task.WaitAll(stdout,stderr);
                        if (process.ExitCode!=0) throw new InvalidOperationException((stderr.Result+"\n"+stdout.Result).Trim());
                    }
                } finally {
                    try {
                        var dir = new DirectoryInfo(temp);
                        if (dir.Exists && dir.Parent!=null &&
                            String.Equals(dir.Parent.FullName.TrimEnd('\\'),tempBase.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase) &&
                            dir.Name.StartsWith(TempPrefix,StringComparison.Ordinal) && (dir.Attributes & FileAttributes.ReparsePoint)==0)
                            Directory.Delete(dir.FullName,true);
                    } catch(IOException) { } catch(UnauthorizedAccessException) { }
                    finally { mutex.ReleaseMutex(); }
                }
            }
        }
    }
}
