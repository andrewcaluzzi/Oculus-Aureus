// GE Launcher - mission/difficulty/cheat picker for ge.exe.
// Build: csc /target:winexe /out:GELauncher.exe GELauncher.cs
// (csc.exe ships with the .NET Framework: C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe)
// Place GELauncher.exe next to ge.exe. Selections map to the GE_STAGE /
// GE_DIFF / GE_CHEATS environment variables the port understands.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Forms;

class GELauncher : Form
{
    ComboBox mission;
    ComboBox difficulty;
    CheckBox invuln;
    CheckBox invis;
    CheckBox allguns;
    CheckBox turbo;
    CheckBox debuglog;
    CheckBox posview;
    CheckBox mpdrive;
    TextBox testpos;
    TextBox extraenv;
    ComboBox demo;
    Button launch;
    Label status;
    Label romstatus;   // where the ROM was found, or that Data\ is empty (romdata.c order)

    static readonly string[] Missions = new string[]
    {
        "(menus - no direct launch)",
        "dam", "facility", "runway", "surface", "bunker1",
        "silo", "frigate", "surface2", "bunker2", "statue",
        "archives", "streets", "depot", "train", "jungle",
        "control", "caverns", "cradle", "aztec", "egypt",
        "citadel", "complex", "temple", "cuba",
    };

    // MP arena names understood by GE_MP (front.c portDirectLaunchMpTick).
    static readonly string[] Arenas = new string[]
    {
        "temple", "complex", "caves", "library", "basement",
        "stack", "facility", "bunker", "archives", "caverns", "egypt",
    };

    // Attract recording names understood by GE_DEMO (front.c
    // portDirectLaunchDemoTick). Playing one overrides Mission/Difficulty
    // and cheats: the demo file carries its own stage, difficulty and
    // recorded save, and anything that perturbs the sim desyncs it.
    static readonly string[] Demos = new string[]
    {
        "(none)",
        "dam1", "dam2", "facility1", "facility2", "facility3",
        "runway1", "runway2", "bunker1", "bunker2", "silo1",
        "silo2", "frigate1", "frigate2", "train",
    };

    // The last entry doubles as the multiplayer trigger: selecting it
    // swaps the mission list to MP arenas and launches via GE_MP with
    // 4 players (scenario Normal, 10 min, Rockets are the game defaults).
    static readonly string[] Difficulties = new string[]
    {
        "Agent", "Secret Agent", "00 Agent", "007", "Multiplayer (4P)",
    };

    GELauncher()
    {
        // Steam-launched launcher (F67, 2026-09-06): after the window closed the
        // process stayed in Task Manager - the injected overlay hooks the exit
        // path. Two seconds after close, terminate ourselves outright; a normal
        // exit ends this background thread first and nothing happens.
        FormClosed += delegate
        {
            var killer = new System.Threading.Thread(delegate ()
            {
                System.Threading.Thread.Sleep(2000);
                try { Process.GetCurrentProcess().Kill(); } catch { }
            });
            killer.IsBackground = true;
            killer.Start();
        };
        Text = "GE Launcher";
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        ClientSize = new Size(280, 218);
        StartPosition = FormStartPosition.CenterScreen;

        var l1 = new Label { Text = "Mission", Location = new Point(12, 15), AutoSize = true };
        mission = new ComboBox
        {
            Location = new Point(90, 12),
            Width = 178,
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        mission.Items.AddRange(Missions);
        mission.SelectedIndex = 0;

        var l2 = new Label { Text = "Difficulty", Location = new Point(12, 44), AutoSize = true };
        difficulty = new ComboBox
        {
            Location = new Point(90, 41),
            Width = 178,
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        difficulty.Items.AddRange(Difficulties);
        difficulty.SelectedIndex = 0;
        difficulty.SelectedIndexChanged += OnDifficultyChanged;

        invuln = new CheckBox { Text = "Invulnerable", Location = new Point(16, 74), AutoSize = true };
        invis = new CheckBox { Text = "Invisible", Location = new Point(16, 97), AutoSize = true };
        allguns = new CheckBox { Text = "All Weapons + Infinite Ammo", Location = new Point(16, 120), AutoSize = true };
        turbo = new CheckBox { Text = "Turbo Bond", Location = new Point(16, 143), AutoSize = true };
        debuglog = new CheckBox { Text = "Debug log (GE_DUMP_BOND)", Location = new Point(16, 166), AutoSize = true };
        posview = new CheckBox { Text = "Testing man pos (pos/aim overlay)", Location = new Point(16, 189), AutoSize = true };
        mpdrive = new CheckBox
        {
            Text = "MP auto-drive soak (GE_MPDRIVE)",
            Location = new Point(16, 212),
            AutoSize = true,
            Visible = false,
        };

        var l3 = new Label { Text = "Test pos", Location = new Point(12, 238), AutoSize = true };
        testpos = new TextBox
        {
            Location = new Point(90, 235),
            Width = 178,
        };
        var tip = new ToolTip();
        tip.SetToolTip(testpos, "Spawn Bond at x,y,z[,heading 0-360[,pitch]] (GE_SPAWNXYZ). Empty = normal spawn.");

        var l4 = new Label { Text = "Demo", Location = new Point(12, 267), AutoSize = true };
        demo = new ComboBox
        {
            Location = new Point(90, 264),
            Width = 178,
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        demo.Items.AddRange(Demos);
        demo.SelectedIndex = 0;
        tip.SetToolTip(demo, "Play an attract-mode recording immediately (GE_DEMO). Overrides mission/cheats.");

        // Free-form diagnostics: the port's GE_* knobs (GE_BLOCKDBG=1,
        // GE_CLIPDBG=1, GE_TICKHZ=60 ...) without a shell. Space-separated
        // KEY=VALUE pairs, applied last so they can override the checkboxes.
        var l5 = new Label { Text = "Extra env", Location = new Point(12, 296), AutoSize = true };
        extraenv = new TextBox
        {
            Location = new Point(90, 293),
            Width = 178,
        };
        tip.SetToolTip(extraenv, "Extra environment variables for ge.exe, space-separated KEY=VALUE, e.g. GE_BLOCKDBG=1 GE_CLIPDBG=1 GE_ROM=<path to a .z64>. Applied last.");

        ClientSize = new Size(280, 418);
        launch = new Button { Text = "Launch", Location = new Point(40, 326), Width = 95, Height = 30 };
        launch.Click += OnLaunch;
        AcceptButton = launch;

        var options = new Button { Text = "Options...", Location = new Point(145, 326), Width = 95, Height = 30 };
        options.Click += delegate
        {
            using (var f = new OptionsForm(AppDomain.CurrentDomain.BaseDirectory))
            {
                f.ShowDialog(this);
            }
            RefreshRomStatus(); // a hand-edited [data] rom= or a new Data\ file
        };

        // The ROM line (2026-09-15, runtime ROM loading): the first thing a
        // new user hits is an empty Data folder, so say where the game will
        // look before they press Launch. Mirrors port/src/romdata.c's order
        // for the two sources the launcher can see (midas.ini [data] rom=, then
        // Data\*); argv/GE_ROM overrides are the game's business.
        romstatus = new Label { Text = "", Location = new Point(12, 362), AutoSize = true, ForeColor = Color.DimGray, MaximumSize = new Size(260, 0) };
        tip.SetToolTip(romstatus, "The game reads all its data from your own GoldenEye 007 (U) cart image. Put the .z64/.v64/.n64 in the Data folder next to ge.exe, or set [data] rom=<path> in midas.ini, or drop the file onto ge.exe.");
        status = new Label { Text = "", Location = new Point(12, 396), AutoSize = true, ForeColor = Color.DimGray };
        RefreshRomStatus();

        Controls.AddRange(new Control[] { l1, mission, l2, difficulty, invuln, invis, allguns, turbo, debuglog, posview, mpdrive, l3, testpos, l4, demo, l5, extraenv, launch, options, romstatus, status });
    }

    // ---- ROM discovery, the launcher's view (romdata.c is authoritative) ----
    // midas.ini [data] rom= (relative to the exe dir) wins, then the first file in
    // Data\ that is at least a stock cart (12,582,912 bytes) and opens with one
    // of the three N64 magics (.z64 80 37 12 40 / .v64 37 80 40 12 / .n64
    // 40 12 37 80) - the extension is never trusted, same as the game.
    static string RomOrderOf(byte[] h)
    {
        if (h.Length < 4) return null;
        if (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) return "z64";
        if (h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) return "v64, byte-swapped - fine";
        if (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80) return "n64, word-swapped - fine";
        return null;
    }

    static string RomOrderOfFile(string path)
    {
        try
        {
            var fi = new FileInfo(path);
            if (!fi.Exists || fi.Length < 12582912L) return null;
            var h = new byte[4];
            using (var f = File.OpenRead(path))
            {
                if (f.Read(h, 0, 4) != 4) return null;
            }
            return RomOrderOf(h);
        }
        catch { return null; }
    }

    public static string FindRom(string dir, out string how)
    {
        how = null;
        string ini = Path.Combine(dir, "midas.ini");
        if (File.Exists(ini))
        {
            bool indata = false;
            foreach (string raw in File.ReadAllLines(ini))
            {
                string line = raw.Trim();
                if (line.StartsWith("[")) { indata = line.StartsWith("[data]"); continue; }
                if (!indata || line.StartsWith(";") || line.StartsWith("#")) continue;
                int eq = line.IndexOf('=');
                if (eq < 0 || line.Substring(0, eq).Trim() != "rom") continue;
                string val = line.Substring(eq + 1).Trim();
                if (val.Length == 0) continue;
                string path = Path.IsPathRooted(val) ? val : Path.Combine(dir, val);
                string order = RomOrderOfFile(path);
                how = order != null ? "midas.ini [data] rom= (" + order + ")" : "midas.ini [data] rom= - NOT a usable ROM";
                return path;
            }
        }
        string data = Path.Combine(dir, "Data");
        if (Directory.Exists(data))
        {
            var names = Directory.GetFiles(data);
            Array.Sort(names, StringComparer.Ordinal);
            foreach (string path in names)
            {
                string order = RomOrderOfFile(path);
                if (order != null)
                {
                    how = "Data\\ (" + order + ")";
                    return path;
                }
            }
        }
        return null;
    }

    void RefreshRomStatus()
    {
        string dir = AppDomain.CurrentDomain.BaseDirectory;
        string how;
        string rom = FindRom(dir, out how);
        if (rom == null)
        {
            romstatus.ForeColor = Color.Firebrick;
            romstatus.Text = "No ROM in Data\\ - put your GoldenEye 007 (U) .z64/.v64/.n64 there";
            return;
        }
        romstatus.ForeColor = how.Contains("NOT") ? Color.Firebrick : Color.DimGray;
        romstatus.Text = "ROM: " + Path.GetFileName(rom) + " from " + how;
    }

    // Windows builds ship ge.exe; the Linux build is plain "ge" (run via mono)
    public static string FindGe(string dir)
    {
        string exe = Path.Combine(dir, "ge" + ".exe");
        return File.Exists(exe) ? exe : Path.Combine(dir, "ge");
    }

    bool IsMpMode()
    {
        return difficulty.SelectedIndex == Difficulties.Length - 1;
    }

    void OnDifficultyChanged(object sender, EventArgs e)
    {
        bool mp = IsMpMode();
        string keep = mission.SelectedItem as string;

        mission.Items.Clear();
        if (mp)
        {
            mission.Items.AddRange(Arenas);
        }
        else
        {
            mission.Items.AddRange(Missions);
        }

        // keep the selection when the same name exists in the other list
        // (temple/complex/facility/archives/caverns/egypt overlap)
        int idx = keep != null ? mission.Items.IndexOf(keep) : -1;
        mission.SelectedIndex = idx >= 0 ? idx : 0;

        mpdrive.Visible = mp;
        invuln.Enabled = invis.Enabled = allguns.Enabled = turbo.Enabled = !mp;
        demo.Enabled = !mp; // demo recordings are solo-only
    }

    void OnLaunch(object sender, EventArgs e)
    {
        string dir = AppDomain.CurrentDomain.BaseDirectory;
        string exe = GELauncher.FindGe(dir);

        if (!File.Exists(exe))
        {
            status.ForeColor = Color.Firebrick;
            status.Text = "ge.exe / ge not found next to the launcher";
            return;
        }
        RefreshRomStatus(); // the user may have dropped a file into Data\ since

        var psi = new ProcessStartInfo(exe)
        {
            WorkingDirectory = dir,
            UseShellExecute = false,
            // Capture the game's console output: the port's crash handler
            // prints the fault address and an EBP-chain backtrace to stderr,
            // which vanishes when launched from a GUI. Everything goes to a
            // per-run log; on abnormal exit the tail is shown in a dialog.
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        bool demoMode = !IsMpMode() && demo.SelectedIndex > 0;

        if (IsMpMode())
        {
            // Multiplayer: direct MP launch, 4 players; scenario Normal,
            // game length 10 min and weapon set Rockets are the game's
            // shipped defaults so only arena + player count are needed.
            psi.EnvironmentVariables["GE_MP"] = (string)mission.SelectedItem;
            psi.EnvironmentVariables["GE_MPPLAYERS"] = "4";
            if (mpdrive.Checked)
            {
                psi.EnvironmentVariables["GE_MPDRIVE"] = "1";
            }
        }
        else if (demoMode)
        {
            // Attract recording: the demo file supplies stage, difficulty
            // and save; GE_STAGE/cheats/spawn overrides would fight it or
            // desync the input replay, so none of them are passed.
            psi.EnvironmentVariables["GE_DEMO"] = (string)demo.SelectedItem;
        }
        else if (mission.SelectedIndex > 0)
        {
            psi.EnvironmentVariables["GE_STAGE"] = (string)mission.SelectedItem;
            psi.EnvironmentVariables["GE_DIFF"] = difficulty.SelectedIndex.ToString();
        }

        string cheats = "";
        if (!IsMpMode() && !demoMode)
        {
            if (invuln.Checked) cheats += "invuln,";
            if (invis.Checked) cheats += "invis,";
            if (allguns.Checked) cheats += "guns,";
            if (turbo.Checked) cheats += "turbo,";
        }
        if (cheats.Length > 0)
        {
            psi.EnvironmentVariables["GE_CHEATS"] = cheats.TrimEnd(',');
        }

        if (debuglog.Checked)
        {
            psi.EnvironmentVariables["GE_DUMP_BOND"] = "1";
        }

        if (posview.Checked)
        {
            psi.EnvironmentVariables["GE_DEBUGPOS"] = "1";
        }

        if (!IsMpMode() && !demoMode && testpos.Text.Trim().Length > 0)
        {
            psi.EnvironmentVariables["GE_SPAWNXYZ"] = testpos.Text.Trim();
        }

        foreach (string tok in extraenv.Text.Split(new[] { ' ', '\t', ',' }, StringSplitOptions.RemoveEmptyEntries))
        {
            int eq = tok.IndexOf('=');
            string key = eq > 0 ? tok.Substring(0, eq) : tok;
            string val = eq > 0 ? tok.Substring(eq + 1) : "1";   // bare GE_X means GE_X=1
            if (key.Length > 0 && (char.IsLetter(key[0]) || key[0] == '_'))
            {
                psi.EnvironmentVariables[key] = val;
            }
        }

        if (SteamBlockedBackend != null)
        {
            psi.EnvironmentVariables[SteamBlockedBackend] = "0";
        }

        string logPath = Path.Combine(dir,
            "ge_run_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".log");

        try
        {
            PruneOldLogs(dir);

            var log = new StreamWriter(logPath) { AutoFlush = true };
            var logLock = new object();
            log.WriteLine("[launcher] " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")
                + " steam overlay in launcher: " + (SteamOverlayLoaded() ? "yes" : "no")
                + (Environment.GetEnvironmentVariable("SteamGameId") != null ? " (SteamGameId set)" : "")
                + (SteamBlockedBackend != null ? " probe bisect: " + SteamBlockedBackend + "=0 passed to ge.exe" : ""));

            var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };

            DataReceivedEventHandler writeLine = (s, ev) =>
            {
                if (ev.Data == null) return;
                lock (logLock) { log.WriteLine(ev.Data); }
            };
            proc.OutputDataReceived += writeLine;
            proc.ErrorDataReceived += writeLine;

            proc.Exited += (s, ev) =>
            {
                int code = proc.ExitCode;
                try { lock (logLock) { log.WriteLine("[launcher] exit code " + FormatExitCode(code)); log.Close(); } }
                catch { }

                BeginInvoke((Action)(() =>
                {
                    if (code == 0)
                    {
                        status.ForeColor = Color.DimGray;
                        status.Text = "Exited cleanly";
                        return;
                    }

                    status.ForeColor = Color.Firebrick;
                    status.Text = "Crashed: " + FormatExitCode(code);
                    MessageBox.Show(this,
                        "ge.exe exited with " + FormatExitCode(code) + "\n\n"
                        + "Log: " + logPath + "\n\n"
                        + "--- last lines ---\n" + TailOfLog(logPath, 15),
                        "GE crashed",
                        MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }));
            };

            proc.Start();
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();

            status.ForeColor = Color.DimGray;
            status.Text = "Launched" + (demoMode ? ": demo " + demo.SelectedItem
                : (mission.SelectedIndex > 0 ? ": " + mission.SelectedItem : ""));
        }
        catch (Exception ex)
        {
            status.ForeColor = Color.Firebrick;
            status.Text = "Launch failed: " + ex.Message;
        }
    }

    // Set by the Options probe bisect (F67): GE_<BACKEND> whose =0 made SDL's
    // controller init answer under Steam's hook; passed to the game at launch.
    public static string SteamBlockedBackend;

    public static string IniKeyFor(string env)
    {
        return env.StartsWith("GE_") ? env.Substring(3).ToLowerInvariant() : env;
    }

    // gameoverlayrenderer.dll in OUR process = Steam started us (it hooks
    // CreateProcess here and injects into ge.exe too).
    public static bool SteamOverlayLoaded()
    {
        try
        {
            foreach (ProcessModule m in Process.GetCurrentProcess().Modules)
            {
                if (m.ModuleName.ToLowerInvariant().Contains("gameoverlayrenderer")) return true;
            }
        }
        catch { }
        return false;
    }

    static string FormatExitCode(int code)
    {
        // Windows fault codes (0xC0000005 etc.) come back as negative ints.
        return code < 0 ? "0x" + code.ToString("X8") : code.ToString();
    }

    static string TailOfLog(string path, int lines)
    {
        try
        {
            string[] all = File.ReadAllLines(path);
            int start = Math.Max(0, all.Length - lines);
            return string.Join("\n", all, start, all.Length - start);
        }
        catch (Exception ex)
        {
            return "(could not read log: " + ex.Message + ")";
        }
    }

    static void PruneOldLogs(string dir)
    {
        try
        {
            string[] logs = Directory.GetFiles(dir, "ge_run_*.log");
            Array.Sort(logs);
            for (int i = 0; i < logs.Length - 5; i++)
            {
                File.Delete(logs[i]);
            }
        }
        catch { }
    }

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.Run(new GELauncher());
    }
}

// ---------------------------------------------------------------------------
// Options dialog: edits midas.ini next to ge.exe - display/input options plus
// the [controls] key bindings with press-to-rebind capture. The file is
// REGENERATED in the port's own canonical format on save (midas.ini is a
// generated file; the port writes the same shape when it is missing), so
// hand-added comments do not survive - by design.
// Option keys/defaults mirror port/src/video.c videoSetDisplayOption /
// videoWriteDefaultIni and port/src/input_pc.c portInputSetOption.
// ---------------------------------------------------------------------------
class OptionsForm : Form
{
    // {ini key, label, default} - order matches the port's sBindInfo table.
    static readonly string[][] BindDefs = new string[][]
    {
        new string[] { "a",            "A",           "" },
        new string[] { "b",            "B",           "" },
        new string[] { "z",            "Z (fire)",    "Mouse Left" },
        new string[] { "l",            "L",           "" },
        new string[] { "r",            "R (aim)",     "Mouse Right" },
        new string[] { "start",        "Start",       "Return" },
        new string[] { "c_up",         "C-Up",        "W" },
        new string[] { "c_down",       "C-Down",      "S" },
        new string[] { "c_left",       "C-Left",      "A" },
        new string[] { "c_right",      "C-Right",     "D" },
        new string[] { "dpad_up",      "D-Pad Up",    "" },
        new string[] { "dpad_down",    "D-Pad Down",  "" },
        new string[] { "dpad_left",    "D-Pad Left",  "" },
        new string[] { "dpad_right",   "D-Pad Right", "" },
        // the Move stick: the only keys that walk outside aim mode now (G228,
        // the game's sBindInfo defaults)
        new string[] { "analog_up",    "Stick Up",    "W" },
        new string[] { "analog_down",  "Stick Down",  "S" },
        new string[] { "analog_left",  "Stick Left",  "A" },
        new string[] { "analog_right", "Stick Right", "D" },
        // Port suite (port/src/joy_port.c) - order matches its sPortActs table.
        new string[] { "reload",         "Port: Reload",          "R" },
        new string[] { "use",            "Port: Use",             "E" },
        new string[] { "walk",           "Port: Walk (hold)",     "Left Shift" },
        new string[] { "run",            "Port: Run (hold)",      "" },
        new string[] { "walk_toggle",    "Port: Walk/Run toggle", "CapsLock" },
        new string[] { "prev_weapon",    "Port: Prev Weapon",     "Mouse Wheel Up" },
        new string[] { "next_weapon",    "Port: Next Weapon",     "Mouse Wheel Down" },
        new string[] { "prev_inventory", "Port: Prev Inventory",  "[" },
        new string[] { "next_inventory", "Port: Next Inventory",  "Q" },
        new string[] { "crouch_toggle",  "Port: Crouch (toggle)", "" },
        new string[] { "crouch_hold",    "Port: Crouch (hold)",   "Left Ctrl" },
        new string[] { "watch_prev",     "Port: Watch prev page", "Comma" },
        new string[] { "watch_next",     "Port: Watch next page", "." },
        new string[] { "watch_objectives", "Port: Pause to Objectives", "" },
        new string[] { "detonate",       "Port: Detonate mines",  "" },
    };
    const int PortBindStart = 18; // first Port-suite row in BindDefs

    readonly string iniPath;
    readonly string exeDir;

    NumericUpDown winW, winH, fov, sensX, sensY, sensAim, trigThresh, stickScale, pads, maxfps, musicVol, sfxVol, shadowAlpha;
    // [display] window_x/window_y/window_display: written by the GAME at exit
    // (last window position + monitor); not editable here, carried through so
    // a Save from this dialog does not forget where the window was.
    string iniWindowX, iniWindowY, iniWindowDisplay;
    // Hover help for options whose label would otherwise have to carry a
    // paragraph (the tick-rate/max-fps relationship in particular).
    readonly ToolTip tip = new ToolTip { AutoPopDelay = 20000, InitialDelay = 350, ReshowDelay = 100 };
    TrackBar deadzonePct;
    Label deadzoneVal;
    CheckBox widescreen, filter2d, highLod, hwMipmaps, n64Aa, n64Speed, roomCache, triHoist, showFps, mouse, dinputPads, showInput, vsync, alwaysWalk, smartMouseLock, detonateUseReload;
    CheckBox adzy, unlockAll, pausePillars, alwaysCrosshair, aimLock, noHeadRoll, crosshairCustom;
    Button crosshairColor;
    ComboBox msaa, filtering, anisotropy;
    readonly string[] keyBinds = new string[BindDefs.Length];

    // Player -> physical controller assignment (midas.ini pad1..pad4) plus
    // per-slot custom controller maps (midas.ini padN_<control>=<token>).
    class PadChoice
    {
        public string Value;   // "keyboard" (view only), "auto", "none", "<guid>[#occ]"
        public string Label;
        public override string ToString() { return Label; }
    }
    class PadDevice
    {
        public string Key;     // "<guid>", "<guid>#occ" or "raphnet<chan>"
        public string Label;
        public int Jidx;       // SDL device index, or the raphnet channel
        public bool Mapped;
        public bool Raphnet;   // vendor-HID RAW path (true N64 poll data)
        public Dictionary<string, string> Defaults = new Dictionary<string, string>();
        // raw token -> controller button name via its SDL mapping ("ge: padnames")
        public Dictionary<string, string> Names = new Dictionary<string, string>();
        // SDL control name ("a", "leftshoulder", "leftx+", "righttrigger") -> raw token ("ge: padsdl")
        public Dictionary<string, string> Sdl = new Dictionary<string, string>();
    }

    // ---- LAYOUT PRESETS from gamecontrollerdefault.txt next to ge.exe (murkantor
    // 2026-08-25: "so a user can add and share their own bindings"). The same
    // file the game seeds new pads from. One layout per line:
    //   Name | match terms | key=value; key=value; ...
    // values = SDL control names or raw tokens, "x,y" = several inputs. The
    // launcher lists every name (first line of a repeated name wins); the
    // game picks the first whose match terms hold.
    readonly List<string> layoutNames = new List<string>();
    readonly List<string[][]> layoutTables = new List<string[][]>();

    void LoadLayoutsFile()
    {
        layoutNames.Clear();
        layoutTables.Clear();
        string path = Path.Combine(exeDir, "gamecontrollerdefault.txt");
        try
        {
            if (File.Exists(path))
            {
                foreach (string raw in File.ReadAllLines(path))
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line[0] == '#') continue;
                    string[] parts = line.Split('|');
                    if (parts.Length < 3) continue;
                    string name = parts[0].Trim();
                    if (name.Length == 0 || layoutNames.Contains(name)) continue;
                    var rows = new List<string[]>();
                    foreach (string kv in string.Join("|", parts, 2, parts.Length - 2).Split(';'))
                    {
                        int eq = kv.IndexOf('=');
                        if (eq <= 0) continue;
                        rows.Add(new string[] { kv.Substring(0, eq).Trim(), kv.Substring(eq + 1).Trim() });
                    }
                    layoutNames.Add(name);
                    layoutTables.Add(rows.ToArray());
                }
            }
        }
        catch { }
        if (layoutSel != null)
        {
            layoutSel.Items.Clear();
            foreach (string n in layoutNames) layoutSel.Items.Add(n);
            if (layoutNames.Count == 0) layoutSel.Items.Add("(no gamecontrollerdefault.txt next to ge.exe)");
            layoutSel.SelectedIndex = 0;
        }
    }

    static bool LooksLikeRawToken(string s)
    {
        return s.Length >= 2 && (s[0] == 'b' || s[0] == 'a' || s[0] == 'h') && char.IsDigit(s[1]);
    }

    /* ---- controller mapping export / import (murkantor 2026-09-06: "add an
     * option to export and load controller mappings in the launcher") ----
     * File = plain ini text, one of:
     *   [controller]  <action>=<raw tokens>   (what midas.ini padN_<action> holds)
     *                 <action>.sdl=<SDL names> (written when the device's probe
     *                   mapping can name every token; import PREFERS these and
     *                   translates through the target device's SDL map, so a
     *                   file made on one Xbox-class pad lands on another one -
     *                   raw tokens are the fallback for unmapped devices)
     *                 swap_sticks= / gadget_switch=  (per-pad layout prefs)
     *   [keyboard]    <bind>=<keys>            (the global keyboard binds)
     * Calibration and rumble are physical-device state, deliberately NOT in
     * the file. Nothing is written to midas.ini until OK/Save as usual. */
    const string MapFileFilter = "GE controller mapping (*.gemap)|*.gemap|All files (*.*)|*.*";

    static string SafeFileName(string s)
    {
        var sb = new StringBuilder();
        foreach (char c in s)
        {
            sb.Append((char.IsLetterOrDigit(c) || c == '-' || c == '_' || c == ' ') ? c : '_');
        }
        string r = sb.ToString().Trim();
        return r.Length > 0 ? r : "controller";
    }

    void ExportMapping()
    {
        var sel = deviceSel.SelectedItem as PadChoice;
        if (sel == null) return;
        int p = CurPlayer();
        var sb = new StringBuilder();
        sb.AppendLine("; GE Launcher controller mapping - " + DateTime.Now.ToString("yyyy-MM-dd HH:mm"));
        string suggested;
        if (sel.Value == "keyboard")
        {
            sb.AppendLine("[keyboard]");
            for (int i = 0; i < BindDefs.Length; i++)
            {
                sb.AppendLine(BindDefs[i][0] + "=" + keyBinds[i].Trim());
            }
            suggested = "keyboard.gemap";
        }
        else
        {
            if (sel.Value == "auto" || sel.Value == "none")
            {
                MessageBox.Show(this, "Pick a specific controller in the Device box first - 'auto' and 'none' have no map of their own.",
                                "Export mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            var dev = FindDevice(sel.Value);
            if (dev != null && dev.Raphnet)
            {
                MessageBox.Show(this, "A raphnet N64 controller has nothing to remap - its buttons are the real thing.",
                                "Export mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            Dictionary<string, string> m = padMapEdit[p];
            if (m == null && dev != null) m = dev.Defaults; /* the map the game will use */
            if (m == null)
            {
                MessageBox.Show(this, "This controller is not connected and has no custom map for player " + (p + 1) + " - nothing to export.",
                                "Export mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            /* raw token -> SDL name, inverted from the probe's SDL -> raw table */
            var rawToSdl = new Dictionary<string, string>();
            if (dev != null)
            {
                foreach (var kv in dev.Sdl)
                {
                    if (kv.Value != null && kv.Value.Length > 0 && kv.Value != "-" && !rawToSdl.ContainsKey(kv.Value))
                    {
                        rawToSdl[kv.Value] = kv.Key;
                    }
                }
            }
            sb.AppendLine("[controller]");
            sb.AppendLine("device=" + ((dev != null) ? dev.Label : sel.Label).Replace('\n', ' '));
            foreach (string[] cd in PadCtrlDefs)
            {
                string tok;
                if (!m.TryGetValue(cd[0], out tok) || tok == null || tok.Length == 0) tok = "-";
                sb.AppendLine(cd[0] + "=" + tok);
                if (tok != "-" && rawToSdl.Count > 0)
                {
                    var names = new List<string>();
                    bool all = true;
                    foreach (string part in tok.Split(','))
                    {
                        string nm;
                        string t = part.Trim();
                        if (t.Length == 0) continue;
                        if (rawToSdl.TryGetValue(t, out nm)) names.Add(nm); else { all = false; break; }
                    }
                    if (all && names.Count > 0) sb.AppendLine(cd[0] + ".sdl=" + string.Join(",", names.ToArray()));
                }
            }
            sb.AppendLine("swap_sticks=" + (padSwapSticks[p] ? "1" : "0"));
            sb.AppendLine("gadget_switch=" + (padGadgetSwitch[p] ? "1" : "0"));
            suggested = SafeFileName((dev != null) ? dev.Label : sel.Label) + ".gemap";
        }
        using (var dlg = new SaveFileDialog
        {
            Title = "Export controller mapping",
            Filter = MapFileFilter,
            DefaultExt = "gemap",
            FileName = suggested,
            InitialDirectory = exeDir,
            OverwritePrompt = true,
        })
        {
            if (dlg.ShowDialog(this) != DialogResult.OK) return;
            try
            {
                File.WriteAllText(dlg.FileName, sb.ToString(), new UTF8Encoding(false));
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Could not write the file:\n" + ex.Message, "Export mapping", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }

    void ImportMapping()
    {
        var sel = deviceSel.SelectedItem as PadChoice;
        if (sel == null) return;
        string path;
        using (var dlg = new OpenFileDialog
        {
            Title = "Import controller mapping",
            Filter = MapFileFilter,
            InitialDirectory = exeDir,
            CheckFileExists = true,
        })
        {
            if (dlg.ShowDialog(this) != DialogResult.OK) return;
            path = dlg.FileName;
        }
        string section = "";
        var kv = new Dictionary<string, string>();
        try
        {
            foreach (string raw in File.ReadAllLines(path))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line[0] == ';' || line[0] == '#') continue;
                if (line[0] == '[')
                {
                    int end = line.IndexOf(']');
                    if (end > 1 && section.Length == 0) section = line.Substring(1, end - 1).Trim().ToLowerInvariant();
                    continue;
                }
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                kv[line.Substring(0, eq).Trim().ToLowerInvariant()] = line.Substring(eq + 1).Trim();
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "Could not read the file:\n" + ex.Message, "Import mapping", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        if (section == "keyboard")
        {
            int applied = 0;
            for (int i = 0; i < BindDefs.Length; i++)
            {
                string v;
                if (kv.TryGetValue(BindDefs[i][0].ToLowerInvariant(), out v)) { keyBinds[i] = v; applied++; }
            }
            if (sel.Value != "keyboard")
            {
                MessageBox.Show(this, "That file holds KEYBOARD binds - " + applied + " loaded. Select Keyboard in the Device box to see them.",
                                "Import mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            RenderRows();
            return;
        }
        if (section != "controller")
        {
            MessageBox.Show(this, "Not a GE controller mapping file (expected a [controller] or [keyboard] section).",
                            "Import mapping", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        if (sel.Value == "keyboard" || sel.Value == "auto" || sel.Value == "none")
        {
            MessageBox.Show(this, "That file holds a CONTROLLER map. Pick the specific controller it should apply to in the Device box, then import again.",
                            "Import mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        var dev = FindDevice(sel.Value);
        if (dev != null && dev.Raphnet)
        {
            MessageBox.Show(this, "A raphnet N64 controller has nothing to remap - its buttons are the real thing.",
                            "Import mapping", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        int p = CurPlayer();
        var m = new Dictionary<string, string>();
        int viaSdl = 0, viaRaw = 0;
        foreach (string[] cd in PadCtrlDefs)
        {
            string key = cd[0].ToLowerInvariant();
            string tok = "-";
            string sdl, rawv;
            /* prefer the device-independent SDL names when THIS device can resolve them */
            if (dev != null && dev.Sdl.Count > 0 && kv.TryGetValue(key + ".sdl", out sdl) && sdl.Length > 0)
            {
                var toks = new List<string>();
                foreach (string nm in sdl.Split(','))
                {
                    string t;
                    string n = nm.Trim();
                    if (n.Length > 0 && dev.Sdl.TryGetValue(n, out t) && t.Length > 0 && t != "-") toks.Add(t);
                }
                if (toks.Count > 0) { tok = string.Join(",", toks.ToArray()); viaSdl++; }
            }
            if (tok == "-" && kv.TryGetValue(key, out rawv) && rawv.Length > 0)
            {
                tok = rawv;
                if (tok != "-") viaRaw++;
            }
            m[cd[0]] = tok;
        }
        padMapEdit[p] = m;
        string sv;
        int sn;
        if (kv.TryGetValue("swap_sticks", out sv) && int.TryParse(sv, out sn)) padSwapSticks[p] = sn != 0;
        if (kv.TryGetValue("gadget_switch", out sv) && int.TryParse(sv, out sn)) padGadgetSwitch[p] = sn != 0;
        RenderRows();
        string from;
        if (!kv.TryGetValue("device", out from)) from = "?";
        padProbeStatus.Text = "imported for player " + (p + 1) + ": " + viaSdl + " by name, " + viaRaw + " raw (from " + from + ")";
    }

    /* Apply a preset to the current player's controller: every PadCtrlDefs
     * key -> the preset's SDL names -> this device's raw tokens. */
    void ApplyLayout(PadDevice dev, string[][] layout)
    {
        int p = CurPlayer();
        var m = new Dictionary<string, string>();
        foreach (string[] cd in PadCtrlDefs) m[cd[0]] = "-";
        foreach (string[] row in layout)
        {
            var toks = new List<string>();
            foreach (string sdl in row[1].Split(','))
            {
                string t;
                string key = sdl.Trim();
                if (dev.Sdl.TryGetValue(key, out t) && t.Length > 0 && t != "-") toks.Add(t);
                else if (LooksLikeRawToken(key)) toks.Add(key); /* device-specific raw token */
            }
            if (toks.Count > 0) m[row[0]] = string.Join(",", toks.ToArray());
        }
        padMapEdit[p] = m;
        RenderRows();
    }

    /* A token (or "tok1,tok2" list) as the controller's own button names
     * when the probe gave us its mapping, else the raw "button 3" form. */
    static string PrettyTokenFor(PadDevice dev, string t)
    {
        if (t == null || t.Length == 0 || t == "-") return "(unbound)";
        var parts = new List<string>();
        foreach (string s in t.Split(','))
        {
            string p = s.Trim();
            string nm;
            if (p.Length == 0) continue;
            if (dev != null && dev.Names.TryGetValue(p, out nm)) parts.Add(nm);
            else parts.Add(PrettyToken(p));
        }
        return string.Join(", ", parts.ToArray());
    }
    // Controller-side rebindable controls (midas.ini suffix, UI label,
    // capture prompt hint, group header). Keys are the port's sPadMapNames;
    // display order is by group. murkantor 2026-08-25: ACTION names, not N64
    // button names - labels describe the default 1.2 / 2.2 twin-stick
    // layout (left stick moves, right stick looks, RT fires, LT aims).
    static readonly string[][] PadCtrlDefs = new string[][]
    {
        new string[] { "stick_x",    "Strafe (Move stick X)",         "push the MOVE stick RIGHT", "Movement - sticks" },
        new string[] { "stick_y",    "Forward / back (Move stick Y)", "push the MOVE stick UP",    "Movement - sticks" },
        new string[] { "look_x",     "Turn (Look stick X)",           "push the LOOK stick RIGHT", "Movement - sticks" },
        new string[] { "look_y",     "Look up / down (Look stick Y)", "push the LOOK stick UP",    "Movement - sticks" },
        // digital movement: a button / hat / axis direction at full deflection
        new string[] { "move_up",    "Move forward",  "press it", "Movement - buttons (optional)" },
        new string[] { "move_down",  "Move backward", "press it", "Movement - buttons (optional)" },
        new string[] { "move_left",  "Strafe left",   "press it", "Movement - buttons (optional)" },
        new string[] { "move_right", "Strafe right",  "press it", "Movement - buttons (optional)" },
        new string[] { "look_up",    "Look up",       "press it", "Movement - buttons (optional)" },
        new string[] { "look_down",  "Look down",     "press it", "Movement - buttons (optional)" },
        new string[] { "look_left",  "Turn left",     "press it", "Movement - buttons (optional)" },
        new string[] { "look_right", "Turn right",    "press it", "Movement - buttons (optional)" },
        // the N64 face controls, named by what they do in the default styles
        // the N64 face controls: the N64 name is what a USB N64 controller
        // passes straight through, the role is what it does in the default styles
        new string[] { "z",          "Fire  (N64 Z)",            "press the button / pull the trigger", "N64 buttons / actions" },
        new string[] { "r",          "Aim  (N64 R)",             "press the button / pull the trigger", "N64 buttons / actions" },
        new string[] { "a",          "Change weapon  (N64 A)",   "press the button", "N64 buttons / actions" },
        new string[] { "b",          "Use / Reload  (N64 B)",    "press the button", "N64 buttons / actions" },
        new string[] { "start",      "Pause  (N64 Start)",       "press the button", "N64 buttons / actions" },
        new string[] { "l",          "Aim alternate  (N64 L)",   "press the button", "N64 buttons / actions" },
        // Port suite actions on this controller (joy_port.c; unbound by default).
        new string[] { "reload",         "Reload",              "press the button", "Port actions" },
        new string[] { "use",            "Use",                 "press the button", "Port actions" },
        new string[] { "walk",           "Walk (hold)",         "press the button / pull the trigger", "Port actions" },
        new string[] { "run",            "Run (hold)",          "press the button / pull the trigger", "Port actions" },
        new string[] { "walk_toggle",    "Walk / Run toggle",   "press the button", "Port actions" },
        new string[] { "prev_weapon",    "Previous weapon",     "press the button", "Port actions" },
        new string[] { "next_weapon",    "Next weapon",         "press the button", "Port actions" },
        new string[] { "prev_inventory", "Previous gadget",     "press the button", "Port actions" },
        new string[] { "next_inventory", "Next gadget",         "press the button", "Port actions" },
        new string[] { "crouch_toggle",  "Crouch (toggle)",     "press the button", "Port actions" },
        new string[] { "crouch_hold",    "Crouch (hold)",       "press the button / pull the trigger", "Port actions" },
        new string[] { "watch_prev",     "Watch: previous page", "press the button", "Port actions" },
        new string[] { "watch_next",     "Watch: next page",    "press the button", "Port actions" },
        new string[] { "watch_objectives", "Pause to Objectives", "press the button", "Port actions" },
        new string[] { "detonate",       "Detonate mines",      "press the button", "Port actions" },
        // the rest of sPadMapNames: unknown here they were dropped by Apply
        // layout (G210) and survived Reset as extras (G086)
        new string[] { "zoom_in",        "Zoom in (aiming)",    "press it / push the stick", "Port actions" },
        new string[] { "zoom_out",       "Zoom out (aiming)",   "press it / push the stick", "Port actions" },
        new string[] { "menu_accept",    "Menu: accept",        "press the button", "Port actions" },
        new string[] { "menu_back",      "Menu: back",          "press the button", "Port actions" },
        new string[] { "watch_up",       "Watch: up",           "press the button", "Port actions" },
        new string[] { "watch_down",     "Watch: down",         "press the button", "Port actions" },
        // the rest of the N64 pad: C buttons (digital; the Look stick above
        // replaces them on twin-stick pads) and the D-pad - pure pass-through
        new string[] { "c_up",       "N64 C-Up",        "press it, or push the C stick UP",    "N64 C buttons and D-pad (pass-through)" },
        new string[] { "c_down",     "N64 C-Down",      "press it, or push the C stick DOWN",  "N64 C buttons and D-pad (pass-through)" },
        new string[] { "c_left",     "N64 C-Left",      "press it, or push the C stick LEFT",  "N64 C buttons and D-pad (pass-through)" },
        new string[] { "c_right",    "N64 C-Right",     "press it, or push the C stick RIGHT", "N64 C buttons and D-pad (pass-through)" },
        new string[] { "dpad_up",    "N64 D-Pad Up",    "press it", "N64 C buttons and D-pad (pass-through)" },
        new string[] { "dpad_down",  "N64 D-Pad Down",  "press it", "N64 C buttons and D-pad (pass-through)" },
        new string[] { "dpad_left",  "N64 D-Pad Left",  "press it", "N64 C buttons and D-pad (pass-through)" },
        new string[] { "dpad_right", "N64 D-Pad Right", "press it", "N64 C buttons and D-pad (pass-through)" },
    };
    readonly List<PadDevice> padDevices = new List<PadDevice>();
    readonly string[] padAssign = new string[] { "auto", "auto", "auto", "auto" };
    readonly Dictionary<string, string>[] padMapEdit = new Dictionary<string, string>[4];
    readonly string[] padCalib = new string[4]; // 16-int csv (padN_calib) or null
    readonly string[] padStickMode = new string[4]; // null (derived by the game), or "calibrated"
    readonly bool[] padRumbleOn = new bool[] { true, true, true, true }; // padN_rumble
    readonly bool[] padSwapSticks = new bool[4]; // padN_swap_sticks, default off
    readonly string[] padLayoutName = new string[4]; // padN_layout: the layout last applied - the game reads its n64 tag (G208)
    readonly bool[] padGadgetSwitch = new bool[4]; // padN_gadget_switch: B then Z = next gadget, default off
    ComboBox playerSel, deviceSel, layoutSel;
    Panel bindPan;
    Label padProbeStatus;
    bool suppressDeviceEvent;

    static readonly int[] MsaaValues = { 1, 2, 4, 8 };
    /* [display] low_res, in the order the combo lists them: Hardware (the N64's
       own 240 lines, legacy 1 or "hw"), then the real numbers, then Native =
       the window's own resolution. Keep these two in step. */
    static readonly int[] LowResValues = { 240, 480, 720, 1080, 0 };
    const int LowResNativeIndex = 4;
    ComboBox lowRes;
    static readonly int[] AnisoValues = { 0, 2, 4, 8, 16 };
    static readonly string[] FilterValues = { "point", "bilinear", "3point", "trilinear" };
    /* [display] fullscreen: the 3-way display mode (menu overhaul 2026-09-16).
       "fullscreen" is the port's borderless window over the display, which is
       what fullscreen=1 has always meant; "exclusive" is true fullscreen.
       Legacy 0 and 1 still load. */
    static readonly string[] DisplayModeValues = { "windowed", "fullscreen", "exclusive" };
    ComboBox displayMode;

    public OptionsForm(string dir)
    {
        iniPath = Path.Combine(dir, "midas.ini");
        exeDir = dir;

        Text = "GE Options";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        ClientSize = new Size(490, 484);

        var tabs = new TabControl { Location = new Point(8, 8), Size = new Size(474, 432) };
        /* AutoScroll: the Display tab outgrew the page once tickhz and Adzy
         * mode were added (14 rows at 30px from y=12 is ~432px against ~406px
         * of usable page). Scrolling rather than a taller dialog keeps it
         * usable on short screens - the Deck is 800px tall - and the
         * scrollbar only appears when a tab actually overflows, so the
         * Input and Controls tabs look unchanged. The Controls tab already
         * scrolls its bind list the same way. */
        var tabDisplay = new TabPage("Display") { AutoScroll = true };
        var tabInput = new TabPage("Input") { AutoScroll = true };
        var tabControls = new TabPage("Controls") { AutoScroll = true };
        tabs.TabPages.AddRange(new TabPage[] { tabDisplay, tabInput, tabControls });

        // ---- Display tab ----
        int y = 12;
        winW = AddNum(tabDisplay, "Window width", ref y, 320, 7680, 0, 10);
        winH = AddNum(tabDisplay, "Window height", ref y, 240, 4320, 0, 10);
        displayMode = AddCombo(tabDisplay, "Display mode", ref y, new string[] { "Windowed", "Fullscreen (borderless window over the display)", "Exclusive fullscreen" });
        widescreen = AddCheck(tabDisplay, "Widescreen (FOV follows window shape)", ref y);
        msaa = AddCombo(tabDisplay, "MSAA", ref y, new string[] { "Off", "2x", "4x", "8x" });
        filtering = AddCombo(tabDisplay, "Texture filtering", ref y, new string[] { "point", "bilinear", "3point (N64)", "trilinear (mip chains on every draw)" });
        anisotropy = AddCombo(tabDisplay, "Anisotropy", ref y, new string[] { "Off", "2x", "4x", "8x", "16x" });
        filter2d = AddCheck(tabDisplay, "Filter 2D (HUD/menu graphics)", ref y);
        highLod = AddCheck(tabDisplay, "High LODs always (no model detail pop)", ref y);
        lowRes = AddCombo(tabDisplay, "Resolution (levels render at this height, scaled up)", ref y, new string[] { "Hardware (240p)", "480p", "720p", "1080p", "Native" });
        hwMipmaps = AddCheck(tabDisplay, "Mipmaps (N64-style mip chains: far textures stop shimmering / moire)", ref y);
        n64Aa = AddCheck(tabDisplay, "N64 AA (the console's edge blend + soft scaling; uses MSAA, best with Low Res HW)", ref y);
        roomCache = AddCheck(tabDisplay, "GPU room / model cache (static geometry kept on the GPU after its first frame - low-end machines)", ref y);
        triHoist = AddCheck(tabDisplay, "Per-draw state hoist (renderer derives draw state per state change, not per triangle)", ref y);
        showFps = AddCheck(tabDisplay, "Show FPS/TPS in game (overlay)", ref y);
        fov = AddNum(tabDisplay, "FOV (60 = original)", ref y, 40, 120, 0, 1);
        shadowAlpha = AddNum(tabDisplay, "Shadow strength % (100 = N64 value; players found that too dark)", ref y, 0, 150, 0, 5);
        musicVol = AddNum(tabDisplay, "Music volume %", ref y, 0, 100, 0, 5,
            "Applied from launch, everywhere - including the intros and the menus, which the original game always plays at full volume.\n\n" +
            "Whatever save file you last loaded writes its own volumes here, so the setting follows you.");
        sfxVol = AddNum(tabDisplay, "Sound volume %", ref y, 0, 100, 0, 5,
            "Sound effects volume, applied from launch the same way as Music volume.");
        maxfps = AddNum(tabDisplay, "Max FPS", ref y, 0, 1000, 0, 10,
            "Frame rate, and the simulation rate with it. 0 = uncapped.\n\n" +
            "There is ONE rate knob now: the game simulates once per frame, so 120 " +
            "here is a 120 Hz simulation and 0 is an uncapped one. The separate Sim " +
            "tick rate setting is gone.\n\n" +
            "Game speed is unchanged at any value: the time unit stays 1/60s, so AI, " +
            "animation, ammo and every authored duration keep their original 60Hz " +
            "cadence. Only continuous motion and input polling run faster.\n\n" +
            "Below 60 the simulation stays at 60 and only the frames are capped.");
        vsync = AddCheck(tabDisplay, "VSync (off = tearing possible; off + Max FPS 0 = fully uncapped)", ref y);
        n64Speed = AddCheck(tabDisplay, "N64 speed (tickhz=hw: the console's frame rate, slowdown and stepped motion)", ref y);
        adzy = AddCheck(tabDisplay, "Adzy mode (Siberian Special Forces + snow cuff)", ref y);
        unlockAll = AddCheck(tabDisplay, "Unlock all (every mission, difficulty and cheat available)", ref y);
        pausePillars = AddCheck(tabDisplay, "Pause pillarbox (black bars ease in to 4:3 while the watch is up)", ref y);
        alwaysCrosshair = AddCheck(tabDisplay, "Always-on crosshair (Perfect Dark style target while not aiming)", ref y);
        tip.SetToolTip(alwaysCrosshair,
            "Draws Perfect Dark's small target plus, in GoldenEye's sight red, at the gun's aim point " +
            "whenever you are not aiming - it sways with the gun and settles on " +
            "the screen centre. Aim mode keeps GoldenEye's own sight; the game's " +
            "Sight-on-screen option still hides everything.");
        aimLock = AddCheck(tabDisplay, "Aim lock (crosshair pinned to centre; aiming turns the view)", ref y);
        crosshairCustom = AddCheck(tabDisplay, "Custom crosshair colour", ref y);
        tip.SetToolTip(crosshairCustom,
            "Recolours BOTH crosshairs - the aim sight and the always-on target.\n\n" +
            "Off = the original red.");
        crosshairColor = AddColor(tabDisplay, "Crosshair colour", ref y,
            "Written to midas.ini as crosshair_color=RRGGBB. Only used when \"Custom crosshair colour\" is ticked.");
        tip.SetToolTip(aimLock,
            "While aiming, the crosshair stays in the middle of the screen and the " +
            "mouse / stick turn the view instead of moving the crosshair (modern " +
            "aim-down-sights feel). Outside aim mode the gun no longer sways off " +
            "centre while you turn, so the weapon stays locked to the middle.\n\n" +
            "Auto-aim still steers the gun, and the tank keeps its own turret handling.");
        noHeadRoll = AddCheck(tabDisplay, "No head roll (no camera tilt, no standing-still breathing sway; any zoom)", ref y);
        tip.SetToolTip(noHeadRoll,
            "Removes the sideways tilt Bond's head animation puts on the camera " +
            "while moving and turning, and the breathing sway while standing still " +
            "(the hidden stamina gauge: the more Bond has run, the bigger and faster " +
            "it gets, and a zoomed sight magnifies it). The walking head bob is " +
            "kept. Applies whether or not you are aiming and at every zoom level.");
        tip.SetToolTip(adzy,
            "Replaces Bond at the start of every solo mission with the Siberian " +
            "Special Forces model wearing the snow cuff, whatever the level would " +
            "normally give him.\n\n" +
            "Purely cosmetic - weapons, objectives, difficulty and everything else " +
            "are untouched.");

        // ---- Input tab ----
        y = 12;
        mouse = AddCheck(tabInput, "Mouse look", ref y);
        smartMouseLock = AddCheck(tabInput, "Smart mouse lock (capture the pointer only while controlling Bond)", ref y);
        tip.SetToolTip(smartMouseLock,
            "Off (default): click in the window to capture the pointer; F1 releases it and grabs it again.\n\n" +
            "On: the window takes the mouse by itself whenever you are controlling Bond, and gives it straight back for menus, the watch, multiplayer pause, cutscenes and death.\n\n" +
            "F1 frees the pointer mid-mission; it stays free until you click back into the window or press F1 again.");
        alwaysWalk = AddCheck(tabInput, "Always walk (half speed; hold the Port 'Run' key for full speed)", ref y);
        detonateUseReload = AddCheck(tabInput, "Use / Reload detonate remote mines while they are in hand (besides the Port 'Detonate' bind)", ref y);
        tip.SetToolTip(detonateUseReload,
            "On (default): with remote mines in your hand, the Port Use key and the Port Reload key " +
            "blow them - the N64's A+B as one press. They have no other job while a mine is held.\n\n" +
            "Off: only the Port 'Detonate mines' bind does it.");
        sensX = AddNum(tabInput, "Mouse sensitivity X", ref y, 0.1m, 20, 1, 0.1m);
        sensY = AddNum(tabInput, "Mouse sensitivity Y", ref y, 0.1m, 20, 1, 0.1m);
        // aim mode drags a cursor across a fixed box rather than rotating the
        // camera, so it gets its own speed (engine: mouse_sens_aim)
        sensAim = AddNum(tabInput, "Mouse sensitivity (aiming)", ref y, 0.1m, 20, 1, 0.1m);
        // right_stick_c / twin_stick retired 2026-09-18: the layout and the control-style row decide these now
        dinputPads = AddCheck(tabInput, "DirectInput controllers (8BitDo DInput mode, N64 USB adapters) - off if launching via Steam", ref y);
        {
            tabInput.Controls.Add(new Label { Text = "Stick deadzone", Location = new Point(12, y + 8), AutoSize = true });
            deadzonePct = new TrackBar
            {
                Location = new Point(150, y),
                Width = 190,
                Minimum = 0,
                Maximum = 100,
                TickFrequency = 10,
                SmallChange = 1,
                LargeChange = 5,
            };
            deadzoneVal = new Label { Location = new Point(346, y + 8), AutoSize = true };
            deadzonePct.ValueChanged += delegate { deadzoneVal.Text = deadzonePct.Value + "%"; };
            tabInput.Controls.Add(deadzonePct);
            tabInput.Controls.Add(deadzoneVal);
            y += 48;
        }
        trigThresh = AddNum(tabInput, "Trigger threshold", ref y, 0, 32767, 0, 256);
        stickScale = AddNum(tabInput, "Stick sensitivity scale", ref y, 0.1m, 3, 2, 0.05m);
        pads = AddNum(tabInput, "N64 pads reported (MP slots)", ref y, 1, 4, 0, 1);
        showInput = AddCheck(tabInput, "Show stick input in-game (X/Y overlay)", ref y);

        // ---- Controls tab: player -> device -> binds ----
        {
            tabControls.Controls.Add(new Label { Text = "Player", Location = new Point(8, 11), AutoSize = true });
            playerSel = new ComboBox
            {
                Location = new Point(52, 8),
                Width = 60,
                DropDownStyle = ComboBoxStyle.DropDownList,
            };
            playerSel.Items.AddRange(new object[] { "1", "2", "3", "4" });
            playerSel.SelectedIndex = 0;
            playerSel.SelectedIndexChanged += delegate { FillDeviceCombo(); };
            tabControls.Controls.Add(playerSel);

            tabControls.Controls.Add(new Label { Text = "Device", Location = new Point(122, 11), AutoSize = true });
            deviceSel = new ComboBox
            {
                Location = new Point(168, 8),
                Width = 190,
                DropDownStyle = ComboBoxStyle.DropDownList,
            };
            deviceSel.SelectedIndexChanged += delegate { OnDeviceSelected(); };
            tabControls.Controls.Add(deviceSel);

            var bRefresh = new Button { Text = "Refresh", Location = new Point(364, 7), Width = 70 };
            bRefresh.Click += delegate { RefreshPads(); LoadLayoutsFile(); };
            tabControls.Controls.Add(bRefresh);

            // Layout presets (see Layouts): pick + Apply = replace this controller's map
            tabControls.Controls.Add(new Label { Text = "Layout", Location = new Point(8, 37), AutoSize = true });
            layoutSel = new ComboBox
            {
                Location = new Point(52, 34),
                Width = 190,
                DropDownStyle = ComboBoxStyle.DropDownList,
            };
            LoadLayoutsFile();
            layoutSel.SelectedIndex = 0;
            tabControls.Controls.Add(layoutSel);
            var bApplyLayout = new Button { Text = "Apply layout", Location = new Point(248, 33), Width = 90 };
            bApplyLayout.Click += delegate
            {
                var sel = deviceSel.SelectedItem as PadChoice;
                var dev = (sel != null) ? FindDevice(sel.Value) : null;
                if (dev == null || dev.Raphnet || layoutSel.SelectedIndex < 0) return;
                if (layoutSel.SelectedIndex >= layoutTables.Count) return;
                ApplyLayout(dev, layoutTables[layoutSel.SelectedIndex]);
                /* recorded as the game's apply records it: the layout's n64 tag
                 * decides whether the pad plays the N64 styles (G208) */
                padLayoutName[CurPlayer()] = layoutNames[layoutSel.SelectedIndex];
            };
            tabControls.Controls.Add(bApplyLayout);

            padProbeStatus = new Label { Text = "", Location = new Point(344, 37), AutoSize = true, ForeColor = Color.DimGray };
            tabControls.Controls.Add(padProbeStatus);

            // Mapping export / import (murkantor 2026-09-06): the binds shown below
            // (this player's controller map, or the keyboard binds) <-> a
            // .gemap text file next to the launcher, shareable between users.
            tabControls.Controls.Add(new Label { Text = "Mapping", Location = new Point(8, 63), AutoSize = true });
            var bExportMap = new Button { Text = "Export...", Location = new Point(60, 59), Width = 80 };
            bExportMap.Click += delegate { ExportMapping(); };
            tabControls.Controls.Add(bExportMap);
            var bImportMap = new Button { Text = "Import...", Location = new Point(144, 59), Width = 80 };
            bImportMap.Click += delegate { ImportMapping(); };
            tabControls.Controls.Add(bImportMap);
            tabControls.Controls.Add(new Label
            {
                Text = "the binds below <-> a .gemap file (share / back up)",
                Location = new Point(232, 63), AutoSize = true, ForeColor = Color.DimGray,
            });

            bindPan = new Panel { Location = new Point(4, 88), Size = new Size(462, 314), AutoScroll = true };
            tabControls.Controls.Add(bindPan);
        }

        var bDefaults = new Button { Text = "Defaults", Location = new Point(8, 448), Width = 90, Height = 28 };
        bDefaults.Click += delegate { ApplyDefaults(); };
        var bOk = new Button { Text = "Save", Location = new Point(300, 448), Width = 85, Height = 28 };
        bOk.Click += delegate { if (SaveIni()) Close(); };
        var bCancel = new Button { Text = "Cancel", Location = new Point(393, 448), Width = 85, Height = 28 };
        bCancel.Click += delegate { Close(); };
        CancelButton = bCancel;

        Controls.AddRange(new Control[] { tabs, bDefaults, bOk, bCancel });

        ApplyDefaults();
        LoadIni();
        RefreshPads();
    }

    // ge.exe GE_LISTPADS=1, optionally with one GE_<BACKEND>=0; null = no answer in
    // 8 s (probe killed). Async read: ReadToEnd() blocked until the child closed
    // stdout, i.e. forever when Steam's hook froze SDL's init inside the probe.
    string RunPadProbe(string exe, string disableEnv)
    {
        var psi = new ProcessStartInfo(exe)
        {
            WorkingDirectory = exeDir,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            CreateNoWindow = true,
        };
        psi.EnvironmentVariables["GE_LISTPADS"] = "1";
        if (dinputPads != null && dinputPads.Checked) psi.EnvironmentVariables["GE_DINPUT"] = "1";
        if (disableEnv != null) psi.EnvironmentVariables[disableEnv] = "0";
        using (var proc = Process.Start(psi))
        {
            var sb = new StringBuilder();
            proc.OutputDataReceived += delegate (object s2, DataReceivedEventArgs ev)
            {
                if (ev.Data != null) lock (sb) sb.AppendLine(ev.Data);
            };
            proc.BeginOutputReadLine();
            if (!proc.WaitForExit(8000))
            {
                try { proc.Kill(); } catch { }
                return null;
            }
            proc.WaitForExit(); // drains the async reader
            lock (sb) return sb.ToString();
        }
    }

    // ---- controller probe (ge.exe GE_LISTPADS=1) ----
    void RefreshPads()
    {
        padDevices.Clear();
        string exe = GELauncher.FindGe(exeDir);

        if (File.Exists(exe))
        {
            try
            {
                string outp = RunPadProbe(exe, null);
                if (outp == null)
                {
                    // BISECT (F67, 2026-09-06: under Steam the probe - SDL controller init
                    // and nothing else - never answered). Retry with one SDL backend off
                    // at a time; the first that answers names the backend Steam's hook
                    // blocks. The launcher then passes that GE_<X>=0 to the game for this
                    // session and tells the user the permanent midas.ini key.
                    string found = null;
                    foreach (string k in new[] { "GE_HIDAPI", "GE_RAWINPUT", "GE_WGI", "GE_XINPUT" })
                    {
                        outp = RunPadProbe(exe, k);
                        if (outp != null) { found = k; break; }
                    }
                    GELauncher.SteamBlockedBackend = found;
                    bool steam = GELauncher.SteamOverlayLoaded();
                    string msg = "The controller probe (ge.exe GE_LISTPADS=1) gave no answer in 8 seconds and was stopped.\n\n";
                    if (found != null)
                    {
                        msg += "Retried with one SDL controller backend off at a time: it answers with " + found + "=0.\n\n"
                            + "This launcher now passes " + found + "=0 to the game for this session. To make it permanent add\n\n"
                            + "    [input]\n    " + GELauncher.IniKeyFor(found) + "=0\n\nto midas.ini"
                            + (steam ? " (Steam's input hook is what blocks that backend)." : ".");
                    }
                    else
                    {
                        msg += "Retried with each SDL controller backend off (hidapi, rawinput, wgi, xinput): none answered.\n\n"
                            + (steam ? "This launcher was started by Steam. Disable the Steam Overlay for this shortcut (Steam > Properties) or run it outside Steam."
                                     : "No controller list this time; the rest of the options still work.");
                    }
                    MessageBox.Show(this, msg, "Controller probe timed out", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    if (outp == null) outp = "";
                }
                {
                    var occSeen = new Dictionary<string, int>();
                    PadDevice last = null;
                    foreach (string raw in outp.Split('\n'))
                    {
                        string s = raw.Trim();
                        // "ge: pad <idx> guid=<hex> <mapped|raw> name=<name>"
                        if (s.StartsWith("ge: pad ") && s.IndexOf("guid=") >= 0)
                        {
                            int g0 = s.IndexOf("guid=") + 5;
                            int g1 = s.IndexOf(' ', g0);
                            if (g1 < 0) continue;
                            string guid = s.Substring(g0, g1 - g0);
                            bool mapped = s.Substring(g1 + 1).StartsWith("mapped");
                            int n0 = s.IndexOf("name=");
                            string name = (n0 >= 0) ? s.Substring(n0 + 5) : "?";
                            int jidx = 0;
                            int.TryParse(s.Substring(8, s.IndexOf(' ', 8) - 8), out jidx);
                            int occ;
                            occSeen.TryGetValue(guid, out occ);
                            occSeen[guid] = occ + 1;
                            last = new PadDevice
                            {
                                Key = (occ == 0) ? guid : guid + "#" + occ,
                                Label = name + (mapped ? "" : "  [raw DInput]") + (occ > 0 ? "  (#" + (occ + 1) + ")" : ""),
                                Jidx = jidx,
                                Mapped = mapped,
                            };
                            padDevices.Add(last);
                        }
                        // "ge: rpad <chan> <present|empty> name=<n>" - raphnet RAW channel
                        else if (s.StartsWith("ge: rpad "))
                        {
                            string[] rp = s.Substring(9).Split(new char[] { ' ' }, 3);
                            int rchan = 0;
                            int.TryParse(rp[0], out rchan);
                            string rname = (rp.Length > 2 && rp[2].StartsWith("name=")) ? rp[2].Substring(5) : "raphnet";
                            padDevices.Add(new PadDevice
                            {
                                Key = "raphnet" + rchan,
                                Label = rname + "  [raphnet RAW ch" + (rchan + 1) + "]"
                                      + ((rp.Length > 1 && rp[1] == "empty") ? "  (no pad)" : ""),
                                Jidx = rchan,
                                Mapped = true,
                                Raphnet = true,
                            });
                        }
                        // "ge: padmap <idx> a=b0 b=b1 ..." - the device's default layout
                        else if (s.StartsWith("ge: padmap ") && last != null)
                        {
                            foreach (string tokpair in s.Substring(11).Split(' '))
                            {
                                int eq = tokpair.IndexOf('=');
                                if (eq > 0)
                                {
                                    last.Defaults[tokpair.Substring(0, eq)] = tokpair.Substring(eq + 1);
                                }
                            }
                        }
                        // "ge: padsdl <idx> a=b0 leftshoulder=b4 leftx+=a0+ ..." - SDL name -> token
                        else if (s.StartsWith("ge: padsdl ") && last != null)
                        {
                            foreach (string pair in s.Substring(11).Split(' '))
                            {
                                int eq = pair.IndexOf('=');
                                if (eq > 0)
                                {
                                    last.Sdl[pair.Substring(0, eq)] = pair.Substring(eq + 1);
                                }
                            }
                        }
                        // "ge: padnames <idx> b0=A|a4+=LT|..." - token -> button name
                        else if (s.StartsWith("ge: padnames ") && last != null)
                        {
                            foreach (string pair in s.Substring(13).Split('|'))
                            {
                                int eq = pair.IndexOf('=');
                                if (eq > 0)
                                {
                                    last.Names[pair.Substring(0, eq).Trim()] = pair.Substring(eq + 1).Trim();
                                }
                            }
                        }
                    }
                }
                padProbeStatus.Text = padDevices.Count + " device(s) found";
            }
            catch (Exception ex)
            {
                padProbeStatus.Text = "probe failed: " + ex.Message;
            }
        }
        else
        {
            padProbeStatus.Text = "ge.exe not found - device list unavailable";
        }

        FillDeviceCombo();
    }

    int CurPlayer()
    {
        return (playerSel != null && playerSel.SelectedIndex >= 0) ? playerSel.SelectedIndex : 0;
    }

    /* Device combo per selected player. For player 1 the "Keyboard +
     * Mouse" entry is a VIEW of the key binds (keyboard is always live on
     * player 1) - the controller assignment entries below it write
     * pad1..pad4. */
    void FillDeviceCombo()
    {
        int p = CurPlayer();

        suppressDeviceEvent = true;
        deviceSel.Items.Clear();
        if (p == 0)
        {
            deviceSel.Items.Add(new PadChoice { Value = "keyboard", Label = "Keyboard + Mouse" });
        }
        deviceSel.Items.Add(new PadChoice { Value = "auto", Label = "Controller: Auto (connect order)" });
        deviceSel.Items.Add(new PadChoice { Value = "none", Label = "Controller: None" });
        foreach (PadDevice d in padDevices)
        {
            deviceSel.Items.Add(new PadChoice { Value = d.Key, Label = d.Label });
        }

        // select the current assignment (or the keyboard view for P1 when
        // the slot has no pinned device)
        string want = padAssign[p];
        PadChoice pick = null;
        foreach (PadChoice c in deviceSel.Items)
        {
            if (string.Equals(c.Value, want, StringComparison.OrdinalIgnoreCase))
            {
                pick = c;
                break;
            }
        }
        if (pick == null && want != null && want != "auto" && want != "none" && want.Length > 0)
        {
            pick = new PadChoice { Value = want, Label = "(not connected)  " + want };
            deviceSel.Items.Add(pick);
        }
        if (p == 0 && (want == "auto" || pick == null))
        {
            pick = (PadChoice) deviceSel.Items[0]; // keyboard view first for P1
        }
        deviceSel.SelectedItem = (pick != null) ? pick : deviceSel.Items[p == 0 ? 0 : 1];
        suppressDeviceEvent = false;

        RenderRows();
    }

    void OnDeviceSelected()
    {
        if (suppressDeviceEvent)
        {
            return;
        }
        var c = deviceSel.SelectedItem as PadChoice;
        int p = CurPlayer();
        if (c != null && c.Value != "keyboard")
        {
            padAssign[p] = c.Value;
        }
        RenderRows();
    }

    PadDevice FindDevice(string key)
    {
        foreach (PadDevice d in padDevices)
        {
            if (string.Equals(d.Key, key, StringComparison.OrdinalIgnoreCase))
            {
                return d;
            }
        }
        return null;
    }

    static string PrettyToken(string t)
    {
        if (t == null || t.Length == 0 || t == "-") return "(unbound)";
        if (t.IndexOf(',') >= 0)
        {   /* "tok1,tok2": one action on several inputs (controller Add) */
            var parts = new List<string>();
            foreach (string s in t.Split(','))
            {
                if (s.Trim().Length > 0) parts.Add(PrettyToken(s.Trim()));
            }
            return string.Join(", ", parts.ToArray());
        }
        if (t[0] == 'b') return "button " + t.Substring(1);
        if (t[0] == 'a') return "axis " + t.Substring(1, t.Length - 2) + " " + t[t.Length - 1];
        if (t[0] == 'h')
        {
            char d = t[t.Length - 1];
            return "hat " + (d == 'u' ? "up" : d == 'd' ? "down" : d == 'l' ? "left" : "right");
        }
        return t;
    }

    /* Right edge for the bind rows' value box + buttons: pinned to the
     * panel's right side, clear of the vertical scrollbar (murkantor 2026-08-25:
     * the long "Port: ..." labels ran under a value box parked at x=92). */
    int RowRight()
    {
        return bindPan.Width - SystemInformation.VerticalScrollBarWidth - 8;
    }

    /* Rebuild the bind rows for the current player/device selection. */
    void RenderRows()
    {
        bindPan.Controls.Clear();
        var sel = deviceSel.SelectedItem as PadChoice;
        int p = CurPlayer();
        int right = RowRight();

        if (sel == null)
        {
            return;
        }

        if (sel.Value == "keyboard")
        {
            for (int i = 0; i < BindDefs.Length; i++)
            {
                int row = i;
                int ry = 4 + i * 28;
                int xClr = right - 48, xAdd = xClr - 2 - 44, xSet = xAdd - 2 - 44;
                int tbW = 150, xTb = xSet - 4 - tbW;
                bindPan.Controls.Add(new Label { Text = BindDefs[i][1], Location = new Point(4, ry + 4), AutoSize = true });
                var tb = new TextBox { Location = new Point(xTb, ry), Width = tbW, ReadOnly = true, Text = keyBinds[i] };
                tb.KeyDown += delegate (object s, KeyEventArgs e)
                {   /* Delete = unbind (same as Clear) */
                    if (e.KeyCode == Keys.Delete) { keyBinds[row] = ""; RenderRows(); e.Handled = true; }
                };
                bindPan.Controls.Add(tb);
                var bSet = new Button { Text = "Set", Location = new Point(xSet, ry - 1), Width = 44 };
                bSet.Click += delegate { CaptureBind(row, false); RenderRows(); };
                bindPan.Controls.Add(bSet);
                var bAdd = new Button { Text = "Add", Location = new Point(xAdd, ry - 1), Width = 44 };
                bAdd.Click += delegate { CaptureBind(row, true); RenderRows(); };
                bindPan.Controls.Add(bAdd);
                var bClr = new Button { Text = "Clear", Location = new Point(xClr, ry - 1), Width = 48 };
                bClr.Click += delegate { keyBinds[row] = ""; RenderRows(); };
                bindPan.Controls.Add(bClr);
            }
            {
                var bNone = new Button { Text = "Unbind all", Location = new Point(right - 90, 4 + BindDefs.Length * 28 + 4), Width = 90 };
                bNone.Click += delegate
                {
                    for (int k = 0; k < keyBinds.Length; k++) keyBinds[k] = "";
                    RenderRows();
                };
                bindPan.Controls.Add(bNone);
            }
            return;
        }

        if (sel.Value == "auto" || sel.Value == "none")
        {
            bindPan.Controls.Add(new Label
            {
                Text = (sel.Value == "none")
                    ? "No controller for this player."
                    : "Connect-order assignment. Pick a specific device above to rebind its buttons.",
                Location = new Point(8, 10),
                AutoSize = true,
            });
            return;
        }

        var dev = FindDevice(sel.Value);
        if (dev != null && dev.Raphnet)
        {
            /* the RAW path delivers the real N64 pad - the buttons ARE
             * A/B/Z/etc, nothing to rebind; only the stick treatment
             * (raw vs calibrated) is a choice */
            bindPan.Controls.Add(new Label
            {
                Text = "Native N64 controller via the raphnet RAW path.\n"
                     + "Buttons are the real thing - no rebinding needed.\n"
                     + "Stick is 1:1 N64 units; calibrate below only if this\n"
                     + "stick is worn and you want it mapped to a full-range one.",
                Location = new Point(8, 8),
                Size = new Size(430, 76),
            });
            RenderStickControls(56, dev); /* stick controls start below the
                                           * info text (combo at +32) */
            return;
        }
        var cur = padMapEdit[p];
        int ycur = 4;
        string lastGroup = null;
        for (int i = 0; i < PadCtrlDefs.Length; i++)
        {
            int row = i;
            if (PadCtrlDefs[i].Length > 3 && PadCtrlDefs[i][3] != lastGroup)
            {   /* group header */
                lastGroup = PadCtrlDefs[i][3];
                bindPan.Controls.Add(new Label
                {
                    Text = lastGroup,
                    Location = new Point(4, ycur + 6),
                    AutoSize = true,
                    Font = new Font(Font, FontStyle.Bold),
                });
                ycur += 26;
            }
            int ry = ycur;
            ycur += 28;
            string ctrl = PadCtrlDefs[i][0];
            string token = null;
            if (cur != null)
            {
                cur.TryGetValue(ctrl, out token);
            }
            if (token == null && dev != null)
            {
                dev.Defaults.TryGetValue(ctrl, out token);
            }
            int xAddC = right - 44, xSet = xAddC - 2 - 44;
            int tbW = 160, xTb = xSet - 4 - tbW;
            bindPan.Controls.Add(new Label { Text = PadCtrlDefs[i][1], Location = new Point(4, ry + 4), AutoSize = true });
            var tb = new TextBox
            {
                Location = new Point(xTb, ry),
                Width = tbW,
                ReadOnly = true,
                Text = PrettyTokenFor(dev, token) + (cur == null ? "  (default)" : ""),
            };
            tb.KeyDown += delegate (object s, KeyEventArgs e)
            {   /* Delete = unbind this control on this controller */
                if (e.KeyCode == Keys.Delete && dev != null) { UnbindPadCtrl(row, dev); e.Handled = true; }
            };
            bindPan.Controls.Add(tb);
            var bSet = new Button { Text = "Set", Location = new Point(xSet, ry - 1), Width = 44, Enabled = dev != null };
            bSet.Click += delegate { CapturePadCtrl(row, false); };
            bSet.KeyDown += delegate (object s, KeyEventArgs e)
            {
                if (e.KeyCode == Keys.Delete && dev != null) { UnbindPadCtrl(row, dev); e.Handled = true; }
            };
            bindPan.Controls.Add(bSet);
            var bAddC = new Button { Text = "Add", Location = new Point(xAddC, ry - 1), Width = 44, Enabled = dev != null };
            bAddC.Click += delegate { CapturePadCtrl(row, true); }; /* one action, several inputs */
            bindPan.Controls.Add(bAddC);
        }
        int by = ycur + 4;
        var bReset = new Button
        {
            Text = "Reset to built-in map",
            Location = new Point(92, by),
            Width = 160,
        };
        bReset.Click += delegate { padMapEdit[CurPlayer()] = null; RenderRows(); };
        bindPan.Controls.Add(bReset);
        var bUnbindAll = new Button
        {
            Text = "Unbind all",
            Location = new Point(92 + 160 + 8, by),
            Width = 90,
            Enabled = dev != null,
        };
        bUnbindAll.Click += delegate
        {   /* every control "-": bind from scratch. Written as-is; the port
             * keeps an all-"-" map custom (explicitly empty, not the default). */
            var m = new Dictionary<string, string>();
            foreach (string[] cd in PadCtrlDefs) m[cd[0]] = "-";
            padMapEdit[CurPlayer()] = m;
            RenderRows();
        };
        bindPan.Controls.Add(bUnbindAll);

        RenderStickControls(by, dev);
    }

    /* Rumble test + calibration controls, shared by the generic-device and
     * raphnet rows (by = layout offset of the section). The stick MODE is no
     * longer a choice here (murkantor 2026-09-05): the game derives it from the
     * device - raphnet-class adapters raw 1:1, everything else scaled,
     * padN_calib = calibrated. padStickMode only carries "calibrated". */
    void RenderStickControls(int by, PadDevice dev)
    {
        int p = CurPlayer();

        bindPan.Controls.Add(new Label
        {
            Text = (dev != null && (dev.Label.ToLowerInvariant().Contains("raphnet") || dev.Label.ToLowerInvariant().Contains("n64")))
                ? "Stick: raw 1:1 (N64 adapter) - calibrate below for the octagon"
                : "Stick: scaled (generic pad)",
            Location = new Point(92, by + 36),
            AutoSize = true,
            ForeColor = Color.DimGray,
        });
        var bBuzz = new Button { Text = "Test rumble", Location = new Point(RowRight() - 84, by + 31), Width = 84, Enabled = dev != null };
        bBuzz.Click += delegate
        {
            var d2 = dev;
            if (d2 == null) return;
            try
            {
                var psi = new ProcessStartInfo(GELauncher.FindGe(exeDir))
                {
                    WorkingDirectory = exeDir,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                psi.EnvironmentVariables["GE_RUMBLETEST"] =
                    d2.Raphnet ? ("r" + d2.Jidx) : d2.Jidx.ToString();
                if (dinputPads != null && dinputPads.Checked) psi.EnvironmentVariables["GE_DINPUT"] = "1";
                Process.Start(psi);
                bBuzz.Enabled = false;
                bBuzz.Text = "Buzzing...";
                var t = new Timer { Interval = 3500 };
                t.Tick += delegate { t.Stop(); bBuzz.Text = "Test rumble"; bBuzz.Enabled = true; };
                t.Start();
            }
            catch { }
        };
        bindPan.Controls.Add(bBuzz);

        var cbRumble = new CheckBox
        {
            Text = "Rumble Pak inserted (in-game rumble for this player)",
            Location = new Point(92, by + 66),
            AutoSize = true,
            Checked = padRumbleOn[p],
        };
        cbRumble.CheckedChanged += delegate { padRumbleOn[CurPlayer()] = cbRumble.Checked; };
        bindPan.Controls.Add(cbRumble);

        var cbSwap = new CheckBox
        {
            Text = "Swap sticks (this player looks with the left stick, moves with the right)",
            Location = new Point(92, by + 96),
            AutoSize = true,
            Checked = padSwapSticks[p],
        };
        tip.SetToolTip(cbSwap,
            "Trades this player's Move and Look sticks over, for a pad or a control " +
            "style whose sticks come out the wrong way round.\n\n" +
            "Everything that names a stick follows: movement, aiming, the menu and " +
            "watch cursor, and the C buttons the Look stick stands in for.\n\n" +
            "A one-stick pad (a real N64 controller, an N64-shaped pad) has nothing " +
            "to trade and is left alone. Player 1's is also on the in-game Extended " +
            "Options page, under controller.");
        cbSwap.CheckedChanged += delegate { padSwapSticks[CurPlayer()] = cbSwap.Checked; };
        bindPan.Controls.Add(cbSwap);

        var cbGadget = new CheckBox
        {
            Text = "Gadget switch: Z while holding B = next gadget (for a real N64 pad)",
            Location = new Point(92, by + 122),
            AutoSize = true,
            Checked = padGadgetSwitch[p],
        };
        tip.SetToolTip(cbGadget,
            "A real N64 controller has no spare button for the Port 'Next gadget' action. " +
            "With this on, pressing Z while B is held switches to the next gadget instead of " +
            "firing; that Z stays swallowed until you let go of it.\n\n" +
            "Z first, then B, is still fire + use. B's own use/reload happens as normal.");
        cbGadget.CheckedChanged += delegate { padGadgetSwitch[CurPlayer()] = cbGadget.Checked; };
        bindPan.Controls.Add(cbGadget);

        bool canCalib = dev != null
            && (dev.Label.ToLowerInvariant().Contains("raphnet")
                || dev.Label.ToLowerInvariant().Contains("n64"));
        if (canCalib || padStickMode[p] == "calibrated")
        {
            var bCal = new Button
            {
                Text = (padCalib[p] != null) ? "Recalibrate stick..." : "Calibrate stick...",
                Location = new Point(92, by + 152),
                Width = 160,
            };
            bCal.Click += delegate
            {
                RunCalibration();
                if (padCalib[CurPlayer()] != null)
                {
                    padStickMode[CurPlayer()] = "calibrated";
                }
                RenderRows();
            };
            bindPan.Controls.Add(bCal);
            if (padCalib[p] != null)
            {
                var bCalClr = new Button
                {
                    Text = "Clear calibration",
                    Location = new Point(258, by + 152),
                    Width = 130,
                };
                bCalClr.Click += delegate
                {
                    int pp = CurPlayer();
                    padCalib[pp] = null;
                    if (padStickMode[pp] == "calibrated")
                    {
                        padStickMode[pp] = null;
                    }
                    RenderRows();
                };
                bindPan.Controls.Add(bCalClr);
            }
            bindPan.Controls.Add(new Label
            {
                Text = (padCalib[p] != null) ? "Stick calibrated for this player." : "Stick not calibrated.",
                Location = new Point(92, by + 182),
                AutoSize = true,
                ForeColor = Color.DimGray,
            });
        }
    }

    /* The 8-step calibration wizard: drives ge.exe GE_CALIBRATE=<jidx>,
     * shows its step prompts, stores the resulting 16-int csv. */
    void RunCalibration()
    {
        int p = CurPlayer();
        var sel = deviceSel.SelectedItem as PadChoice;
        var dev = (sel != null) ? FindDevice(sel.Value) : null;
        if (dev == null)
        {
            return;
        }

        string exe = GELauncher.FindGe(exeDir);
        string result = null;
        Process proc = null;

        using (var wiz = new Form
        {
            Text = "Stick calibration - " + dev.Label,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false,
            MinimizeBox = false,
            StartPosition = FormStartPosition.CenterParent,
            ClientSize = new Size(420, 130),
        })
        {
            var prompt = new Label
            {
                Text = "Starting...",
                Location = new Point(12, 12),
                Size = new Size(396, 70),
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font(FontFamily.GenericSansSerif, 11f),
            };
            wiz.Controls.Add(prompt);
            var bCancel = new Button { Text = "Cancel", Location = new Point(160, 92), Width = 100 };
            bCancel.Click += delegate { wiz.DialogResult = DialogResult.Cancel; };
            wiz.Controls.Add(bCancel);

            wiz.Shown += delegate
            {
                try
                {
                    var psi = new ProcessStartInfo(exe)
                    {
                        WorkingDirectory = exeDir,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        CreateNoWindow = true,
                    };
                    psi.EnvironmentVariables["GE_CALIBRATE"] =
                        dev.Raphnet ? ("r" + dev.Jidx) : dev.Jidx.ToString();
                    if (dinputPads != null && dinputPads.Checked) psi.EnvironmentVariables["GE_DINPUT"] = "1";
                    proc = Process.Start(psi);
                    proc.OutputDataReceived += delegate (object s, DataReceivedEventArgs ev)
                    {
                        if (ev.Data == null)
                        {
                            return;
                        }
                        string line = ev.Data.Trim();
                        if (line.StartsWith("ge: calibrate step "))
                        {
                            string dir = line.Substring(line.LastIndexOf(' ') + 1);
                            int stepn = 0;
                            int.TryParse(line.Substring(19, 1), out stepn);
                            string txt = "Step " + (stepn + 1) + " of 8:\nHold the stick fully "
                                       + dir + "\nand press A";
                            try { wiz.BeginInvoke((Action) delegate { prompt.Text = txt; }); }
                            catch { }
                        }
                        else if (line.StartsWith("ge: calibrate done "))
                        {
                            result = line.Substring(19).Trim();
                            try { wiz.BeginInvoke((Action) delegate { wiz.DialogResult = DialogResult.OK; }); }
                            catch { }
                        }
                        else if (line.StartsWith("ge: calibrate none"))
                        {
                            try { wiz.BeginInvoke((Action) delegate { wiz.DialogResult = DialogResult.Cancel; }); }
                            catch { }
                        }
                    };
                    proc.BeginOutputReadLine();
                }
                catch
                {
                    wiz.DialogResult = DialogResult.Cancel;
                }
            };

            wiz.ShowDialog(this);
            try
            {
                if (proc != null && !proc.HasExited)
                {
                    proc.Kill();
                }
            }
            catch { }
        }

        if (result != null && result.Split(',').Length == 16)
        {
            padCalib[p] = result;
            using (var rf = new CalibResultForm(result))
            {
                rf.ShowDialog(this);
            }
        }
        RenderRows();
    }

    /* Rebind one controller control: run ge.exe GE_CAPTUREPAD=<jidx> and
     * wait for the press. Seeds the full custom map from the device's
     * defaults on first edit so every other control stays bound. */
    /* append = Add: keep the row's existing inputs and add this one (up to 4,
     * "tok1,tok2" - the port ORs them); Set replaces. */
    void CapturePadCtrl(int row, bool append)
    {
        int p = CurPlayer();
        var sel = deviceSel.SelectedItem as PadChoice;
        var dev = (sel != null) ? FindDevice(sel.Value) : null;
        if (dev == null)
        {
            return;
        }

        string exe = GELauncher.FindGe(exeDir);
        string token = null;
        bool unbind = false;
        Process proc = null;
        using (var wait = new Form
        {
            Text = "Bind " + PadCtrlDefs[row][1],
            FormBorderStyle = FormBorderStyle.FixedDialog,
            ControlBox = false,
            StartPosition = FormStartPosition.CenterParent,
            ClientSize = new Size(340, 82),
            KeyPreview = true,
        })
        {
            wait.Controls.Add(new Label
            {
                Text = "On '" + dev.Label + "':\n" + PadCtrlDefs[row][2] + "\n(Delete = unbind, Esc = cancel, 10s timeout)",
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
            });
            wait.KeyDown += delegate (object s, KeyEventArgs e)
            {   /* Delete unbinds the row, Esc cancels: kill the capture run */
                if (e.KeyCode == Keys.Delete || e.KeyCode == Keys.Escape)
                {
                    unbind = e.KeyCode == Keys.Delete;
                    try { if (proc != null && !proc.HasExited) proc.Kill(); } catch { }
                    wait.DialogResult = DialogResult.Cancel;
                    e.Handled = true;
                }
            };
            wait.Shown += delegate
            {
                try
                {
                    var psi = new ProcessStartInfo(exe)
                    {
                        WorkingDirectory = exeDir,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        CreateNoWindow = true,
                    };
                    psi.EnvironmentVariables["GE_CAPTUREPAD"] = dev.Jidx.ToString();
                    if (dinputPads != null && dinputPads.Checked) psi.EnvironmentVariables["GE_DINPUT"] = "1";
                    proc = Process.Start(psi);
                    var t = new System.Threading.Thread(delegate ()
                    {
                        string outp = proc.StandardOutput.ReadToEnd();
                        proc.WaitForExit();
                        foreach (string raw in outp.Split('\n'))
                        {
                            string s = raw.Trim();
                            if (s.StartsWith("ge: capture "))
                            {
                                string tk = s.Substring(12).Trim();
                                if (tk != "none")
                                {
                                    token = tk;
                                }
                            }
                        }
                        try { wait.BeginInvoke((Action) delegate { wait.DialogResult = DialogResult.OK; }); }
                        catch { }
                    });
                    t.IsBackground = true;
                    t.Start();
                }
                catch
                {
                    wait.DialogResult = DialogResult.Cancel;
                }
            };
            wait.ShowDialog(this);
        }

        if (unbind)
        {
            UnbindPadCtrl(row, dev);
            return;
        }
        if (token == null)
        {
            return;
        }
        if (padMapEdit[p] == null)
        {
            padMapEdit[p] = new Dictionary<string, string>(dev.Defaults);
        }
        if (append)
        {
            string old;
            var parts = new List<string>();
            if (padMapEdit[p].TryGetValue(PadCtrlDefs[row][0], out old) && old != null)
            {
                foreach (string s in old.Split(','))
                {
                    string t = s.Trim();
                    if (t.Length > 0 && t != "-") parts.Add(t);
                }
            }
            if (!parts.Contains(token)) parts.Add(token);
            while (parts.Count > 4) parts.RemoveAt(0); /* the port keeps 1 primary + 3 alternates */
            token = string.Join(",", parts.ToArray());
        }
        padMapEdit[p][PadCtrlDefs[row][0]] = token;
        RenderRows();
    }

    /* Delete key on a row: unbind that control on this controller. Stored
     * and written as "-" (the port parses "-" as unset = unbound), so a
     * reload shows "(unbound)" rather than falling back to the default. */
    void UnbindPadCtrl(int row, PadDevice dev)
    {
        int p = CurPlayer();
        if (padMapEdit[p] == null)
        {
            padMapEdit[p] = new Dictionary<string, string>(dev.Defaults);
        }
        padMapEdit[p][PadCtrlDefs[row][0]] = "-";
        RenderRows();
    }

    // ---- small layout helpers ----
    NumericUpDown AddNum(TabPage p, string label, ref int y, decimal min, decimal max, int decimals, decimal inc, string hint = null)
    {
        var lbl = new Label { Text = label, Location = new Point(12, y + 3), AutoSize = true };
        p.Controls.Add(lbl);
        var n = new NumericUpDown
        {
            Location = new Point(230, y),
            Width = 110,
            Minimum = min,
            Maximum = max,
            DecimalPlaces = decimals,
            Increment = inc,
        };
        p.Controls.Add(n);
        if (hint != null)
        {
            // Both, so hovering anywhere on the row explains it.
            tip.SetToolTip(lbl, hint);
            tip.SetToolTip(n, hint);
        }
        y += 30;
        return n;
    }

    // A colour swatch that opens the system picker. The swatch's BackColor IS
    // the stored value - no separate field to keep in step with it.
    Button AddColor(TabPage p, string label, ref int y, string hint = null)
    {
        var lbl = new Label { Text = label, Location = new Point(12, y + 3), AutoSize = true };
        p.Controls.Add(lbl);
        var b = new Button
        {
            Location = new Point(230, y),
            Width = 110,
            Height = 23,
            FlatStyle = FlatStyle.Flat,
            Text = "",
        };
        b.Click += (s2, e2) =>
        {
            using (var dlg = new ColorDialog { Color = b.BackColor, FullOpen = true })
            {
                if (dlg.ShowDialog() == DialogResult.OK)
                {
                    b.BackColor = dlg.Color;
                }
            }
        };
        p.Controls.Add(b);
        if (hint != null)
        {
            tip.SetToolTip(lbl, hint);
            tip.SetToolTip(b, hint);
        }
        y += 30;
        return b;
    }

    CheckBox AddCheck(TabPage p, string label, ref int y)
    {
        var c = new CheckBox { Text = label, Location = new Point(12, y + 1), AutoSize = true };
        p.Controls.Add(c);
        y += 30;
        return c;
    }

    ComboBox AddCombo(TabPage p, string label, ref int y, string[] items)
    {
        p.Controls.Add(new Label { Text = label, Location = new Point(12, y + 3), AutoSize = true });
        var c = new ComboBox
        {
            Location = new Point(230, y),
            Width = 110,
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        c.Items.AddRange(items);
        c.SelectedIndex = 0;
        p.Controls.Add(c);
        y += 30;
        return c;
    }

    // ---- defaults / load / save ----
    void ApplyDefaults()
    {
        winW.Value = 640; winH.Value = 480;
        displayMode.SelectedIndex = 1;   /* fullscreen (borderless) is the default now */
        widescreen.Checked = true;
        msaa.SelectedIndex = 0;              // off
        filtering.SelectedIndex = 3;   // trilinear (menu overhaul Q1)
        anisotropy.SelectedIndex = 0;        // off
        filter2d.Checked = false;  /* new default (menu overhaul 2.1) */
        highLod.Checked = true;    /* the game default is 1; the spec says always ON */
        lowRes.SelectedIndex = LowResNativeIndex;   // native (the window's own)
        hwMipmaps.Checked = true;
        n64Aa.Checked = false;
        roomCache.Checked = false;
        triHoist.Checked = true;
        showFps.Checked = false;
        alwaysWalk.Checked = false;
        detonateUseReload.Checked = true;
        fov.Value = 60;
        shadowAlpha.Value = 65;
        musicVol.Value = 100;
        sfxVol.Value = 100;
        maxfps.Value = 120;
        vsync.Checked = true;
        n64Speed.Checked = false;
        adzy.Checked = false;
        unlockAll.Checked = false;
        pausePillars.Checked = true;
        alwaysCrosshair.Checked = false;
        aimLock.Checked = false;
        crosshairCustom.Checked = false;
        crosshairColor.BackColor = Color.FromArgb(0xF5, 0x00, 0x00); // the sight's own red
        noHeadRoll.Checked = false;
        mouse.Checked = true;
        smartMouseLock.Checked = false; // the game default is 0 since the menu overhaul (input_pc.c s_OptSmartLock); F1 is the grab toggle
        sensX.Value = 2.5m; sensY.Value = 2.5m; sensAim.Value = 1.0m;
        dinputPads.Checked = false;
        deadzonePct.Value = 12; // ~4096 of 32768, the port default
        trigThresh.Value = 3276; // 10% of the pull (murkantor 2026-08-27)
        stickScale.Value = 1.0m;
        pads.Value = 4;
        showInput.Checked = false;
        for (int i = 0; i < BindDefs.Length; i++)
        {
            keyBinds[i] = BindDefs[i][2];
        }
        for (int i = 0; i < 4; i++)
        {
            padAssign[i] = "auto";
            padMapEdit[i] = null;
            padCalib[i] = null;
            padStickMode[i] = null;
            padRumbleOn[i] = true;
            padSwapSticks[i] = false;
            padGadgetSwitch[i] = false;
        }
        if (deviceSel != null)
        {
            FillDeviceCombo();
        }
    }

    static decimal ClampDec(decimal v, NumericUpDown n)
    {
        if (v < n.Minimum) return n.Minimum;
        if (v > n.Maximum) return n.Maximum;
        return v;
    }

    /* SDL_atoi's leading integer ("480p" = 480, "off" = 0), for ini values
       the game reads with SDL_atoi rather than a strict parse. */
    static long IniAtoi(string v)
    {
        int end = (v.Length > 0 && (v[0] == '+' || v[0] == '-')) ? 1 : 0;
        while (end < v.Length && v[end] >= '0' && v[end] <= '9') end++;
        long n;
        return long.TryParse(v.Substring(0, end), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out n) ? n : 0;
    }

    /* midas.ini low_res text -> lowRes index, read EXACTLY as the game's
       videoLowResLines (port/src/video.c) reads it, so a Save writes back the
       value the game was really using: "hw" = Hardware, "off" = Native, else
       SDL_atoi's leading integer ("480p" = 480): <= 0 Native, 1..240 Hardware,
       then snapped UP to 480 / 720 / 1080. Keep in step with videoLowResLines
       and LowResValues. */
    static int LowResIndexFromIni(string v)
    {
        if (string.Equals(v, "hw", StringComparison.OrdinalIgnoreCase)) return 0;
        if (string.Equals(v, "off", StringComparison.OrdinalIgnoreCase)) return LowResNativeIndex;
        long n = IniAtoi(v);
        if (n <= 0) return LowResNativeIndex;
        if (n <= 240) return 0;
        if (n <= 480) return 1;
        if (n <= 720) return 2;
        return 3;
    }

    void LoadIni()
    {
        if (!File.Exists(iniPath))
        {
            return; // fresh install: dialog shows the defaults
        }
        var opts = new Dictionary<string, string>();
        var known = KnownIniKeys();
        string section = "";
        iniExtra.Clear();
        foreach (string raw in File.ReadAllLines(iniPath))
        {
            string s = raw.Trim();
            if (s.Length == 0 || s[0] == ';' || s[0] == '#')
            {
                continue;
            }
            if (s[0] == '[')
            {
                section = s.ToLowerInvariant();
                continue;
            }
            int eq = s.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }
            string key = s.Substring(0, eq).Trim().ToLowerInvariant();
            /* hw_mipmaps is mipmaps' pre-overhaul name: one slot for both, so
               whichever line is LAST wins, exactly as the game parses */
            if (key == "hw_mipmaps") key = "mipmaps";
            opts[key] = s.Substring(eq + 1).Trim();
            if (!known.Contains(key))
            {   /* a key this dialog has no control for (set in game by the
                 * PORT OPTIONS page, or by hand): carried through SaveIni
                 * verbatim in its section instead of being dropped */
                List<string> extra;
                if (!iniExtra.TryGetValue(section, out extra))
                {
                    extra = new List<string>();
                    iniExtra[section] = extra;
                }
                /* one line per key: a repeated key keeps its LAST value (the
                 * game parses last-wins too). low_res was missing from the
                 * known set until 2026-09-06 and grew one duplicate line per
                 * save; this also collapses those on the next load. */
                string dupPrefix = key + "=";
                extra.RemoveAll(l => l.StartsWith(dupPrefix, StringComparison.Ordinal));
                extra.Add(key + "=" + opts[key]);
            }
        }

        string v;
        double d;
        int n;
        if (opts.TryGetValue("window_width", out v) && int.TryParse(v, out n)) winW.Value = ClampDec(n, winW);
        if (opts.TryGetValue("window_height", out v) && int.TryParse(v, out n)) winH.Value = ClampDec(n, winH);
        if (opts.TryGetValue("window_x", out v) && int.TryParse(v, out n)) iniWindowX = n.ToString();
        if (opts.TryGetValue("window_y", out v) && int.TryParse(v, out n)) iniWindowY = n.ToString();
        if (opts.TryGetValue("window_display", out v) && int.TryParse(v, out n)) iniWindowDisplay = n.ToString();
        if (opts.TryGetValue("fullscreen", out v))
        {
            int di = Array.IndexOf(DisplayModeValues, v.Trim().ToLowerInvariant());
            if (di < 0 && v.Trim().ToLowerInvariant() == "borderless") di = 1;
            if (di < 0 && int.TryParse(v, out n)) di = n <= 0 ? 0 : (n >= 2 ? 2 : 1);
            if (di >= 0) displayMode.SelectedIndex = di;
        }
        if (opts.TryGetValue("widescreen", out v) && int.TryParse(v, out n)) widescreen.Checked = n != 0;
        if (opts.TryGetValue("msaa", out v) && int.TryParse(v, out n)) msaa.SelectedIndex = Math.Max(0, Array.IndexOf(MsaaValues, n));
        if (opts.TryGetValue("filtering", out v))
        {   /* read as the game's videoFilteringIndex does: the two long spellings
               of 3point, and ANY other text is bilinear (not point) */
            string fv = v.ToLowerInvariant();
            if (fv == "threepoint" || fv == "three-point") fv = "3point";
            int fi = Array.IndexOf(FilterValues, fv);
            filtering.SelectedIndex = fi >= 0 ? fi : 1;
        }
        if (opts.TryGetValue("anisotropy", out v) && int.TryParse(v, out n)) anisotropy.SelectedIndex = Math.Max(0, Array.IndexOf(AnisoValues, n));
        if (opts.TryGetValue("filter_2d", out v) && int.TryParse(v, out n)) filter2d.Checked = n != 0;
        if (opts.TryGetValue("high_lod", out v) && int.TryParse(v, out n)) highLod.Checked = n != 0;
        if (opts.TryGetValue("low_res", out v)) lowRes.SelectedIndex = LowResIndexFromIni(v);
        /* "mipmaps" is the key since the menu overhaul; a 2026-09-06 "hw_mipmaps"
           line was folded into it above, last line winning like the game. */
        if (opts.TryGetValue("mipmaps", out v)) hwMipmaps.Checked = IniAtoi(v) != 0; /* SDL_atoi != 0, as video.c reads it: a raw "on"/"off" from the old console path is 0 */
        if (opts.TryGetValue("n64_aa", out v) && int.TryParse(v, out n)) n64Aa.Checked = n != 0;
        if (opts.TryGetValue("room_cache", out v) && int.TryParse(v, out n)) roomCache.Checked = n == 2; /* 1 = the CPU replay, never offered here */
        if (opts.TryGetValue("tri_hoist", out v) && int.TryParse(v, out n)) triHoist.Checked = n != 0;
        if (opts.TryGetValue("show_fps", out v) && int.TryParse(v, out n)) showFps.Checked = n != 0;
        if (opts.TryGetValue("fov", out v) && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) fov.Value = ClampDec((decimal) d, fov);
        if (opts.TryGetValue("shadow_alpha", out v) && int.TryParse(v, out n)) shadowAlpha.Value = ClampDec(n, shadowAlpha);
        /* -1 = the game's Max FPS "Hardware" (and Classic Mode's): N64 speed
           here, not a clamp to 0 = uncapped (G025) */
        if (opts.TryGetValue("maxfps", out v) && int.TryParse(v, out n))
        {
            if (n == -1) n64Speed.Checked = true;
            else maxfps.Value = ClampDec(n, maxfps);
        }
        if (opts.TryGetValue("vsync", out v) && int.TryParse(v, out n)) vsync.Checked = n != 0;
        /* tickhz is DERIVED from maxfps now; the only value still worth
           reading back is "hw", the console's own pacing. */
        if (opts.TryGetValue("tickhz", out v))
        {
            if (v.Trim().ToLowerInvariant() == "hw" || v.Trim() == "-1") n64Speed.Checked = true;
        }
        if (opts.TryGetValue("adzy", out v) && int.TryParse(v, out n)) adzy.Checked = n != 0;
        if (opts.TryGetValue("unlock_all", out v) && int.TryParse(v, out n)) unlockAll.Checked = n != 0;
        if (opts.TryGetValue("pause_pillars", out v) && int.TryParse(v, out n)) pausePillars.Checked = n != 0;
        if (opts.TryGetValue("always_crosshair", out v) && int.TryParse(v, out n)) alwaysCrosshair.Checked = n != 0;
        if (opts.TryGetValue("aim_lock", out v) && int.TryParse(v, out n)) aimLock.Checked = n != 0;
        if (opts.TryGetValue("no_head_roll", out v) && int.TryParse(v, out n)) noHeadRoll.Checked = n != 0;
        if (opts.TryGetValue("crosshair_color", out v))
        {
            int rgb;
            string hex = (v ?? "").Trim().TrimStart('#');
            if (hex.Length == 6 && int.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out rgb))
            {
                crosshairCustom.Checked = true;
                crosshairColor.BackColor = Color.FromArgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            }
            else
            {
                crosshairCustom.Checked = false; // "default", blank, or malformed
            }
        }
        if (opts.TryGetValue("music_volume", out v) && int.TryParse(v, out n)) musicVol.Value = ClampDec(n, musicVol);
        if (opts.TryGetValue("sfx_volume", out v) && int.TryParse(v, out n)) sfxVol.Value = ClampDec(n, sfxVol);
        if (opts.TryGetValue("mouse", out v) && int.TryParse(v, out n)) mouse.Checked = n != 0;
        if (opts.TryGetValue("smart_mouse_lock", out v) && int.TryParse(v, out n)) smartMouseLock.Checked = n != 0;
        if (opts.TryGetValue("always_walk", out v) && int.TryParse(v, out n)) alwaysWalk.Checked = n != 0;
        if (opts.TryGetValue("detonate_use_reload", out v) && int.TryParse(v, out n)) detonateUseReload.Checked = n != 0;
        if (opts.TryGetValue("mouse_sens_x", out v) && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) sensX.Value = ClampDec((decimal) d, sensX);
        if (opts.TryGetValue("mouse_sens_y", out v) && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) sensY.Value = ClampDec((decimal) d, sensY);
        if (opts.TryGetValue("mouse_sens_aim", out v) && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) sensAim.Value = ClampDec((decimal) d, sensAim);
        if (opts.TryGetValue("dinput", out v) && int.TryParse(v, out n)) dinputPads.Checked = n != 0;
        if (opts.TryGetValue("deadzone", out v) && int.TryParse(v, out n))
        {
            int pct = (int) Math.Round(n * 100.0 / 32768.0);
            deadzonePct.Value = Math.Max(0, Math.Min(100, pct));
        }
        if (opts.TryGetValue("trigger_threshold", out v) && int.TryParse(v, out n)) trigThresh.Value = ClampDec(n, trigThresh);
        if (opts.TryGetValue("stick_scale", out v) && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) stickScale.Value = ClampDec((decimal) d, stickScale);
        if (opts.TryGetValue("pads", out v) && int.TryParse(v, out n)) pads.Value = ClampDec(n, pads);
        if (opts.TryGetValue("show_input", out v) && int.TryParse(v, out n)) showInput.Checked = n != 0;
        for (int i = 0; i < 4; i++)
        {
            if (opts.TryGetValue("pad" + (i + 1), out v) && v.Length > 0)
            {
                padAssign[i] = v;
            }
            foreach (string[] cd in PadCtrlDefs)
            {
                if (opts.TryGetValue("pad" + (i + 1) + "_" + cd[0], out v) && v.Length > 0)
                {
                    if (padMapEdit[i] == null)
                    {
                        padMapEdit[i] = new Dictionary<string, string>();
                    }
                    padMapEdit[i][cd[0]] = v;
                }
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_calib", out v) && v.Split(',').Length == 16)
            {
                padCalib[i] = v;
                if (padStickMode[i] == null)
                {
                    padStickMode[i] = "calibrated"; // matches the port's implied default
                }
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_rumble", out v))
            {
                int rn;
                if (int.TryParse(v, out rn))
                {
                    padRumbleOn[i] = rn != 0;
                }
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_swap_sticks", out v))
            {
                int sn;
                if (int.TryParse(v, out sn))
                {
                    padSwapSticks[i] = sn != 0;
                }
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_layout", out v))
            {
                padLayoutName[i] = v;
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_gadget_switch", out v))
            {
                int gn;
                if (int.TryParse(v, out gn))
                {
                    padGadgetSwitch[i] = gn != 0;
                }
            }
            if (opts.TryGetValue("pad" + (i + 1) + "_stickmode", out v) && v.Length > 0)
            {
                // raw / scaled are not choices any more (the game derives them); only calibrated survives
                padStickMode[i] = v.ToLowerInvariant() == "calibrated" ? "calibrated" : null;
            }
        }

        for (int i = 0; i < BindDefs.Length; i++)
        {
            if (opts.TryGetValue(BindDefs[i][0], out v))
            {
                keyBinds[i] = v;
            }
        }
    }

    // ---- keys this dialog does NOT model, kept across Save (section -> "key=value" lines) ----
    // The port's in-game PORT OPTIONS page writes midas.ini in place; this dialog
    // regenerates the whole file, so anything it has no control for must be
    // carried through or a launcher Save would silently undo it.
    readonly Dictionary<string, List<string>> iniExtra = new Dictionary<string, List<string>>();

    HashSet<string> KnownIniKeys()
    {
        var k = new HashSet<string>(new[] {
            "window_width", "window_height", "window_x", "window_y", "window_display",
            "fullscreen", "widescreen", "msaa", "filtering", "anisotropy", "filter_2d",
            "high_lod", "low_res", "hw_mipmaps", "mipmaps", "n64_aa", "room_cache", "tri_hoist", "show_fps", "fov", "crosshair_color", "music_volume", "sfx_volume", "maxfps", "vsync", "tickhz", "adzy",
            "unlock_all", "pause_pillars", "always_crosshair", "aim_lock", "no_head_roll", "shadow_alpha",
            "mouse", "smart_mouse_lock", "always_walk", "detonate_use_reload", "mouse_sens_x", "mouse_sens_y", "mouse_sens_aim",
            "right_stick_c", "twin_stick", "dinput", "deadzone", "trigger_threshold",
            "stick_scale", "pads", "show_input",
        });
        for (int i = 0; i < 4; i++)
        {
            string p = "pad" + (i + 1);
            k.Add(p);
            k.Add(p + "_calib");
            k.Add(p + "_rumble");
            k.Add(p + "_swap_sticks");
            k.Add(p + "_layout");
            k.Add(p + "_gadget_switch");
            k.Add(p + "_stickmode");
            foreach (string[] cd in PadCtrlDefs)
            {
                k.Add(p + "_" + cd[0]);
            }
        }
        for (int i = 0; i < BindDefs.Length; i++)
        {
            k.Add(BindDefs[i][0].ToLowerInvariant());
        }
        return k;
    }

    void AppendIniExtras(StringBuilder sb, string section)
    {
        List<string> extra;
        if (iniExtra.TryGetValue(section, out extra) && extra.Count > 0)
        {
            sb.AppendLine("; kept as found in midas.ini (in-game PORT OPTIONS / hand edits):");
            foreach (string line in extra)
            {
                sb.AppendLine(line);
            }
        }
    }

    bool SaveIni()
    {
        var sb = new StringBuilder();
        sb.AppendLine("; GoldenEye PC port configuration");
        sb.AppendLine(";");
        sb.AppendLine("; [controls]: N64 control = up to 4 comma-separated key names, as");
        sb.AppendLine("; SDL spells them (\"W\", \"Up\", \"Space\", \"Return\", \"Keypad Enter\",");
        sb.AppendLine("; \"Left Shift\", ...), plus \"Mouse Left\", \"Mouse Right\",");
        sb.AppendLine("; \"Mouse Middle\", \"Mouse X1\", \"Mouse X2\". Empty value = unbound.");
        sb.AppendLine("; Names refer to physical key positions (scancodes), so WASD is the");
        sb.AppendLine("; same cluster on any layout.");
        sb.AppendLine("; analog_* are the analog stick at full deflection.");
        sb.AppendLine("[controls]");
        for (int i = 0; i < BindDefs.Length; i++)
        {
            if (i == PortBindStart)
            {
                sb.AppendLine("; Port suite - actions beyond the N64 pad (joy_port.c)");
            }
            sb.AppendLine(BindDefs[i][0] + "=" + keyBinds[i].Trim());
        }
        AppendIniExtras(sb, "[controls]");
        sb.AppendLine();
        sb.AppendLine("; [input]: controllers + mouse.");
        sb.AppendLine("; Controllers map to N64 pads 1-4 in connect order. Fixed layout:");
        sb.AppendLine(";   A/X=A  B/Y=B  Start=Start  LB=L  RB=R  RT=Z(fire)  LT=R(aim)");
        sb.AppendLine(";   dpad=dpad  left stick=stick  right stick=C buttons");
        sb.AppendLine("; For native dual-analog choose control style 2.x in-game (uses two");
        sb.AppendLine("; N64 pads - the port feeds them from one controller each).");
        sb.AppendLine("; Mouse: click in the window to capture, F1 to release (F1 also grabs it back).");
        sb.AppendLine("[input]");
        sb.AppendLine("; smart_mouse_lock: 1 = capture the pointer only while controlling Bond");
        sb.AppendLine(";   (menus, watch, pause, cutscenes hand it back). 0 (default) = click to capture, F1 to release.");
        sb.AppendLine("mouse=" + (mouse.Checked ? "1" : "0"));
        sb.AppendLine("smart_mouse_lock=" + (smartMouseLock.Checked ? "1" : "0"));
        sb.AppendLine("always_walk=" + (alwaysWalk.Checked ? "1" : "0"));
        sb.AppendLine("; detonate_use_reload=1: with remote mines in hand, Use + Reload together detonate them (the N64 A+B chord).");
        sb.AppendLine("detonate_use_reload=" + (detonateUseReload.Checked ? "1" : "0"));
        sb.AppendLine("mouse_sens_x=" + sensX.Value.ToString("0.##", CultureInfo.InvariantCulture));
        sb.AppendLine("mouse_sens_y=" + sensY.Value.ToString("0.##", CultureInfo.InvariantCulture));
        sb.AppendLine("mouse_sens_aim=" + sensAim.Value.ToString("0.##", CultureInfo.InvariantCulture));
        sb.AppendLine("; dinput=1: SDL's DirectInput backend (8BitDo DInput mode, N64 USB adapters). Off if launching via Steam: its input hook deadlocks DirectInput at window creation.");
        sb.AppendLine("dinput=" + (dinputPads.Checked ? "1" : "0"));
        sb.AppendLine("deadzone=" + (deadzonePct.Value * 32768 / 100));
        sb.AppendLine("trigger_threshold=" + (int) trigThresh.Value);
        sb.AppendLine("stick_scale=" + stickScale.Value.ToString("0.##", CultureInfo.InvariantCulture));
        sb.AppendLine("pads=" + (int) pads.Value);
        sb.AppendLine("; rumble is per player: padN_rumble below (Controls tab checkbox).");
        sb.AppendLine("; padN_swap_sticks=1: that player's Move and Look sticks trade places - left stick looks, right stick moves (Controls tab checkbox).");
        sb.AppendLine("; padN_gadget_switch=1: on that pad, Z pressed while B is held = next gadget, shot swallowed (real N64 pads; Controls tab checkbox).");
        sb.AppendLine("; show_input=1: on-screen live stick X/Y readout (N64 units).");
        sb.AppendLine("show_input=" + (showInput.Checked ? "1" : "0"));
        sb.AppendLine("; pad1..pad4: which physical controller drives each N64 pad slot.");
        sb.AppendLine("; auto = connect order, none = leave the slot free, or an SDL");
        sb.AppendLine("; joystick GUID (optionally #n for the n-th identical device) -");
        sb.AppendLine("; the GE Launcher options dialog fills these in by name.");
        sb.AppendLine("; Unmapped (DInput) sticks get a fixed fallback layout: axes 0/1 =");
        sb.AppendLine("; stick, 2/3 = C, hat = dpad, buttons 0-9 = A B Z Start L R C-u/d/l/r;");
        sb.AppendLine("; add the device to gamecontrollerdb.txt for a full remap.");
        for (int i = 0; i < 4; i++)
        {
            sb.AppendLine("pad" + (i + 1) + "=" + padAssign[i]);
        }
        for (int i = 0; i < 4; i++)
        {
            if (padMapEdit[i] != null)
            {
                foreach (string[] cd in PadCtrlDefs)
                {
                    string tok;
                    if (padMapEdit[i].TryGetValue(cd[0], out tok) && tok != null && tok.Length > 0)
                    {
                        sb.AppendLine("pad" + (i + 1) + "_" + cd[0] + "=" + tok);
                    }
                }
            }
            if (padCalib[i] != null)
            {
                sb.AppendLine("pad" + (i + 1) + "_calib=" + padCalib[i]);
            }
            if (padStickMode[i] != null)
            {
                sb.AppendLine("pad" + (i + 1) + "_stickmode=" + padStickMode[i]);
            }
            sb.AppendLine("pad" + (i + 1) + "_rumble=" + (padRumbleOn[i] ? "1" : "0"));
            sb.AppendLine("pad" + (i + 1) + "_swap_sticks=" + (padSwapSticks[i] ? "1" : "0"));
            if (!string.IsNullOrEmpty(padLayoutName[i]))
            {
                sb.AppendLine("pad" + (i + 1) + "_layout=" + padLayoutName[i]);
            }
            sb.AppendLine("pad" + (i + 1) + "_gadget_switch=" + (padGadgetSwitch[i] ? "1" : "0"));
        }
        AppendIniExtras(sb, "[input]");
        sb.AppendLine();
        sb.AppendLine("; [display]:");
        sb.AppendLine("; widescreen=1 widens the 3D FOV to match the window shape (resize or");
        sb.AppendLine(";   go fullscreen for ultrawide); 0 = classic stretched 4:3.");
        sb.AppendLine("; msaa: 1=off, 2/4/8 = MSAA samples.");
        sb.AppendLine("; filtering: point | bilinear | 3point (N64-style three-point) |");
        sb.AppendLine(";   trilinear (default: bilinear plus mip minification on every draw).");
        sb.AppendLine("; anisotropy: 0=off, 2..16 (sharper floors/walls at glancing angles).");
        sb.AppendLine("; filter_2d: 0 = crisp unfiltered HUD/menu graphics.");
        sb.AppendLine("; high_lod: 1 = always use the highest-detail model LODs (no pop).");
        sb.AppendLine("; fov: 3D viewport field of view in degrees, 40..120 (60 = original).");
        sb.AppendLine("; maxfps: render pace cap; 0 = render as fast as possible.");
        sb.AppendLine("; vsync: 0 = never wait for vblank (tearing possible).");
        sb.AppendLine(";   maxfps=0 + vsync=0 = fully uncapped (testing).");
        sb.AppendLine("; tickhz: DERIVED - the simulation rate follows Max FPS (never below 60,");
        sb.AppendLine(";   uncapped when Max FPS is). Only \"hw\" is set here, by N64 speed.");
        sb.AppendLine(";   This is what sets your FPS - the port presents exactly one frame");
        sb.AppendLine(";   per tick, so maxfps above can only cap it, never raise it.");
        sb.AppendLine(";   The time UNIT stays 1/60s at any setting - AI, animation, ammo and");
        sb.AppendLine(";   every authored duration keep vanilla 60Hz cadence; only continuous");
        sb.AppendLine(";   motion and input polling run at the raised rate.");
        sb.AppendLine("[display]");
        sb.AppendLine("window_width=" + (int) winW.Value);
        sb.AppendLine("window_height=" + (int) winH.Value);
        if (iniWindowX != null && iniWindowY != null)
        {
            sb.AppendLine("window_x=" + iniWindowX);
            sb.AppendLine("window_y=" + iniWindowY);
        }
        if (iniWindowDisplay != null)
        {
            sb.AppendLine("window_display=" + iniWindowDisplay);
        }
        sb.AppendLine("; fullscreen: windowed | fullscreen (a borderless window over the");
        sb.AppendLine(";   display, the default) | exclusive (true fullscreen). 0 and 1 still read");
        sb.AppendLine(";   as windowed and fullscreen.");
        sb.AppendLine("fullscreen=" + DisplayModeValues[displayMode.SelectedIndex]);
        sb.AppendLine("widescreen=" + (widescreen.Checked ? "1" : "0"));
        sb.AppendLine("msaa=" + MsaaValues[msaa.SelectedIndex]);
        sb.AppendLine("filtering=" + FilterValues[filtering.SelectedIndex]);
        sb.AppendLine("anisotropy=" + AnisoValues[anisotropy.SelectedIndex]);
        sb.AppendLine("filter_2d=" + (filter2d.Checked ? "1" : "0"));
        sb.AppendLine("high_lod=" + (highLod.Checked ? "1" : "0"));
        sb.AppendLine("; low_res: the LEVEL render's line count, scaled up to the window - 0 = the");
        sb.AppendLine(";   window's own resolution (Native), 240 = Hardware (the N64's, was");
        sb.AppendLine(";   low_res=1), then 480, 720, 1080.");
        sb.AppendLine(";   Levels only: the frontend menus stay at window resolution.");
        sb.AppendLine("low_res=" + LowResValues[lowRes.SelectedIndex]);
        sb.AppendLine("; mipmaps=1: N64-style mip chains on the textures the game draws with");
        sb.AppendLine(";   per-pixel LOD - far surfaces stop shimmering / moire-ing (most visible");
        sb.AppendLine(";   with low_res). 0 = sample level 0 only.");
        sb.AppendLine("mipmaps=" + (hwMipmaps.Checked ? "1" : "0"));
        sb.AppendLine("; n64_aa=1: the level frame is anti-aliased the way the N64's Video Interface");
        sb.AppendLine(";   did it - edge pixels blended with their neighbours by the coverage they");
        sb.AppendLine(";   lack, then a soft resample to the window. Uses MSAA (at least 4x while on).");
        sb.AppendLine("n64_aa=" + (n64Aa.Checked ? "1" : "0"));
        sb.AppendLine("; room_cache=2: static room geometry and model parts are kept on the GPU");
        sb.AppendLine(";   after their first frame (one draw each; fewer draw calls, meant for");
        sb.AppendLine(";   low-end machines). 0 = every triangle through the interpreter.");
        sb.AppendLine("room_cache=" + (roomCache.Checked ? "2" : "0"));
        sb.AppendLine("; tri_hoist=1: the renderer derives draw state per state change, not per triangle.");
        sb.AppendLine("tri_hoist=" + (triHoist.Checked ? "1" : "0"));
        sb.AppendLine("; show_fps=1: in-game FPS/TPS overlay (TPS should read 60 at any tick rate).");
        sb.AppendLine("show_fps=" + (showFps.Checked ? "1" : "0"));
        sb.AppendLine("fov=" + fov.Value.ToString("0.##", CultureInfo.InvariantCulture));
        sb.AppendLine("; shadow_alpha=N: character/object drop-shadow strength, % of the N64 value (65 default).");
        sb.AppendLine("shadow_alpha=" + (int) shadowAlpha.Value);
        sb.AppendLine("; crosshair_color=RRGGBB (hex) recolours BOTH crosshairs - the aim sight");
        sb.AppendLine(";   and the always-on target. \"default\" keeps the original red.");
        sb.AppendLine("crosshair_color=" + (crosshairCustom.Checked
            ? string.Format(CultureInfo.InvariantCulture, "{0:X2}{1:X2}{2:X2}",
                crosshairColor.BackColor.R, crosshairColor.BackColor.G, crosshairColor.BackColor.B)
            : "default"));
        sb.AppendLine("; music_volume / sfx_volume: 0..100 percent, applied globally from launch");
        sb.AppendLine(";   (the original game plays intros and menus at full volume regardless).");
        sb.AppendLine(";   The save file you last loaded writes its own values back here.");
        sb.AppendLine("music_volume=" + (int) musicVol.Value);
        sb.AppendLine("sfx_volume=" + (int) sfxVol.Value);
        sb.AppendLine("maxfps=" + (n64Speed.Checked ? -1 : (int) maxfps.Value)); /* -1 = Hardware (G025) */
        sb.AppendLine("vsync=" + (vsync.Checked ? "1" : "0"));
        sb.AppendLine(";   hw = the N64's own pacing: the 60 Hz sim, each frame held for the fields the");
        sb.AppendLine(";   console would have needed (its frame rate, slowdown and stepped motion).");
        /* only written for hw: the game derives every other value from maxfps,
           so writing one would leave a number in the file that is not true. */
        if (n64Speed.Checked) sb.AppendLine("tickhz=hw");
        sb.AppendLine("; adzy=1 replaces Bond at every solo mission start with the");
        sb.AppendLine(";   Siberian Special Forces model and the snow cuff. Cosmetic only.");
        sb.AppendLine("adzy=" + (adzy.Checked ? "1" : "0"));
        sb.AppendLine("unlock_all=" + (unlockAll.Checked ? "1" : "0"));
        sb.AppendLine("pause_pillars=" + (pausePillars.Checked ? "1" : "0"));
        sb.AppendLine("; always_crosshair=1 draws a Perfect Dark style target (small plus, GE sight red)");
        sb.AppendLine(";   at the gun's aim point while not aiming; aim mode keeps the GE sight.");
        sb.AppendLine("always_crosshair=" + (alwaysCrosshair.Checked ? "1" : "0"));
        sb.AppendLine("; aim_lock=1 pins the crosshair to the screen centre: aiming turns the view");
        sb.AppendLine(";   instead of moving the crosshair, and the gun no longer sways off centre");
        sb.AppendLine(";   while turning. Auto-aim and the tank keep their own behaviour.");
        sb.AppendLine("aim_lock=" + (aimLock.Checked ? "1" : "0"));
        sb.AppendLine("; no_head_roll=1 stops the camera tilting sideways with Bond's head animation");
        sb.AppendLine(";   and removes the standing-still breathing sway (the hidden stamina gauge),");
        sb.AppendLine(";   aiming or not, any zoom; the walking head bob is kept.");
        sb.AppendLine("no_head_roll=" + (noHeadRoll.Checked ? "1" : "0"));
        AppendIniExtras(sb, "[display]");
        foreach (var kv in iniExtra)
        {   /* sections this dialog does not write at all (hand-made, or keys
             * above any header): keep them as their own section at the end */
            if (kv.Key == "[controls]" || kv.Key == "[input]" || kv.Key == "[display]" || kv.Value.Count == 0)
            {
                continue;
            }
            sb.AppendLine();
            if (kv.Key.Length > 0)
            {
                sb.AppendLine(kv.Key);
            }
            AppendIniExtras(sb, kv.Key);
        }

        try
        {
            File.WriteAllText(iniPath, sb.ToString());
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "Could not write midas.ini:\n" + ex.Message, "GE Options",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }
    }

    // ---- rebind capture ----
    void CaptureBind(int row, bool append)
    {
        using (var cf = new CaptureForm())
        {
            if (cf.ShowDialog(this) != DialogResult.OK || cf.Result == null)
            {
                return;
            }
            if (cf.Result.Length == 0)
            {   /* Delete in the capture dialog: unbind the row */
                keyBinds[row] = "";
                return;
            }
            if (!append)
            {
                keyBinds[row] = cf.Result;
                return;
            }
            var parts = new List<string>();
            int blanks = 0; /* decoded as video.c videoParseBindValue does: the old "," key name splits into TWO empty fields */
            foreach (string p in keyBinds[row].Split(','))
            {
                string t = p.Trim();
                if (t.Length == 0)
                {
                    if (++blanks == 2)
                    {
                        parts.Add("Comma");
                        blanks = 0;
                    }
                    continue;
                }
                blanks = 0;
                parts.Add(t);
            }
            if (!parts.Contains(cf.Result))
            {
                parts.Add(cf.Result);
            }
            while (parts.Count > 4) // port reads at most 4 keys per bind
            {
                parts.RemoveAt(0);
            }
            keyBinds[row] = string.Join(",", parts.ToArray());
        }
    }
}

// Post-calibration result: the measured stick octagon drawn over the
// ideal N64 gate shape, with the per-direction numbers in N64 units
// (raw/256; y shown up-positive). Green = this stick, gray = ideal shape.
class CalibResultForm : Form
{
    static readonly string[] DirNames = { "E", "NE", "N", "NW", "W", "SW", "S", "SE" };
    static readonly int[,] Ideal = { {85,0},{65,65},{0,85},{-65,65},{-85,0},{-65,-65},{0,-85},{65,-65} };
    readonly int[] vx = new int[8];
    readonly int[] vy = new int[8];

    public CalibResultForm(string csv)
    {
        string[] parts = csv.Split(',');
        for (int i = 0; i < 8; i++)
        {
            int.TryParse(parts[i * 2], out vx[i]);
            int.TryParse(parts[i * 2 + 1], out vy[i]);
        }

        Text = "Stick calibration result";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        ClientSize = new Size(470, 330);

        var canvas = new Panel { Location = new Point(12, 12), Size = new Size(280, 280), BackColor = Color.Black };
        canvas.Paint += OnPaint;
        Controls.Add(canvas);

        var list = new StringBuilder();
        list.AppendLine("Measured (N64 units):");
        list.AppendLine();
        for (int i = 0; i < 8; i++)
        {
            list.AppendLine(DirNames[i].PadRight(3) + (vx[i] / 256) + ", " + (-(vy[i] / 256)));
        }
        Controls.Add(new Label
        {
            Text = list.ToString(),
            Location = new Point(304, 12),
            Size = new Size(155, 240),
            Font = new Font(FontFamily.GenericMonospace, 9f),
        });

        var bOk = new Button { Text = "OK", Location = new Point(330, 262), Width = 100, Height = 28 };
        bOk.Click += delegate { DialogResult = DialogResult.OK; };
        AcceptButton = bOk;
        Controls.Add(bOk);
    }

    void OnPaint(object sender, PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        int cx = 140, cy = 140;
        float r = 120f;

        // fit: the largest measured magnitude maps to radius r
        float maxm = 1f;
        for (int i = 0; i < 8; i++)
        {
            float m = Math.Max(Math.Abs(vx[i]), Math.Abs(vy[i]));
            if (m > maxm) maxm = m;
        }
        float ms = r / maxm;
        float isc = r / 85f;

        using (var axis = new Pen(Color.FromArgb(70, 70, 70)))
        {
            g.DrawLine(axis, cx - (int) r, cy, cx + (int) r, cy);
            g.DrawLine(axis, cx, cy - (int) r, cx, cy + (int) r);
        }

        // ideal N64 gate shape (85 cardinal / 65 diagonal), gray reference
        var ip = new PointF[8];
        for (int i = 0; i < 8; i++)
        {
            ip[i] = new PointF(cx + Ideal[i, 0] * isc, cy - Ideal[i, 1] * isc);
        }
        using (var gray = new Pen(Color.FromArgb(130, 130, 130)) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dash })
        {
            g.DrawPolygon(gray, ip);
        }

        // measured octagon (SDL raw plotted directly: y down = screen down)
        var mp = new PointF[8];
        for (int i = 0; i < 8; i++)
        {
            mp[i] = new PointF(cx + vx[i] * ms, cy + vy[i] * ms);
        }
        using (var green = new Pen(Color.LimeGreen, 2f))
        {
            g.DrawPolygon(green, mp);
        }
        using (var dot = new SolidBrush(Color.LimeGreen))
        using (var txt = new SolidBrush(Color.White))
        using (var f = new Font(FontFamily.GenericSansSerif, 7.5f))
        {
            for (int i = 0; i < 8; i++)
            {
                g.FillEllipse(dot, mp[i].X - 3, mp[i].Y - 3, 6, 6);
                float lx = mp[i].X + (mp[i].X - cx) * 0.10f - 8;
                float ly = mp[i].Y + (mp[i].Y - cy) * 0.10f - 6;
                g.DrawString(DirNames[i], f, txt, lx, ly);
            }
        }
    }
}

// Modal "press a key or mouse button" capture. Names must match what the
// port resolves: SDL scancode names (SDL_GetScancodeFromName) plus the
// launcher-style "Mouse ..." button names.
class CaptureForm : Form
{
    public string Result;

    public CaptureForm()
    {
        Text = "Bind";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ControlBox = false;
        StartPosition = FormStartPosition.CenterParent;
        ClientSize = new Size(300, 74);
        KeyPreview = true;
        var l = new Label
        {
            Text = "Press a key or mouse button\n(Delete = unbind, Esc cancels)",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleCenter,
        };
        l.MouseDown += OnMouse;
        MouseDown += OnMouse;
        Controls.Add(l);
    }

    void OnMouse(object sender, MouseEventArgs e)
    {
        switch (e.Button)
        {
            case MouseButtons.Left: Result = "Mouse Left"; break;
            case MouseButtons.Right: Result = "Mouse Right"; break;
            case MouseButtons.Middle: Result = "Mouse Middle"; break;
            case MouseButtons.XButton1: Result = "Mouse X1"; break;
            case MouseButtons.XButton2: Result = "Mouse X2"; break;
        }
        if (Result != null)
        {
            DialogResult = DialogResult.OK;
        }
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        if (e.Delta != 0)
        {
            Result = e.Delta > 0 ? "Mouse Wheel Up" : "Mouse Wheel Down";
            DialogResult = DialogResult.OK;
        }
        base.OnMouseWheel(e);
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        Keys k = keyData & Keys.KeyCode;
        if (k == Keys.Escape)
        {
            DialogResult = DialogResult.Cancel;
            return true;
        }
        if (k == Keys.Delete)
        {   /* Delete = unbind (Result "" - the Delete key itself is not bindable here) */
            Result = "";
            DialogResult = DialogResult.OK;
            return true;
        }
        string name = KeyMap.ToSdl(k);
        if (name != null)
        {
            Result = name;
            DialogResult = DialogResult.OK;
            return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }
}

// WinForms Keys -> SDL scancode name (the strings SDL_GetScancodeName
// returns, which midas.ini stores). WinForms cannot tell left/right modifiers
// apart in KeyCode, so modifiers map to the Left variants.
static class KeyMap
{
    static readonly Dictionary<Keys, string> map = Build();

    public static string ToSdl(Keys k)
    {
        string s;
        return map.TryGetValue(k, out s) ? s : null;
    }

    static Dictionary<Keys, string> Build()
    {
        var m = new Dictionary<Keys, string>();
        for (Keys k = Keys.A; k <= Keys.Z; k++)
        {
            m[k] = k.ToString();
        }
        for (int i = 0; i <= 9; i++)
        {
            m[Keys.D0 + i] = i.ToString();
            m[Keys.NumPad0 + i] = "Keypad " + i;
        }
        for (int i = 1; i <= 12; i++)
        {
            m[Keys.F1 + (i - 1)] = "F" + i;
        }
        m[Keys.Up] = "Up"; m[Keys.Down] = "Down"; m[Keys.Left] = "Left"; m[Keys.Right] = "Right";
        m[Keys.Space] = "Space";
        m[Keys.Enter] = "Return";
        m[Keys.Tab] = "Tab";
        m[Keys.Back] = "Backspace";
        m[Keys.CapsLock] = "CapsLock";
        m[Keys.ShiftKey] = "Left Shift";
        m[Keys.ControlKey] = "Left Ctrl";
        m[Keys.Menu] = "Left Alt";
        m[Keys.LWin] = "Left GUI";
        m[Keys.RWin] = "Right GUI";
        m[Keys.Insert] = "Insert"; m[Keys.Delete] = "Delete";
        m[Keys.Home] = "Home"; m[Keys.End] = "End";
        m[Keys.PageUp] = "PageUp"; m[Keys.PageDown] = "PageDown";
        m[Keys.Pause] = "Pause";
        m[Keys.Scroll] = "ScrollLock";
        m[Keys.NumLock] = "Numlock";
        m[Keys.Add] = "Keypad +"; m[Keys.Subtract] = "Keypad -";
        m[Keys.Multiply] = "Keypad *"; m[Keys.Divide] = "Keypad /";
        m[Keys.Decimal] = "Keypad .";
        m[Keys.Oemcomma] = "Comma"; m[Keys.OemPeriod] = "."; // SDL's "," is the midas.ini list separator (video.c sSepKeyNames)
        m[Keys.OemQuestion] = "/"; m[Keys.OemSemicolon] = ";";
        m[Keys.OemQuotes] = "'"; m[Keys.Oemtilde] = "`";
        m[Keys.OemOpenBrackets] = "["; m[Keys.OemCloseBrackets] = "]";
        m[Keys.OemPipe] = "\\";
        m[Keys.OemMinus] = "-"; m[Keys.Oemplus] = "=";
        return m;
    }
}
