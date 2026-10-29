-- Arena crowd favour (drop A8, modding/arena.md "Drop A8"). Shipped to Data/Scripts/modes/_arena_crowd.lua and included by arena.lua.
-- Defines the global ARENA_CROWD.new(S, H) -> CR (S = arena.lua's settings, H = a few hooks into arena.lua's state).
--
-- The crowd watches the fight: stylish kills win its favour (0..100, starts at 50, drifts slowly back to 50), dull or rude play loses
-- it. Its mood is on HUD line 4 ("CROWD 64 ROARING   HEADSHOT +4") and it acts on it:
--   0-19  HATED     the gates open early: an extra pack now (or the breather is cut and the next wave brings it); "REDEMPTION" when you
--                   climb out. No drift up while HATED: the crowd holds a grudge, you have to win it back.
--   20-39 RESTLESS  bored fidgets, boos (laughs)
--   40-59 WATCHING
--   60-79 ROARING   spectators throw ammo / armour onto the sand near you (real pickups)
--   80-100 ADORED   "a gift from a sponsor" in the centre now and then: the Golden Gun, rockets, full armour or brief double damage
-- At the last boss kill of a boss wave the emperor's thumb: high favour = every Bond healed in full + a gift; low = no breather.
-- Co-op: one shared favour; the toasts name the Bond who earned it ("P2 HEADSHOT +4").
--
-- Settings: ARENA_SETTINGS.crowd may be the spectator count (a number, as before) or a table:
--   crowd = { count = 100, favour = true, ... any of D below ... }        crowd = { favour = false } turns the favour off
--   (ARENA_SETTINGS.crowd_favour = false also turns it off.) Partial tables are fine: missing keys use D, and D.points merges by key.
-- Everything is sim state (the Lua heap, the session RNG, real pickups and modifiers), so co-op and netplay stay in step.

ARENA_CROWD = {}

-- the Bond theme (tools/arena_theme.lua, included by arena.lua first): every name the player sees. Internal labels stay as below (logs,
-- tests); the theme maps them for the toasts and callouts. Without the theme the old names show.
local T = type(ARENA_THEME) == "table" and ARENA_THEME or {}
local function theme_label(key) return (T.label and T.label(key)) or key end
local function TS(key, fallback) return (T.say and T.say[key]) or fallback end
local EV = T.events                -- the single-kill event callouts (nil without the theme: none)
local EVSUB = T.event_subs or {}
local ROCKET_ITEM = 25             -- ITEM_ROCKETLAUNCH (bondconstants.h): a blast kill while Bond holds it is his own rocket
local function rocket_held(p) local ok, w = pcall(bond.weapon, p); return ok and w == ROCKET_ITEM end

local D = {
  favour       = true,
  start        = 50,
  drift        = 0.25,      -- points a second back toward 50 (never up while HATED)
  tiers        = { 0, 20, 40, 60, 80 },
  tier_names   = T.tiers or { "HATED", "RESTLESS", "WATCHING", "ROARING", "ADORED" },   -- (internal tier numbers 1..5 keep their meaning)
  toast_secs   = 2.5,
  log          = true,      -- "ge: arena: CROWD ..." lines (tests read them)
  points = {
    headshot = 3, headshot_range_step = 1500, headshot_range_max = 3,   -- +1 per 1500 units of range, at most +3
    long_shot = 3, long_shot_dist = 3000,
    multi = 3, multi_step = 2, multi_max = 12, multi_window = 2.0,       -- the 2nd kill within 2 s: +3, the 3rd +5, ...
    streak_from = 3, streak_max = 4,                                     -- kills without taking damage: the 3rd +1, then +1 every 2 kills
    close_call = 5, close_call_health = 0.20,
    melee = 5,                                                           -- a fists / knife kill
    stolen = 1,                                                          -- THEIR OWN GUN: a kill with the very gun model the victim carried
                                                                         -- (picked up: not Bond's start loadout or a gift)
    blast = 5, blast_step = 2,                                           -- one rocket / grenade, 2+ kills: +5, then +2 more each
    disarm = 4,
    revenge = 3, revenge_window = 3.0,                                   -- kill the enemy who last hurt you within 3 s
    boss = 10, elite = 4, pistol = 6,                                    -- giant slayer (+6 more with a pistol)
    clean_wave = 6,                                                      -- a wave cleared with no Bond hurt
    -- losses (negative)
    hit_taken = -1, hit_cooldown = 0.4,
    camping = -2, camp_radius = 400, camp_secs = 10, camp_repeat = 5,
    spraying = -3, spray_shots = 30,
    crowd_shot = -8, crowd_cooldown = 1.0, panic_radius = 2000,
  },
  disarm_drop   = true,     -- a Bond's shot on a held gun knocks it out of the hand (the stock game only chips the gun)
  roar_every    = 25,       -- ROARING and up: seconds between gifts thrown from the stands
  roar_ammo     = { pp7 = 20, kf7 = 30, shotgun = 8 },
  roar_armour   = 0.5,
  roar_armour_chance = 0.3,
  roar_max      = 2,        -- thrown gifts on the floor at once
  roar_ttl      = 30,
  sponsor_cooldown = 75,    -- ADORED: seconds between sponsor gifts
  sponsor_list  = { "goldengun", "rocket", "armour", "double" },
  sponsor_ttl   = 45,
  golden_ammo   = 12,
  rocket_ammo   = 6,
  double_secs   = 15,       -- double damage: enemies take twice the damage (the enemy_health modifier at 50) for this long
  hated_penalty = "pack",   -- "pack" (an extra pack of rushers; in a breather the gates open early and the next wave brings it),
                            -- "elite" (the next wave's enemies one tier tougher), "off"
  hated_pack    = 4,
  thumb         = true,     -- the emperor's thumb at the last boss kill of a boss wave
  thumb_up      = 60,       -- at or above: full heal for every Bond + a gift
  thumb_down    = 40,       -- below: the next wave comes with no breather
  ripple_speed  = 5000,     -- units a second: reactions spread out from the stand nearest the action
  sfx           = true,     -- stock sounds by id: 160 BING (gifts), 54 EVIL_LAUGH (thumbs down), 161 ALARM1 (gates), 4 GRENADE_THROW (throw)
  -- the centre callouts (port_arenahud.c; features/arena-announcer-research.md): kill chains (kills within points.multi_window) and
  -- streaks (kills without being hurt). The names come from the Bond theme (arena_theme.lua); the level (1..6) sets the colour, size
  -- and pulse.
  callouts      = true,
  chain_names   = T.chain or { [2] = "TWIN STRIKE", [3] = "TRIPLE CROWN", [4] = "QUAD FURY", [5] = "BLOODBATH", [6] = "MASSACRE" },
  chain_more    = T.chain_more or "CARNAGE",   -- 7+: "<chain_more> x7"
  streak_names  = T.streak or { [5] = "THE CROWD STIRS", [10] = "CHAMPION OF THE PIT", [15] = "BLOODED", [20] = "UNBROKEN",
                    [25] = "IMMORTAL", [30] = "LEGEND OF THE ARENA" },   -- the [30] name repeats every 5 kills after 30
  streak_broken = 5,        -- a streak this long ended by a hit: the theme's streak_broken callout
  first_kill    = true,     -- the first kill of a run gets the theme's first_kill callout ("THE NAME'S BOND")
}
ARENA_CROWD.DEFAULTS = D

local function merge(base, over)
  local o = {}
  for k, v in pairs(base) do o[k] = v end
  if type(over) == "table" then
    for k, v in pairs(over) do
      if type(v) == "table" and type(base[k]) == "table" and k == "points" then o[k] = merge(base[k], v) else o[k] = v end
    end
  end
  return o
end

local PISTOLS = { pp7 = true, pp7sil = true, dd44 = true, cougar = true, klobb = true }

function ARENA_CROWD.new(S, H)
  local C = merge(D, type(S.crowd) == "table" and S.crowd or nil)
  if S.crowd_favour == false then C.favour = false end
  local P = C.points
  local X = { on = C.favour and true or false, favour = C.start, peak = C.start, low = C.start, gains = 0, losses = 0,
              gifts = 0, sponsors = 0, hated_times = 0, redemptions = 0, thumbs_up = 0, thumbs_down = 0, kills_scored = 0 }
  local now, tier, was_hated = 0, 3, false
  local toast, toast_t = nil, 0
  local per = {}                 -- per Bond: streak, chain, last kill time, anchor, camp time, shots, last attacker ...
  local start_kit = {}           -- per Bond: weapons carried at the start of wave 1
  local gifted = {}              -- weapon names the crowd / a sponsor gave
  local blasts = {}              -- blast serial -> { n = kills, t = time }
  local seat_pos = {}            -- spectator chr -> { x, z }
  local pit, centre = {}, {}     -- pad ids (pit floor; pit within 800 of the middle)
  local thrown = {}              -- live thrown gifts { h, born }
  local sponsor_item, sponsor_t = nil, -1e9
  local double_until = nil
  local roar_acc = 0
  local penalty_used_wave = -1
  local pending_pack, pending_elite, breather_cut = 0, false, nil
  local hurt_this_wave, wave_kills = false, 0

  local function log(fmt, ...) if C.log then print("ge: arena: CROWD " .. string.format(fmt, ...)) end end
  local function sfx(id) if C.sfx then pcall(arena.sfx, id) end end
  local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
  local function tier_of(f)
    local t = 1
    for i = 1, #C.tiers do if f >= C.tiers[i] then t = i end end
    return t
  end
  local function who(p) return (bond.count() > 1 and p ~= nil) and string.format("P%d ", p + 1) or "" end
  local function B(p)
    local b = per[p]
    if not b then
      b = { streak = 0, chain = 0, last_kill = -1e9, hit_t = -1e9, att = nil, att_t = -1e9, camp = 0, camp_next = 0, sprayed = 0,
            fired = nil, hits = nil, crowd_t = -1e9 }
      per[p] = b
    end
    return b
  end

  -- ---------------------------------------------------------------- reactions --
  local function seat(c)
    local s = seat_pos[c]
    if s == nil then
      local x, z = spawn.where(c)
      s = x and { x, z } or false
      seat_pos[c] = s
    end
    return s or nil
  end

  -- spectators react, nearest stand first: share (0..1) of them, kinds = { anim, ... }; the delay grows with the distance
  local function ripple(x, z, share, kinds)
    local list = H.crowd
    if not list or #list == 0 or not x then return end
    for _, s in ipairs(list) do
      local p = seat(s.chr)
      if p then
        local d = math.sqrt((p[1] - x) ^ 2 + (p[2] - z) ^ 2)
        local w = share * math.max(0.15, 1 - d / 9000)
        if math.random() < w then
          s.react = kinds[math.random(#kinds)]
          s.react_at = now + d / C.ripple_speed + math.random() * 0.4
        end
      end
    end
  end

  local function set_toast(text) toast, toast_t = text, C.toast_secs end
  -- a centre callout (port_arenahud.c): the newest replaces the one showing. The crowd's own cheer (arena.lua react_kill: the WAVs /
  -- screams) is the sound; the callout adds none.
  local function callout(text, level, sub)
    if not C.callouts or not arena.callout then return end
    if T.callout then T.callout(text, level, sub) else pcall(arena.callout, text, level, sub) end   -- (the theme adds the sounds)
    log("CALLOUT %s (level %d)%s", text, level, sub and sub ~= "" and (" " .. sub) or "")
  end

  -- -------------------------------------------------------------- the meter --
  local on_tier_change   -- forward

  local function round(d) return d >= 0 and math.floor(d + 0.5) or -math.floor(-d + 0.5) end

  -- label: the internal label (the log keeps it); shown: the player's text (default: the theme's text for label)
  local function change(delta, label, p, x, z, shown)
    if not X.on or delta == 0 then return end
    local before = X.favour
    X.favour = clamp(X.favour + delta, 0, 100)
    if delta > 0 then X.gains = X.gains + delta else X.losses = X.losses - delta end
    X.peak, X.low = math.max(X.peak, X.favour), math.min(X.low, X.favour)
    set_toast(string.format("%s%s %+d", who(p), shown or theme_label(label), round(delta)))
    log("%+d %s (player %s) %.1f -> %.1f %s", round(delta), label, tostring(p), before, X.favour,
      C.tier_names[tier_of(X.favour)])
    if x then
      if delta >= 6 then ripple(x, z, 0.55, { "dance", "surrender", "dance" })
      elseif delta > 0 then ripple(x, z, 0.12 + delta * 0.05, { "laugh", "dance" })
      else ripple(x, z, 0.25, { "laugh", "look" }) end
    end
    local t = tier_of(X.favour)
    if t ~= tier then on_tier_change(tier, t, p) end
  end

  -- ------------------------------------------------------------ the pickups --
  local function dist2(a, b, x, z) return (a - x) ^ 2 + (b - z) ^ 2 end
  local function taken(pad)
    local items = (type(ARENA_CENTRE_ITEMS) == "function") and ARENA_CENTRE_ITEMS() or {}
    for _, it in ipairs(items) do if it.pad == pad then return true end end
    for _, g in ipairs(thrown) do if g.pad == pad then return true end end
    if sponsor_item and sponsor_item.pad == pad then return true end
    return false
  end
  -- a pit pad 250..800 units from Bond p (gifts land near him), else any free pit pad near the middle
  local function pad_near(p)
    local bx, _, bz = bond.pos(p)
    local cand = {}
    if bx then
      for _, id in ipairs(pit) do
        local x, _, z = pads.pos(id)
        local d = dist2(x, z, bx, bz)
        if d > 250 * 250 and d < 800 * 800 and not taken(id) then cand[#cand + 1] = id end
      end
    end
    if #cand == 0 then for _, id in ipairs(centre) do if not taken(id) then cand[#cand + 1] = id end end end
    if #cand == 0 then return nil end
    return cand[math.random(#cand)]
  end

  local function throw_gift(p, why)
    if #thrown >= C.roar_max then return false end
    local pad = pad_near(p)
    if not pad then return false end
    local armour = math.random() < C.roar_armour_chance
    local ok, h
    if armour then ok, h = pcall(spawn.item, pad, "armour", { amount = C.roar_armour })
    else ok, h = pcall(spawn.item, pad, "ammo", { ammo = C.roar_ammo }) end
    if not ok or not h then log("gift could not be placed (%s)", tostring(h)); return false end
    pcall(spawn.item_marker, h, "gift", "GIFT")
    thrown[#thrown + 1] = { h = h, pad = pad, born = now }
    X.gifts = X.gifts + 1
    local x, _, z = pads.pos(pad)
    ripple(x, z, 0.5, { "dance", "laugh" })
    sfx(4)
    if armour then H.say(string.format(TS("gift_armour", "THE CROWD THROWS ARMOUR%s"), why and (" - " .. why) or ""), 3)
    else H.say(string.format(TS("gift_ammo", "THE CROWD THROWS AMMO%s"), why and (" - " .. why) or ""), 3) end
    log("GIFT thrown: %s on pad %d near player %d (%s)", armour and "armour" or "ammo", pad, p, why or "roaring")
    return true
  end

  local function double_damage(on)
    if on then
      if H.mutator_active() or double_until then return false end
      mods.set("enemy_health", 50)
      double_until = now + C.double_secs
      H.say(string.format(TS("double", "A GIFT FROM A SPONSOR: DOUBLE DAMAGE FOR %d SECONDS"), C.double_secs), 5)
      log("SPONSOR double damage on (enemy_health %s)", tostring(mods.get("enemy_health")))
      return true
    elseif double_until then
      mods.set("enemy_health", 100)
      double_until = nil
      log("SPONSOR double damage off (enemy_health %s)", tostring(mods.get("enemy_health")))
    end
  end

  function X.double_on() return double_until ~= nil end   -- (arena plus: the modifier is shared with Q DAMAGE)

  local function sponsor_gift(force)
    if not force and now - sponsor_t < C.sponsor_cooldown then return false end
    if sponsor_item and spawn.item_alive(sponsor_item.h) then return false end
    local list = {}
    for _, v in ipairs(C.sponsor_list) do
      if v ~= "double" or (not H.mutator_active() and not double_until) then list[#list + 1] = v end
    end
    if #list == 0 then return false end
    local what = list[math.random(#list)]
    sponsor_t = now
    X.sponsors = X.sponsors + 1
    sfx(160)
    if what == "double" then return double_damage(true) end
    local cand = {}
    for _, id in ipairs(centre) do if not taken(id) then cand[#cand + 1] = id end end
    if #cand == 0 then return false end
    local pad = cand[math.random(#cand)]
    local ok, h, name
    if what == "goldengun" then ok, h = pcall(spawn.item, pad, "goldengun", { ammo = C.golden_ammo }); name = "THE GOLDEN GUN"
    elseif what == "rocket" then ok, h = pcall(spawn.item, pad, "rocket", { ammo = C.rocket_ammo }); name = "ROCKETS"
    else ok, h = pcall(spawn.item, pad, "armour", { amount = 1.0 }); name = "FULL ARMOUR" end
    if not ok or not h then log("sponsor gift %s could not be placed (%s)", what, tostring(h)); return false end
    if what ~= "armour" then gifted[what] = true end
    pcall(spawn.item_marker, h, "gift", "SPONSOR")
    sponsor_item = { h = h, pad = pad, born = now, what = what }
    local x, _, z = pads.pos(pad)
    ripple(x, z, 0.6, { "dance", "surrender" })
    H.say(string.format(TS("sponsor", "A GIFT FROM A SPONSOR: %s IN THE CENTRE"), name), 5)
    log("SPONSOR gift %s on pad %d", what, pad)
    return true
  end

  -- ---------------------------------------------------------------- tiers --
  local function hated_penalty()
    if C.hated_penalty == "off" or penalty_used_wave == H.wave() then return end
    penalty_used_wave = H.wave()
    X.hated_times = X.hated_times + 1
    if C.hated_penalty == "elite" then
      pending_elite = true
      H.say(TS("hated_elite", "THE CROWD WANTS BLOOD - THE NEXT WAVE IS ELITE"), 4)
      log("HATED penalty: next wave elite")
    elseif H.phase() == "fight" then
      H.extra_pack(C.hated_pack)
      H.say(TS("hated_now", "THE CROWD WANTS BLOOD - THE GATES OPEN"), 4)
      log("HATED penalty: an extra pack of %d now (wave %d)", C.hated_pack, H.wave())
    else
      pending_pack = pending_pack + C.hated_pack
      breather_cut = 2
      H.say(TS("hated_early", "THE CROWD WANTS BLOOD - THE GATES OPEN EARLY"), 4)
      log("HATED penalty: the gates open early, a pack of %d with the next wave", C.hated_pack)
    end
    sfx(161)
  end

  on_tier_change = function(old, new, p)
    tier = new
    log("tier %s -> %s (%.1f)", C.tier_names[old], C.tier_names[new], X.favour)
    if new == 1 then
      was_hated = true
      ripple(0, 0, 0.5, { "laugh", "look" })
      hated_penalty()
    elseif old == 1 and new >= 2 and was_hated then
      was_hated = false
      X.redemptions = X.redemptions + 1
      set_toast(who(p) .. theme_label("REDEMPTION") .. "!")
      H.say(TS("redemption", "REDEMPTION! THE CROWD FORGIVES YOU"), 3)
      ripple(0, 0, 0.5, { "dance", "surrender" })
      log("REDEMPTION")
    end
    if new >= 4 and old < 4 then ripple(0, 0, 0.45, { "dance", "laugh" }) end
    if new == 5 and old < 5 then sponsor_gift(false) end
  end

  -- -------------------------------------------------------------- the kills --
  -- arena.lua's chr_died hook, before the kill heal: e = its enemy record (boss, tier), elite = an affix elite, bosses_left = bosses
  -- still alive or queued in this wave
  function X.kill(victim, killer, e, elite, bosses_left)
    if not X.on then return end
    local judged = X.favour   -- the emperor's thumb goes by the mood as the boss falls, before this kill's own applause
    local part, weapon, hp, dist, age, blast = spawn.last_hit(victim)
    local p
    if part and hp ~= nil and age <= 3 then p = hp
    elseif killer ~= nil and killer ~= CHR_NONE then p = bond.player(killer) end
    local x, z = spawn.where(victim)
    if p == nil then
      if x then ripple(x, z, 0.08, { "laugh" }) end
      return
    end
    local b = B(p)
    local total, labels, parts = 0, {}, {}
    local function add(n, l)
      if n and n ~= 0 then total = total + n; labels[#labels + 1] = l; parts[#parts + 1] = { n = n, l = l, i = #parts } end
    end
    local isblast = part == "blast"
    if part == "head" and not isblast then
      add(P.headshot + math.min(P.headshot_range_max, math.floor((dist or 0) / P.headshot_range_step)), "HEADSHOT")
    end
    if not isblast and (dist or 0) >= P.long_shot_dist then add(P.long_shot, "LONG SHOT") end
    if weapon == "fist" then add(P.melee, "BARE HANDS") elseif weapon == "knife" then add(P.melee, "KNIFE KILL") end
    -- THEIR OWN GUN: the killing shot came from the very gun model the victim carried (either hand; still counts after a disarm or
    -- when Bond picked the dropped gun up), and Bond did not start with that gun or get it as a crowd gift. Compared by weapon, not
    -- by ammo type: an RC-P90 kill on a D5K soldier (both 9 mm) is not "their own gun".
    if weapon and not isblast and weapon ~= "fist" and weapon ~= "knife" and weapon ~= "other" and start_kit[p] and not start_kit[p][weapon]
       and not gifted[weapon] then
      local g1, g2 = spawn.owned_guns(victim)
      if weapon == g1 or weapon == g2 then add(P.stolen, "THEIR OWN GUN") end
    end
    local health = bond.vitals(p)
    if health and health < P.close_call_health then add(P.close_call, "CLOSE CALL") end
    if b.att == victim and now - b.att_t <= P.revenge_window then add(P.revenge, "REVENGE") end
    if e and e.boss then add(P.boss, "GIANT SLAYER") elseif elite then add(P.elite, "ELITE SLAIN") end
    if (e and e.boss or elite) and weapon and PISTOLS[weapon] then add(P.pistol, "WITH A PISTOL") end
    if now - b.last_kill <= P.multi_window then b.chain = b.chain + 1 else b.chain = 1 end
    b.last_kill = now
    local chain_name
    if b.chain >= 2 then
      chain_name = C.chain_names[b.chain] or (C.chain_more .. " x" .. b.chain)
      add(math.min(P.multi_max, P.multi + P.multi_step * (b.chain - 2)), chain_name)
    end
    b.streak = b.streak + 1
    if b.streak >= P.streak_from then add(math.min(P.streak_max, 1 + (b.streak - P.streak_from) // 2), "STREAK " .. b.streak) end
    -- the centre callout: the chain tier, else a streak milestone (a milestone on the same kill goes under the chain)
    local streak_name = C.streak_names[b.streak] or (b.streak > 30 and b.streak % 5 == 0 and C.streak_names[30]) or nil
    local blast_n = 0
    if isblast and blast and blast > 0 then
      local bl = blasts[blast]
      if not bl or now - bl.t > 3 then bl = { n = 0, t = now }; blasts[blast] = bl end
      bl.n = bl.n + 1
      blast_n = bl.n
      if bl.n >= 2 then add(P.blast + P.blast_step * (bl.n - 2), "BOOM x" .. bl.n) end
    end
    if blast_n >= 2 and EV and EV.blast then
      -- one blast, several kills: the blast's own callout beats the chain those kills also make
      callout(EV.blast, math.min(6, blast_n), who(p) .. blast_n .. " IN ONE BLAST")
    elseif chain_name then
      callout(chain_name, math.min(6, b.chain - 1), who(p) .. (streak_name and (streak_name .. "  ") or "") .. theme_label("STREAK") .. " " .. b.streak)
    elseif streak_name then
      callout(streak_name, math.min(6, 1 + b.streak // 5), who(p) .. string.format(T.streak_sub or "%d KILLS UNTOUCHED", b.streak))
    elseif EV then
      -- a single-kill event (the theme's events table), the rarest first; the sub line is the crowd's points for the kill
      local ev
      if X.kills_scored == 0 and C.first_kill then ev = "first_kill"
      elseif weapon == "goldengun" then ev = "golden_gun"
      elseif isblast and rocket_held(p) then ev = "rocket"
      elseif weapon == "knife" then ev = "knife"
      elseif weapon == "fist" then ev = "melee"
      elseif part == "head" and not isblast then ev = "headshot"
      elseif not isblast and (dist or 0) >= P.long_shot_dist then ev = "long_shot"
      elseif health and health < P.close_call_health then ev = "low_health"
      else
        for _, l in ipairs(labels) do if l == "THEIR OWN GUN" then ev = "own_gun" end end
      end
      if ev and EV[ev] then
        callout(EV[ev], ev == "first_kill" and 2 or 1, who(p) .. (total > 0 and string.format("+%d CROWD", total) or ""))
      end
    end
    X.kills_scored = X.kills_scored + 1
    wave_kills = wave_kills + 1
    if total ~= 0 then
      -- keep the toast short: the two biggest labels
      table.sort(parts, function(a, b2) if a.n ~= b2.n then return a.n > b2.n end return a.i < b2.i end)
      local label, shown = parts[1].l, theme_label(parts[1].l)
      if #parts >= 2 then label, shown = label .. " + " .. parts[2].l, shown .. " + " .. theme_label(parts[2].l) end
      if #parts > 2 then label, shown = label .. "...", shown .. "..." end
      log("kill chr %d by player %d: %s (part %s, weapon %s, dist %.0f%s)", victim, p, table.concat(labels, ", "), tostring(part),
        tostring(weapon), dist or 0, blast and blast > 0 and (", blast " .. blast) or "")
      change(total, label, p, x, z, shown)
    elseif x then
      ripple(x, z, 0.06, { "laugh" })
    end
    if e and e.boss and (bosses_left or 0) == 0 and C.thumb then X.thumb(p, judged) end
  end

  -- the emperor's thumb: the last boss of a boss wave is down
  function X.thumb(p, f)
    f = f or X.favour
    if f >= C.thumb_up then
      X.thumbs_up = X.thumbs_up + 1
      for q = 0, bond.count() - 1 do if H.bond_ok(q) then bond.heal(q, 1.0, 0) end end
      H.say(TS("thumb_up", "THE EMPEROR: THUMBS UP! FULL HEALTH AND A GIFT"), 5)
      log("THUMB UP at %.1f: every Bond healed, a gift", f)
      ripple(0, 0, 0.7, { "dance", "surrender" })
      sfx(160)
      if not throw_gift(p or 0, TS("emperor_gift", "THE EMPEROR'S GIFT")) then sponsor_gift(true) end
    elseif f < C.thumb_down then
      X.thumbs_down = X.thumbs_down + 1
      breather_cut = 1
      H.say(TS("thumb_down", "THE EMPEROR: THUMBS DOWN - NO MERCY, NO BREATHER"), 5)
      log("THUMB DOWN at %.1f: no breather", f)
      ripple(0, 0, 0.5, { "laugh", "look" })
      sfx(54)
    else
      H.say(TS("thumb_side", "THE EMPEROR IS UNMOVED"), 3)
      log("THUMB sideways at %.1f", f)
    end
  end

  -- -------------------------------------------------------------- the waves --
  function X.wave_start(w, queue, boss_wave)
    hurt_this_wave, wave_kills = false, 0
    breather_cut = nil
    if double_until then double_damage(false) end   -- before the mutator roll: the two never share enemy_health
    if not next(start_kit) then
      local names = spawn.weapons()
      for p = 0, bond.count() - 1 do
        start_kit[p] = {}
        local kit = {}
        for _, n in ipairs(names) do
          local has = bond.ammo(p, n)
          if has then start_kit[p][n] = true; kit[#kit + 1] = n end
        end
        log("start kit player %d: %s", p, #kit > 0 and table.concat(kit, " ") or "(none)")
      end
    end
    if not X.on then return end
    if pending_pack > 0 then
      H.extra_pack(pending_pack, queue)
      log("wave %d: +%d from the HATED penalty", w, pending_pack)
      pending_pack = 0
    end
    if pending_elite then
      pending_elite = false
      local n = 0
      for _, e in ipairs(queue) do
        if not e.boss then
          e.spec.health = (e.spec.health or 1) * 1.3
          e.spec.armour = (e.spec.armour or 0) + 1.5 * (e.spec.health or 1)
          n = n + 1
        end
      end
      log("wave %d: ELITE from the HATED penalty (%d enemies one tier tougher)", w, n)
    end
  end

  function X.wave_cleared(w, boss_wave)
    if not X.on then return end
    if not hurt_this_wave and wave_kills > 0 then
      local bx, _, bz = bond.pos(0)
      change(P.clean_wave, "CLEAN WAVE", nil, bx, bz)
      if EV and EV.clean_wave then callout(EV.clean_wave, 2, string.format("WAVE %d - NOBODY HURT", w)) end
    elseif EV and EV.last_enemy and wave_kills > 0 then   -- the wave's last enemy down
      callout(EV.last_enemy, 2, string.format(EVSUB.last_enemy or "WAVE %d CLEARED", w))
    end
    log("wave %d over: favour %.1f %s (peak %.1f, low %.1f), gifts %d, sponsors %d", w, X.favour, C.tier_names[tier], X.peak, X.low, X.gifts,
      X.sponsors)
  end

  -- arena.lua asks how long this breather is (the thumbs down / HATED cut it)
  function X.breather(normal) return breather_cut or normal end

  -- ------------------------------------------------------------- the losses --
  on("damage", function(victim, attacker, amount, weapon)
    if not X.on or victim > -2 or amount <= 0 then return end
    local p = -2 - victim
    if p < 0 or p > 3 then return end
    local b = B(p)
    hurt_this_wave = true
    if b.streak >= C.streak_broken then
      callout((EV and EV.streak_broken) or "STREAK BROKEN", 1, who(p) .. string.format(EVSUB.streak_broken or "STREAK %d ENDED", b.streak))
    end
    b.streak = 0
    if attacker and attacker >= 0 then b.att, b.att_t = attacker, now end
    if now - b.hit_t >= P.hit_cooldown then
      b.hit_t = now
      change(P.hit_taken, "HIT", p)
    end
  end)

  local function poll_hits()
    local evs = spawn.hit_events()
    for _, ev in ipairs(evs) do
      local b = B(ev.player)
      if ev.kind == "disarm" then
        local ok = true
        if C.disarm_drop then ok = spawn.disarm(ev.chr) end
        if ok then
          local x, z = spawn.where(ev.chr)
          change(P.disarm, "DISARMED", ev.player, x, z)
        end
      elseif ev.kind == "crowd" and now - b.crowd_t >= P.crowd_cooldown then
        b.crowd_t = now
        local s = seat(ev.chr)
        change(P.crowd_shot, "SHOT THE CROWD", ev.player)
        if s and H.crowd then   -- the spectators around the one that was hit panic
          local n = 0
          for _, sp in ipairs(H.crowd) do
            local q = seat(sp.chr)
            if q and (q[1] - s[1]) ^ 2 + (q[2] - s[2]) ^ 2 < P.panic_radius ^ 2 then
              sp.react = (math.random() < 0.5) and "startled" or "surrender"
              sp.react_at = now + math.random() * 0.3
              n = n + 1
            end
          end
          log("panic: spectator %d was hit, %d around it panic", ev.chr, n)
        end
      end
    end
  end

  local function watch_bonds(dt, fighting)
    for p = 0, bond.count() - 1 do
      local b = B(p)
      if H.bond_ok(p) then
        local x, _, z = bond.pos(p)
        -- camping: inside camp_radius for camp_secs while enemies are on the floor
        if x and fighting then
          if not b.ax or dist2(x, z, b.ax, b.az) > P.camp_radius ^ 2 then
            b.ax, b.az, b.camp, b.camp_next = x, z, 0, P.camp_secs
          else
            b.camp = b.camp + dt
            if b.camp >= b.camp_next then
              b.camp_next = b.camp_next + P.camp_repeat
              change(P.camping, "CAMPING", p, x, z)
            end
          end
        else
          b.ax = nil
        end
        -- spraying: many shots and not one hit
        local fired, hits = bond.shots(p)
        if fired then
          if b.fired then
            if hits > b.hits then b.sprayed = 0
            else b.sprayed = b.sprayed + math.max(0, fired - b.fired) end
            if b.sprayed >= P.spray_shots then
              b.sprayed = 0
              change(P.spraying, "SPRAY AND PRAY", p)
            end
          end
          b.fired, b.hits = fired, hits
        end
      end
    end
  end

  -- every arena tick (0.25 s)
  function X.tick(dt, t, phase)
    now = t
    if toast_t > 0 then toast_t = toast_t - dt; if toast_t <= 0 then toast = nil end end
    if double_until and now >= double_until then double_damage(false) end
    if not X.on then return end
    poll_hits()
    watch_bonds(dt, phase == "fight" and H.alive() > 0)
    -- drift back toward 50 (a HATED crowd holds its grudge)
    if X.favour > C.start then X.favour = math.max(C.start, X.favour - C.drift * dt)
    elseif X.favour < C.start and tier > 1 then X.favour = math.min(C.start, X.favour + C.drift * dt) end
    local t2 = tier_of(X.favour)
    if t2 ~= tier then on_tier_change(tier, t2, nil) end
    -- the gifts: thrown ones expire; ROARING and up throw more; ADORED calls a sponsor
    local keep = {}
    for _, g in ipairs(thrown) do
      if not spawn.item_alive(g.h) then log("GIFT collected after %.0fs", now - g.born)
      elseif now - g.born > C.roar_ttl then spawn.item_remove(g.h); log("GIFT expired")
      else keep[#keep + 1] = g end
    end
    thrown = keep
    if sponsor_item then
      if not spawn.item_alive(sponsor_item.h) then log("SPONSOR gift %s collected after %.0fs", sponsor_item.what, now - sponsor_item.born); sponsor_item = nil
      elseif now - sponsor_item.born > C.sponsor_ttl then spawn.item_remove(sponsor_item.h); log("SPONSOR gift expired"); sponsor_item = nil end
    end
    if H.wave() >= 1 and tier >= 4 and phase ~= "delay" then
      roar_acc = roar_acc + dt
      if roar_acc >= C.roar_every then
        roar_acc = 0
        local ps = {}
        for p = 0, bond.count() - 1 do if H.bond_ok(p) then ps[#ps + 1] = p end end
        if #ps > 0 then throw_gift(ps[math.random(#ps)], nil) end
      end
    else
      roar_acc = 0
    end
    if tier == 5 and phase ~= "delay" then sponsor_gift(false) end
  end

  -- spectator fidgets by mood (arena.lua's crowd_tick picks from this list)
  local MOODS = {
    { "laugh", "look", "cough", "yawn", "laugh" },                      -- HATED: mocking
    { "yawn", "scratch_leg", "scratch_butt", "flies", "look", "cough" }, -- RESTLESS: bored
    nil,                                                                 -- WATCHING: the stock fidgets
    { "dance", "laugh", "chat", "surrender", "listen" },                -- ROARING
    { "dance", "surrender", "dance", "laugh" },                         -- ADORED: chanting, hands up
  }
  function X.fidgets() if not X.on then return nil end return MOODS[tier] end

  -- the wave panel's crowd block (port_arenahud.c): favour, tier, tier name, toast, double damage seconds left
  function X.panel_info()
    if not X.on then return nil end
    return math.floor(X.favour + 0.5), tier, C.tier_names[tier], toast or "",
      double_until and math.max(0, math.floor(double_until - now + 0.5)) or 0
  end

  function X.hud()
    if not X.on then return nil end
    local s = string.format("CROWD %d %s", math.floor(X.favour + 0.5), C.tier_names[tier])
    if double_until then s = s .. string.format("  2X DAMAGE %ds", math.max(0, math.floor(double_until - now + 0.5))) end
    if toast then s = s .. "   " .. toast end
    return s:sub(1, 63)
  end

  function X.finish(win)
    if double_until then double_damage(false) end
    if not X.on then return nil end
    log("summary: favour %.1f, peak %.1f, low %.1f, +%.0f / -%.0f, gifts %d, sponsors %d, HATED %d, redemptions %d, thumbs up %d down %d, kills scored %d",
      X.favour, X.peak, X.low, X.gains, X.losses, X.gifts, X.sponsors, X.hated_times, X.redemptions, X.thumbs_up, X.thumbs_down, X.kills_scored)
    return string.format("Crowd: peak %d   gifts %d   sponsors %d", math.floor(X.peak + 0.5), X.gifts, X.sponsors)
  end

  -- test helpers
  function X.set(f) X.favour = clamp(f, 0, 100); local t = tier_of(X.favour); if t ~= tier then on_tier_change(tier, t, nil) end end
  -- the crowd's mood moves for a reason that is nobody's doing (the weather): delta favour, the toast's text
  function X.mood(delta, shown) change(delta, "mood", nil, nil, nil, shown or "THE CROWD") end
  function X.tier() return tier, C.tier_names[tier] end
  function X.sponsor(force) return sponsor_gift(force) end
  function X.throw(p) return throw_gift(p or 0, nil) end
  function X.live_items()   -- { {h=, kind="gift"|"sponsor", what=}, ... }
    local l = {}
    for _, g in ipairs(thrown) do l[#l + 1] = { h = g.h, kind = "gift" } end
    if sponsor_item then l[#l + 1] = { h = sponsor_item.h, kind = "sponsor", what = sponsor_item.what } end
    return l
  end
  function X.double_left() return double_until and (double_until - now) or 0 end
  X.C = C

  on("stage_start", function()
    pit, centre = {}, {}
    for i = 0, pads.count() - 1 do
      local x, y, z = pads.pos(i)
      if x and y <= 1 and math.abs(x) < 4300 and math.abs(z) < 4300 and math.abs(x) + math.abs(z) < 6000 then
        pit[#pit + 1] = i
        if x * x + z * z <= 800 * 800 then centre[#centre + 1] = i end
      end
    end
    tier = tier_of(X.favour)
    log("favour %s: start %d, %d pit pads, %d centre pads", X.on and "on" or "off", C.start, #pit, #centre)
  end)

  return X
end
