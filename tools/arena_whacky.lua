-- Arena "Whacky" layer (features/arena.md "Whacky", modding/arena.md). Shipped to Data/Scripts/modes/_arena_whacky.lua and included by
-- arena.lua. Defines the global ARENA_WHACKY.new(S, MUT) -> WH; arena.lua calls WH.spawned(c, spec, entry) and WH.tick(dt, elapsed,
-- phase, wave) and nothing else. It adds the Whacky mutators to ARENA_MUTATORS.DEFS (a preset turns them on with mutator_list, see
-- arena-whacky.lua), colours the affix elites and the Golden Gun boss, and runs the minion launcher (port_whacky.c).
--
-- Engine pieces: chrfx (port_chrfx.c, PRESENTATION: per-chr colour wash, translucency, cycle / pulse / flicker / shimmer) and whacky
-- (port_whacky.c, SIM: anim rate, line mode, 2x weapons, scale, the minion launcher). Every gameplay change goes through sim calls made
-- from the mode's own (deterministic) handlers; the colours never feed back into the sim. Settings (S = arena.lua's settings):
--   whacky          false (default); true = the elite glow and the golden champion (whacky_fx) and the Whacky reward drops
--   whacky_fx       colour the affix elites by affix and the Golden Gun boss in gold (default: S.whacky)
--   minion_shots    rockets that hatch minions per launcher (10);  minion_count per rocket (3);  minion_scale (0.25)
--   minion_life     seconds before a minion fades out (15);  minion_health (0.3 x a guard: one hit);  minion_cap live at once (12)
--   minion_hostile  false; true = the minions go for Bond instead (chaos)
--   crowd_throw_every  seconds between the crowd's gifts in THE CROWD PROVIDES (14)
--   whacky_reward   after each boss wave the crowd throws the minion launcher (true with whacky)
-- Test keys (test_keys or GE_ARENA_TESTKEYS=1): F5 / F6 next / previous effect now (on the live enemies), F7 effect off, F8 the
-- minion launcher (and rockets).

ARENA_WHACKY = {}

-- every player-facing string comes from ARENA_THEME.whacky (arena_theme.lua, the Bond theme); the literal is the fallback
local function TW(key, fallback)
  local t = ARENA_THEME and ARENA_THEME.whacky
  local v = t and t[key]
  if v == nil then return fallback end
  return v
end

local function call(text, level, sub)   -- the arena's centre callout (port_arenahud.c); the log line is for the tests
  if arena and arena.callout then pcall(arena.callout, text, level or 2, sub) end
  print("ge: whacky: CALLOUT " .. text .. (sub and ("  " .. sub) or ""))
end

local function fx(c, t) local ok = pcall(chrfx.set, c, t); return ok end

local AFFIX_COL = { armoured = "steel", swift = "yellow", knives = "green", explosive = "orange", regen = "pink", marksman = "red" }

function ARENA_WHACKY.new(S, MUT)
  local WH = { live = {}, minion_kills = 0, gifts = {}, phase = nil, wave = 0, elapsed = 0, test_i = 0, kill_call_at = -10 }
  local FXON = S.whacky_fx
  if FXON == nil then FXON = S.whacky and true or false end
  local DEFS = ARENA_MUTATORS and ARENA_MUTATORS.DEFS or {}
  local pit = nil   -- pit floor pads, for the crowd's gifts

  local function launcher_opts()
    return { count = S.minion_count or 3, scale = S.minion_scale or 0.25, life = S.minion_life or 15, health = S.minion_health or 0.3,
             hostile = S.minion_hostile and true or false, cap = S.minion_cap or 12 }
  end

  local function arm(p, shots)
    pcall(bond.give, p, "rocketlaunch", 12)    -- bond.give takes the item names (port_lua.c), spawn.item the arena's ("rocket")
    pcall(bond.equip, p, "rocketlaunch")       -- in hand: the next trigger pull is a minion rocket
    whacky.launcher(p, shots or S.minion_shots or 10, launcher_opts())
  end
  local function arm_all(shots) for p = 0, bond.count() - 1 do arm(p, shots) end end
  local function disarm_all() for p = 0, bond.count() - 1 do whacky.launcher(p, 0, launcher_opts()) end end
  WH.arm, WH.arm_all = arm, arm_all

  -- the base look of a chr (elite glow, the golden champion), re-applied when a test key swaps the wave's effect off
  local function base_look(rec)
    if not FXON then return false end
    if rec.boss and rec.weapon == "goldengun" then
      return fx(rec.c, { tint = "gold", amount = 0.6, anim = "pulse", lo = 0.55, speed = 0.4 })
    end
    local a = rec.affixes
    if a and #a > 0 then
      local c1 = AFFIX_COL[a[1]] or "white"
      if #a > 1 then return fx(rec.c, { tint = c1, tint2 = AFFIX_COL[a[2]] or "white", amount = 0.55, anim = "pulse2", speed = 0.7 }) end
      return fx(rec.c, { tint = c1, amount = 0.6, anim = "pulse", lo = 0.3, speed = 0.8 })
    end
    return false
  end

  -- ---------------------------------------------------------------- the crowd's gifts --
  local function pit_pads()
    if pit then return pit end
    pit = {}
    for i = 0, pads.count() - 1 do
      local x, y, z = pads.pos(i)
      if x and y < 1 and math.abs(x) <= 4500 and math.abs(z) <= 4500 and math.abs(x) + math.abs(z) <= 6000 then
        pit[#pit + 1] = { id = i, x = x, z = z }
      end
    end
    return pit
  end

  local function pad_near_bond()
    local bx, _, bz = bond.pos(0)
    if not bx then return nil end
    local cands = {}
    for _, p in ipairs(pit_pads()) do
      local d = math.sqrt((p.x - bx) ^ 2 + (p.z - bz) ^ 2)
      if d > 300 and d < 1500 then cands[#cands + 1] = p end
    end
    if #cands == 0 then return nil end
    return cands[math.random(#cands)]
  end

  local GIFTS = {
    { item = "shotgun", label = "SHOTGUN", say = "A SHOTGUN" }, { item = "autoshot", label = "AUTOSHT", say = "AN AUTOMATIC SHOTGUN" },
    { item = "rcp90", label = "RCP90", say = "AN RC-P90" }, { item = "grenadelauncher", label = "GRENADE", say = "A GRENADE LAUNCHER" },
    { item = "armour", label = "ARMOUR", say = "BODY ARMOUR" }, { item = "laser", label = "LASER", say = "A MOONRAKER LASER" },
    { item = "rocket", label = TW("launcher_label", "MINIONS"), say = "THE MINION LAUNCHER", minions = true },
  }
  local gift_names = TW("gifts", {})
  for _, g in ipairs(GIFTS) do g.say = gift_names[g.item] or g.say end
  local LAUNCHER = TW("launcher", "MINION LAUNCHER")
  local LAUNCHER_SUB = TW("launcher_sub", "%d ROCKETS THAT HATCH TINY KNIFE THROWERS")

  function WH.crowd_throw(gift)
    gift = gift or GIFTS[math.random(#GIFTS)]
    local p = pad_near_bond()
    if not p then return false end
    local h = spawn.item(p.id, gift.item, gift.minions and { ammo = 12 } or nil)
    if not h then return false end
    if spawn.item_marker then pcall(spawn.item_marker, h, "gift", gift.label) end
    WH.gifts[#WH.gifts + 1] = { h = h, minions = gift.minions, born = WH.elapsed, x = p.x, z = p.z }
    call(string.format(TW("airdrop", "THE CROWD THROWS %s!"), gift.say), 2,
      gift.minions and string.format(LAUNCHER_SUB, S.minion_shots or 10) or nil)
    if ARENA_HOOKS and ARENA_HOOKS.cheer then pcall(ARENA_HOOKS.cheer, 2.5, 0.4) end
    print(string.format("ge: whacky: crowd gift %s on pad %d (%.0f, %.0f)", gift.item, p.id, p.x, p.z))
    return true
  end

  local function gifts_tick()
    if #WH.gifts == 0 then return end
    local keep = {}
    for _, g in ipairs(WH.gifts) do
      if spawn.item_alive(g.h) then
        if WH.elapsed - g.born > 60 then spawn.item_remove(g.h) else keep[#keep + 1] = g end
      elseif g.minions then   -- taken: the Bond nearest where it lay gets the launcher
        local best, bd = 0, 1e18
        for q = 0, bond.count() - 1 do
          local x, _, z = bond.pos(q)
          if x and (x - g.x) ^ 2 + (z - g.z) ^ 2 < bd then best, bd = q, (x - g.x) ^ 2 + (z - g.z) ^ 2 end
        end
        whacky.launcher(best, S.minion_shots or 10, launcher_opts())
        call(LAUNCHER .. "!", 3, string.format(LAUNCHER_SUB, S.minion_shots or 10))
      end
    end
    WH.gifts = keep
  end

  -- ---------------------------------------------------------------- the Whacky mutators --
  local st = {}   -- the running mutator's clock and flags
  local W = {}
  W.ghost = { title = "GHOST WAVE", blurb = "NOW YOU SEE THEM...",
    spawned = function(c) fx(c, { tint = "ghost", amount = 0.45, alpha = 0.42, anim = "flicker", lo = 0.10, speed = 0.6 }) end }
  W.chameleon = { title = "CHAMELEON", blurb = "THEY WILL NOT KEEP STILL",
    spawned = function(c) fx(c, { amount = 0.6, anim = "cycle", speed = 0.35 }) end }
  W.shadow = { title = "SHADOW TROOPS", blurb = "DARK SHAPES ON THE SAND",
    spec = function(s) s.anim_speed = math.max(s.anim_speed or 1, 1.15) end,
    spawned = function(c) fx(c, { tint = "shadow", amount = 0.92 }) end }
  W.glass = { title = "GLASS CANNONS", blurb = "ONE SHOT KILLS THEM - THEY HIT TWICE AS HARD", mods = { enemy_damage = 250 },
    spec = function(s) s.health = 0.05; s.armour = nil end,
    spawned = function(c) fx(c, { tint = "glass", amount = 0.6, alpha = 0.7, anim = "shimmer", lo = 0.35, speed = 1.4 }) end }
  W.giants = { title = "HONEY, I SHRUNK BOND", blurb = "OR DID THEY GROW?", mods = { tiny_bond = true },
    spawned = function(c) whacky.scale(c, 1.3) end }
  W.fastfwd = { title = "FAST FORWARD", blurb = "EVERYBODY AT DOUBLE SPEED",
    start = function() whacky.anim_rate(1.9) end, stop = function() whacky.anim_rate(1.0) end }
  W.slowmo = { title = "SLOW MOTION", blurb = "...BUT THERE ARE MORE OF THEM", extra = 0.4,
    start = function() whacky.anim_rate(0.5) end, stop = function() whacky.anim_rate(1.0) end }
  W.rockets = { title = "ROCKET PARTY", blurb = "SOMEBODY BROUGHT LAUNCHERS",
    start = function() st.rk = 0 end,
    spec = function(s)
      st.rk = (st.rk or 0) + 1
      if st.rk % 3 == 0 then s.weapon, s.accuracy, s.engage = "rocket", math.min(s.accuracy or 30, 30), 4000 end
    end }
  W.double = { title = "DOUBLE TROUBLE", blurb = "A GIFT: TWO OF THEM, ONE IN EACH HAND", extra = 0.5,
    start = function()
      local k = ({ "rocket", "grenade", "rcp90", "laser" })[math.random(4)]
      for p = 0, bond.count() - 1 do whacky.give_double(p, k) end
      st.double = k
    end }
  W.glitch = { title = "GLITCH", blurb = "THE SIGNAL IS BREAKING UP",
    spawned = function(c) fx(c, { amount = 0.5, anim = "cycle", speed = 2.5 }) end,
    start = function() st.t, st.on = 3, false end,
    tick = function(dt)
      st.t = st.t - dt
      if st.t <= 0 then
        st.on = not st.on
        whacky.linemode(st.on)
        st.t = st.on and 2.0 or (6 + math.random() * 6)
        if st.on then call(TW("emp", "SIGNAL LOST"), 2) end
      end
    end,
    stop = function() whacky.linemode(false) end }
  W.swap = { title = "SPEED SWAP", blurb = "FAST... SLOW... FAST...",
    start = function() st.t, st.fast = 6, false end,
    tick = function(dt)
      st.t = st.t - dt
      if st.t <= 0 then
        st.fast = not st.fast
        whacky.anim_rate(st.fast and 1.9 or 0.5)
        call(st.fast and TW("swap_fast", "FAST!") or TW("swap_slow", "SLOW!"), 2, TW("swap_sub", "SPEED SWAP"))
        st.t = 10
      end
    end,
    stop = function() whacky.anim_rate(1.0) end }
  W.crowd = { title = "THE CROWD PROVIDES", blurb = "GIFTS RAIN FROM THE STANDS",
    start = function() st.t = 4 end,
    tick = function(dt)
      st.t = st.t - dt
      if st.t <= 0 then st.t = S.crowd_throw_every or 14; WH.crowd_throw() end
    end }
  W.minions = { title = "MINION MADNESS", blurb = "YOUR ROCKETS HATCH TINY KNIFE THROWERS",
    start = function() arm_all(S.minion_shots or 10) end, stop = function() disarm_all() end }
  W.mayhem = { title = "TINY MAYHEM", blurb = "THEIR KNIFE THROWERS ARE TINY. AND ANGRY.",
    start = function()   -- the chaos option: hostile minions burst out of the gates now and then
      st.t = 3
    end,
    tick = function(dt)
      st.t = st.t - dt
      if st.t > 0 then return end
      st.t = 9
      local bx, _, bz = bond.pos(0)
      if not bx then return end
      local o = launcher_opts()
      o.hostile = true
      local saved = whacky.launcher_shots(0)
      whacky.launcher(0, saved, o)   -- settings only (shots unchanged); minions_at uses them
      local ang = math.random() * 6.2832
      whacky.minions_at(0, bx + math.sin(ang) * 1400, bz + math.cos(ang) * 1400, 3)
      call(TW("mayhem_burst", "TINY ASSASSINS!"), 2)
    end,
    stop = function()
      whacky.minions_fade()
      whacky.launcher(0, whacky.launcher_shots(0), launcher_opts())   -- back on Bond's side
    end }
  WH.ORDER = { "ghost", "chameleon", "shadow", "glass", "giants", "fastfwd", "slowmo", "rockets", "double", "glitch", "swap", "crowd",
               "minions", "mayhem" }
  local themed = TW("waves", {})
  for _, n in ipairs(WH.ORDER) do
    local d = W[n]
    local start = d.start
    if themed[n] then d.title, d.blurb = themed[n][1] or d.title, themed[n][2] or d.blurb end
    d.start = function(w, queue)   -- every Whacky mutator announces itself in the centre
      call(d.title, 3, d.blurb)
      if start then start(w, queue) end
    end
    DEFS[n] = d
  end
  WH.DEFS = W

  -- the start menu's WHACKY EFFECTS row (arena_menu_defs.lua) sets S.whacky = 1 (mild) / 2 (normal) / 3 (wild). A preset with its own
  -- mutator_list (arena-whacky.lua, whacky = true) keeps it. Otherwise the level picks the Whacky mutators that may roll (added to
  -- the A7 ones when WAVE MUTATORS is on) and a floor for the roll chance; mutator_list is read at each roll (mode_arena_mutators.lua).
  local lvl = type(S.whacky) == "number" and S.whacky or nil
  if lvl and S.mutator_list == nil then
    local SETS = {
      { "ghost", "chameleon", "shadow", "giants", "glitch", "crowd", "minions" },
      { "ghost", "chameleon", "shadow", "glass", "giants", "fastfwd", "slowmo", "rockets", "double", "glitch", "swap", "crowd", "minions" },
      WH.ORDER,
    }
    local FLOOR = { { 3, 0.25, 0.01, 0.40 }, { 2, 0.45, 0.02, 0.70 }, { 2, 0.70, 0.03, 0.95 } }
    lvl = math.max(1, math.min(3, math.floor(lvl)))
    local list = {}
    if S.mutators then for _, n in ipairs(ARENA_MUTATORS.ORDER or {}) do list[#list + 1] = n end end
    for _, n in ipairs(SETS[lvl]) do list[#list + 1] = n end
    local f = FLOOR[lvl]
    if not S.mutators then S.mutator_from = f[1] end
    S.mutator_from = math.min(S.mutator_from or 3, f[1])
    S.mutator_chance = math.max(S.mutator_chance or 0.30, f[2])
    S.mutator_chance_inc = math.max(S.mutator_chance_inc or 0.02, f[3])
    S.mutator_chance_max = math.max(S.mutator_chance_max or 0.60, f[4])
    if lvl == 3 then S.mutator_bosses = true end
    S.mutators, S.mutator_list = true, list
    print(string.format("ge: whacky: menu level %d: %d mutators may roll (from wave %d, chance %.2f)", lvl, #list, S.mutator_from, S.mutator_chance))
  end

  -- ---------------------------------------------------------------- hooks from arena.lua --
  function WH.spawned(c, spec, entry)
    local rec = { c = c, boss = entry.boss, weapon = spec.weapon, affixes = entry.affixes }
    WH.live[#WH.live + 1] = rec
    if base_look(rec) and rec.boss and rec.weapon == "goldengun" then
      call(TW("golden", "THE GOLDEN CHAMPION"), 4, TW("golden_sub", "BARON SAMEDI, IN GOLD"))
    end
  end

  local last_phase = nil
  function WH.tick(dt, elapsed, phase, wave)
    WH.elapsed, WH.wave = elapsed, wave
    if #WH.live > 40 then
      local keep = {}
      for _, r in ipairs(WH.live) do if spawn.alive(r.c) then keep[#keep + 1] = r end end
      WH.live = keep
    end
    gifts_tick()
    if last_phase == "fight" and phase == "breather" and S.whacky and S.whacky_reward ~= false and S.boss_every and S.boss_every > 0
       and wave % S.boss_every == 0 then
      WH.crowd_throw(GIFTS[#GIFTS])   -- a boss down: the crowd throws Bond the minion launcher
    end
    last_phase = phase
  end

  -- turn effect `name` on now, on the live enemies too (test keys)
  function WH.force(name)
    local d = DEFS[name]
    if not d or not MUT then return nil end
    MUT.wave_end("test key")
    for _, r in ipairs(WH.live) do
      if spawn.alive(r.c) then
        chrfx.clear(r.c)
        base_look(r)
      end
    end
    local t = MUT.apply(name, math.max(1, WH.wave), {})
    if d.spawned then
      for _, r in ipairs(WH.live) do if spawn.alive(r.c) and not r.boss then pcall(d.spawned, r.c, {}) end end
    end
    print(string.format("ge: whacky: TEST effect %s (%s) on %d live", name, tostring(t), #WH.live))
    return t
  end

  on("chr_died", function(victim, killer)
    if killer and killer >= 28000 and killer < 32000 then   -- a minion's kill (port_whacky.c chrnums)
      WH.minion_kills = WH.minion_kills + 1
      print(string.format("ge: whacky: MINION KILL chr %d by minion %d (%d minion kills)", victim, killer, WH.minion_kills))
      if WH.elapsed - WH.kill_call_at > 2.5 then
        WH.kill_call_at = WH.elapsed
        if WH.minion_kills % 5 == 0 then
          call(string.format(TW("minion_squad", "MINION SQUAD x%d"), WH.minion_kills), 4, TW("minion_squad_sub", "THE TINY KNIFE THROWERS ARE ON A ROLL"))
        else
          call(TW("minion_kill", "MINION KILL!"), 2, TW("minion_kill_sub", "A TINY KNIFE THROWER GOT ONE"))
        end
      end
    end
  end)

  on("stage_start", function() chrfx.clear_all() end)

  local TEST = {}
  for _, n in ipairs(WH.ORDER) do TEST[#TEST + 1] = n end
  for _, n in ipairs({ "dk", "paintball", "turbo", "knives" }) do if DEFS[n] then TEST[#TEST + 1] = n end end
  on("test_key", function(name)
    if not (S.test_keys or mode.env_flag("GE_ARENA_TESTKEYS")) then return end
    if name == "wfx+" or name == "wfx-" then
      if name == "wfx+" then WH.test_i = WH.test_i % #TEST + 1 else WH.test_i = (WH.test_i - 2) % #TEST + 1 end
      WH.force(TEST[WH.test_i])
    elseif name == "woff" then
      MUT.wave_end("test key")
      for _, r in ipairs(WH.live) do if spawn.alive(r.c) then chrfx.clear(r.c); base_look(r) end end
      call(TW("effect_off", "EFFECT OFF"), 1)
    elseif name == "wgun" then
      arm(0, 20)
      call(LAUNCHER .. "!", 3, string.format(LAUNCHER_SUB, 20))
    end
  end)

  WH.base_look = base_look
  ARENA_WHACKY.last = WH   -- test hook: the live instance
  print(string.format("ge: whacky: loaded (fx %s, %d mutators added)", tostring(FXON), #WH.ORDER))
  return WH
end
