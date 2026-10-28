-- heist_menu_defs.lua: what the heist's start menu offers (shipped as Data/Scripts/modes/_heist_menu_defs.lua). Read by the lobby
-- (tools/mode_heist_menu.lua) and the run side (tools/heist_flow.lua). Defines the global HEIST_MENU_DEFS:
--   ROWS     every setting: id, group, label, choices { { label, value }, ... }, default, desc; scored = true when it changes the job
--            (any scored row off its default makes the records key "<job>_<d>_custom"); cheat = true: no records at all
--   JOBS     the cards: id, lg (the generated level's name: levelgen.has / GE_LEVELGEN), the theme's name, place and blurb
--   settings(M) -> HEIST_SETTINGS overrides, difficulty (0..3), env (GE_* for the run), cheat
--   record_key(job, difficulty, custom)   the bests' key in Data/Saves/ui/heist_bests.ini
-- The words are HEIST_THEME's (tools/heist_theme.lua); only the rows' own labels and help lines are here, beside their values.

if not HEIST_THEME then include("_heist_theme") end
HEIST_MENU_DEFS = {}
local D = HEIST_MENU_DEFS
local T = HEIST_THEME.menu
D.T, D.TD = T, HEIST_THEME.debrief

local ON_OFF = { { "ON", true }, { "OFF", false } }
local DEALS = { { "FRESH EACH JOB", 0 } }
for i = 1, 12 do DEALS[#DEALS + 1] = { string.format("DEAL %d", i), i } end

D.ROWS = {
  -- THE JOB
  { id = "difficulty", group = "JOB", label = "DIFFICULTY", default = 1,
    choices = { { T.difficulty[1], 0 }, { T.difficulty[2], 1 }, { T.difficulty[3], 2 }, { T.difficulty[4], 3 } },
    desc = "The force inside (Agent ~30 posted guards at stock aim, Secret Agent ~45, 00 Agent ~70, 007 the full 100+) and the civilian limit." },
  { id = "deal", group = "JOB", label = "THE DAY'S DEAL", scored = true, default = 1, choices = { { "RANDOMISED", true }, { "AS WRITTEN", false } },
    desc = "Randomised: who carries which card, dummy cameras, the time lock, the roster, sealed routes, the job of the day. As written: the plan's job." },
  { id = "seed", group = "JOB", label = "DEAL NUMBER", default = 1, choices = DEALS,
    dim = function(M) return not M.val("deal") end,
    desc = "Fresh each job: a new deal every time. A number: the same bank every time (share it: the same number deals the same job)." },
  { id = "planning", group = "JOB", label = "PLANNING BOARD", default = 1, choices = { { "ON", true }, { "SKIP: STANDARD KIT", false } },
    desc = "On: buy the kit, pick the way in and the extras in the van before the clock starts. Skip: the standard kit, the front door." },
  -- THE FIELD TEAM
  { id = "partner", group = "CREW", label = "PARTNER", scored = true, default = 1, choices = { { "SOLO", false }, { "00 PARTNER (FOLLOWS)", true } },
    desc = "A second Bond who follows you (a simple follower, not a thinking bot). Every living Bond must reach the exit. Keys are per Bond." },
  { id = "cover", group = "CREW", label = "COVER", scored = true, default = 1, choices = { { "BANK STAFF", true }, { "NONE", false } },
    desc = "Bank staff: the guards take you for staff until something gives you away (a gun, loot, a restricted room). None: the stock sight." },
  { id = "hand", group = "CREW", label = "SIDEARM", default = 1, choices = { { "HOLSTERED", "fist" }, { "DRAWN", "pp7sil" } },
    dim = function(M) return not M.val("cover") end,
    desc = "What you hold as the job starts. A drawn gun gives the cover away in under a second." },
  -- THE OPPOSITION
  { id = "personalities", group = "GUARDS", label = "PERSONALITIES", scored = true, default = 1, choices = { { "ON", true }, { "UNIFORM", false } },
    desc = "Rookies, veterans, by-the-book and bribable guards, dealt with the job. Uniform: every guard alike." },
  { id = "hearing", group = "GUARDS", label = "HEARING", scored = true, default = 1, choices = { { "NORMAL", 1.0 }, { "SHARP", 1.3 }, { "DULL", 0.7 } },
    desc = "How far a shot, a slap or a door carries to the guards (on top of the difficulty's own)." },
  { id = "combat", group = "GUARDS", label = "LOUD-ROUTE TACTICS", scored = true, default = 1, choices = { { "TACTICAL", true }, { "STOCK", false } },
    desc = "Tactical: when it goes loud the guards take cover, peek, flank in pairs and fall back; the police and SWAT come. Stock: GoldenEye's fighting." },
  { id = "waves", group = "GUARDS", label = "REINFORCEMENTS", scored = true, default = 1,
    choices = { { "EVERY 25 S", 25 }, { "EVERY 15 S", 15 }, { "EVERY 40 S", 40 } }, desc = "Once the alarm is up: how often a new wave of guards arrives." },
  { id = "hold_fire", group = "GUARDS", label = "MIND THE CIVILIANS", scored = true, default = 1, choices = ON_OFF,
    desc = "Guards hold their fire while a bank civilian is in their line, and shoot when it clears." },
  -- SECURITY
  { id = "interlock", group = "SECURITY", label = "DOOR INTERLOCKS", scored = true, default = 1, choices = ON_OFF,
    desc = "The staff mantrap and the sally port: one door of a group open at a time." },
  { id = "lockdown", group = "SECURITY", label = "ALARM LOCKDOWN", scored = true, default = 1, choices = ON_OFF,
    desc = "The alarm drops the blast doors at the hall: BLUE to pass. The loading dock is always open." },
  { id = "timelock", group = "SECURITY", label = "EMPTY-VAULT TIME LOCK", scored = true, default = 1, choices = { { "QUIET", false }, { "ALARM", true } },
    desc = "Quiet: the vault closing on nobody raises nothing (a clean job stays clean). Alarm: it always alarms. Anyone trapped inside: alarm." },
  { id = "crack", group = "SECURITY", label = "CRACKER", scored = true, default = 1, choices = { { "90 S", 90 }, { "60 S", 60 }, { "120 S", 120 } },
    desc = "Seconds of work at the vault door (it runs only while a Bond is near it)." },
  { id = "jams", group = "SECURITY", label = "CRACKER JAMS", scored = true, default = 1, choices = { { "SOMETIMES", 0.30 }, { "NEVER", 0 }, { "OFTEN", 0.5 } },
    desc = "Every 15 s of work the cracker may jam: walk back and restart it." },
  -- CIVILIANS
  { id = "hostages", group = "CIVILIANS", label = "HOSTAGES & SHIELD", scored = true, default = 1, choices = ON_OFF,
    desc = "Grab a compliant civilian with a pistol drawn: guards hold their fire and go round; police stand-offs; fines." },
  { id = "civ_limit", group = "CIVILIANS", label = "CIVILIAN LIMIT", scored = true, default = 1,
    choices = { { "BY DIFFICULTY", -1 }, { "ZERO TOLERANCE", 0 }, { "LENIENT 6", 6 } },
    desc = "Civilian deaths before the job fails (by difficulty: Agent 4, Secret Agent 2, 00 Agent and 007 1). Knock-outs never count." },
  -- Q BRANCH KIT
  { id = "gadgets", group = "KIT", label = "STANDARD KIT", scored = true, default = 1, choices = { { "FULL", "full" }, { "LIGHT", "light" }, { "NONE", "none" } },
    dim = function(M) return M.val("planning") end,
    desc = "With the planning board off: full (piton gun, lockpicks, 4 coins, 2 tape loops, sleep gas, disguise), light, or nothing." },
  { id = "ar", group = "KIT", label = "Q-BRANCH SPECS (AR)", default = 1, choices = ON_OFF,
    desc = "Suspicion marks over the guards, speech bubbles, key brackets, waypoints (Shift+F3 toggles them in the field)." },
  { id = "hints", group = "KIT", label = "HINTS", default = 1, choices = ON_OFF, desc = "One-line hints at a keyed door, the vault door, the breaker, the console." },
  { id = "guide", group = "KIT", label = "FIRST-PLAY GUIDE", default = 1, choices = ON_OFF, desc = "The objective line, the key strip and the prompts." },
  -- FIELD TESTS
  { id = "invincible", group = "TEST", label = "INVULNERABLE", cheat = true, default = 1, choices = { { "OFF", false }, { "ON", true } },
    desc = "A licence to survive: Bond takes no damage. Field test: no records." },
  { id = "diag", group = "TEST", label = "F3 DIAGNOSTICS", default = 1, choices = { { "OFF", false }, { "ON", true } },
    desc = "Start with the F3 diagnostics up (guards' awareness, hearing, routes)." },
  { id = "noise_rings", group = "TEST", label = "NOISE RINGS", default = 1, choices = { { "OFF", false }, { "ON", true } },
    desc = "A ripple on the floor for every noise the guards can hear." },
}
D.BY_ID = {}
for _, r in ipairs(D.ROWS) do D.BY_ID[r.id] = r end
D.GROUPS = { "JOB", "CREW", "GUARDS", "SECURITY", "CIVILIANS", "KIT", "TEST" }
D.GROUP_COL = { JOB = 1, CREW = 1, GUARDS = 1, SECURITY = 2, CIVILIANS = 2, KIT = 2, TEST = 2 }

D.JOBS = {
  { id = "bank", lg = "bank" },
  { id = "casino", lg = "casino" },
}
D.JOB_BY_ID = {}
for _, j in ipairs(D.JOBS) do
  local t = HEIST_THEME.jobs[j.id] or {}
  j.name, j.place, j.blurb, j.more = t.name or j.id:upper(), t.place or "", t.blurb or "", t.more or ""
  D.JOB_BY_ID[j.id] = j
end
function D.available(j) return levelgen and levelgen.has(j.lg) or (j.lg == "bank") end
-- the job the levelgen slot holds now (the lobby's backdrop)
function D.job_here()
  local lg = levelgen and levelgen.current() or "bank"
  for _, j in ipairs(D.JOBS) do if j.lg == lg then return j end end
  return D.JOBS[1]
end

-- the menu's values -> the run (M: a UI_MENU with these rows)
function D.settings(M)
  local S = {}
  S.randomise = M.val("deal")
  S.planning = M.val("planning")
  S.cover = M.val("cover")
  S.start_hand = M.val("hand")
  S.personalities = M.val("personalities")
  S.hearing = M.val("hearing")
  if not M.val("combat") then S.combat = false end
  S.wave_interval = M.val("waves")
  S.hold_fire = M.val("hold_fire")
  S.interlock = M.val("interlock")
  S.lockdown = M.val("lockdown")
  S.timelock_alarm_empty = M.val("timelock")
  S.crack_time = M.val("crack")
  S.jam_chance = M.val("jams")
  S.hostages = M.val("hostages")
  if M.val("civ_limit") >= 0 then S.civ_limit = M.val("civ_limit") end
  local g = M.val("gadgets")
  if not S.planning and g == "light" then S.gadgets = { coins = 2, loops = 1, gas = 0, thermal = false, disguise = false }
  elseif not S.planning and g == "none" then S.gadgets = { piton = false, lockpick = false, coins = 0, loops = 0, gas = 0, thermal = false, disguise = false } end
  S.ar = M.val("ar")
  S.hints = M.val("hints")
  S.guide = M.val("guide")
  S.noise_rings = M.val("noise_rings")
  S.invincible = M.val("invincible") or nil
  local env = {}
  if M.val("deal") and M.val("seed") > 0 then env.GE_SEED = tostring(M.val("seed")) end
  if M.val("partner") then env.GE_COOP = "1"; env.GE_COOP_P2 = "follow" end
  if M.val("diag") then env.GE_DIAG = "1" end
  local cheat, custom = false, false
  for _, r in ipairs(D.ROWS) do
    if r.cheat and M.v[r.id] ~= r.default then cheat = true end
    if r.scored and M.v[r.id] ~= r.default then custom = true end
  end
  return S, M.val("difficulty"), env, cheat, custom
end

function D.record_key(job, d, custom) return string.format("%s_%d%s", job, d or 0, custom and "_custom" or "") end
