-- Arena plus, batch A (modding/arena.md "Arena plus"). Shipped to Data/Scripts/modes/_arena_plus.lua and included by arena.lua.
-- Defines the global ARENA_PLUS.new(S, H) -> P (S = arena.lua's settings, H = a few hooks into arena.lua's state). Four features, each a
-- start-menu row and an ARENA_SETTINGS key (default on); every balance number is in D below (ARENA_SETTINGS.plus = { key = value } overrides):
--   credits    (S.credits)   the currency: every kill pays by the enemy's tier, more as the waves climb, with bonuses for the stylish kills
--                            (headshot, long shot, bare hands / knife, a multi-kill, a close call, a blast with 2+ kills). On the panel and the debrief.
--   powerups   (S.powerups)  a kill has a small chance (4%) to drop one power-up (never two on the field) as a gold bar / bank note on the
--                            sand with a marker and a label, for 20 s; walk over it. MAX AMMO (every carried gun full), INSTA-KILL (20 s: every hit
--                            kills a non-boss; bosses take 4x), DOUBLE CREDITS (20 s), NUKE (every live non-boss enemy dies), Q DAMAGE (20 s, 4x).
--                            Timed ones show a chip on the panel. The floor visual is the stock loot prop (spawn.item "goldbar" / "money").
--   crate      (S.crate)     Q waits at a fixed spot in the pit. USE near him pays the price for a random weapon (with a full clip, drawn into
--                            Bond's hand): 10% a dud (the credits are lost, Q quips), 2% the Golden Gun, else a weighted table.
--   cards      (S.cards)     after every cleared wave but the last, the game holds (plan.hold) and shows three Requisition cards: one is taken,
--                            it stacks for the run. The generic ui layer draws them (tools/ui_menu.lua). Automated runs (GE_FRAMES,
--                            GE_PLAYERBOT, GE_INPUTPLAY) and netplay take a random card at once; GE_ARENA_CARDS=ui keeps the menu in an automated run.
-- Everything is sim state (the Lua heap, the session RNG, mods.set, real pickups), so co-op and netplay stay in step. What is drawn
-- (the panel, the callouts, the AR markers, the card view) is presentation only. Credits and cards are one pool for the party in co-op.

ARENA_PLUS = ARENA_PLUS or {}
pcall(include, "_ui_menu")   -- the generic menu layer draws the cards (a build without it takes a card at random)

local T =(type(ARENA_THEME) == "table" and ARENA_THEME.plus) or {}
local TP = T.powerups or {}
local TC = T.crate or {}
local TK = T.cards or {}

local D = {
  -- credits: per kill by the enemy's tier, x (1 + credit_growth * (wave - 1)); the bonuses are flat
  credit_kill   = { light = 5, normal = 8, armoured = 12, heavy = 18, boss = 80 },
  credit_growth = 0.04,
  credit_head   = 4,     -- a headshot
  credit_long   = 4,     -- a kill from 3000 units or more
  credit_long_dist = 3000,
  credit_melee  = 5,     -- a bare-hands or knife kill
  credit_multi  = 3,     -- the 2nd kill inside multi_window seconds; +3 more for each after it, at most credit_multi_max
  credit_multi_max = 12,
  multi_window  = 2.0,
  credit_close  = 5,     -- a kill with Bond under close_health
  close_health  = 0.20,
  credit_blast  = 3,     -- each kill after the first of one blast
  -- power-ups
  pu_chance     = 0.04,  -- per kill
  pu_chance_boss = 0.25, -- a boss kill
  pu_ttl        = 20,    -- seconds on the floor
  pu_secs       = 20,    -- how long a timed one lasts
  pu_weights    = { maxammo = 3, insta = 2, double = 3, nuke = 1.5, qdamage = 2.5 },
  pu_order      = { "maxammo", "insta", "double", "nuke", "qdamage" },
  qdamage_mult  = 4,     -- Q DAMAGE and the insta-kill's boss damage: enemy_health 100 / 4 (the modifier's floor is 25)
  -- the Q-Branch crate
  crate_price_base = 30,       -- price = base + per_wave * wave, to the nearest 5; about 1 to 2 waves of normal play
  crate_price_per_wave = 26,
  crate_radius  = 330,         -- units from Q within which USE rolls and the price shows
  crate_dud     = 0.10,
  crate_golden  = 0.02,
  crate_spot    = { -2600, 2600 },   -- the pit pad nearest this point (x, z) holds Q; fixed for the run and every run
  crate_skin    = "scientist_m",
  -- { gun, weight, minimum rounds given }; the rounds are the gun's magazine (weapon.get magsize) or the minimum if that is more
  crate_table   = { { "dd44", 10, 7 }, { "klobb", 10, 20 }, { "kf7", 9, 30 }, { "zmg", 9, 32 }, { "d5k", 9, 30 }, { "ar33", 8, 30 },
                    { "cougar", 7, 6 }, { "phantom", 6, 50 }, { "shotgun", 7, 8 }, { "autoshot", 5, 8 }, { "rcp90", 5, 80 },
                    { "sniper", 4, 8 }, { "grenadelauncher", 3, 6 }, { "rocket", 3, 4 }, { "laser", 1, 20 } },
  crate_golden_rounds = 8,
  -- Requisition cards
  cards_offered = 3,
  cards_pause_ok = true,
  card = {   -- per stack: the numbers each card uses (max = the most stacks)
    clips    = { max = 4, mag = 0.5 },            -- +50% magazine size per stack (of the stock size)
    armour   = { max = 1 },                       -- armour to full at the start of each wave (and now)
    medic    = { max = 4, rate = 0.006 },         -- fraction of a full health bar a second per stack
    vampire  = { max = 4, heal = 0.0025, cap = 0.03 },   -- fraction of a bar per hit landed per stack (at most `cap` a tick)
    batteries = { max = 4, more = 0.5 },          -- timed power-ups last 50% longer per stack
    airdrop  = { max = 4, more = 0.75 },          -- +75% power-up drop chance per stack
    discount = { max = 4, less = 0.20 },          -- -20% crate price per stack
    briefing = { max = 3, first = 5, more = 3 },  -- seconds of Q damage at the start of every wave: 5, 8, 11
    swiss    = { max = 4, rate = 0.08, cap = 40 },-- interest after each wave: 8% of the credits per stack, at most 40 per stack
    cache    = { max = 4, frac = 0.25 },          -- ammo for every carried gun at the start of every wave, per stack
    bounty   = { max = 4, more = 0.25 },          -- +25% credits per kill per stack
  },
  card_order = { "clips", "armour", "medic", "vampire", "batteries", "airdrop", "discount", "briefing", "swiss", "cache", "bounty" },
}
ARENA_PLUS.DEFAULTS = D

local function merge(base, over)
  local o = {}
  for k, v in pairs(base) do o[k] = v end
  if type(over) == "table" then
    for k, v in pairs(over) do
      if type(v) == "table" and type(base[k]) == "table" and k ~= "crate_table" and k ~= "crate_spot" and k ~= "pu_order" and k ~= "card_order" then
        o[k] = merge(base[k], v)
      else
        o[k] = v
      end
    end
  end
  return o
end

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

function ARENA_PLUS.new(S, H)
  local C = merge(D, S.plus)
  C.credits  = S.credits ~= false
  C.powerups = S.powerups ~= false
  C.crate    = S.crate ~= false and C.credits       -- the crate is paid in credits
  C.cards    = S.cards ~= false
  local P = { on = C.credits or C.powerups or C.crate or C.cards, cfg = C }
  local now = 0                                      -- the plus clock: stops while the cards hold the run
  local credits, earned, spent = 0, 0, 0
  local stack = {}                                   -- card id -> stacks
  local picked = {}                                  -- card ids in the order taken
  local pu                                           -- the power-up on the floor: { h, kind, born, x, y, z, label }
  local until_ = { insta = -1, double = -1, qdamage = -1 }    -- the timed ones' end (plus clock)
  local drops, collected, expired, nukes, nuked = 0, 0, 0, 0, 0
  local rolls, duds, goldens, kills_paid = 0, 0, 0, 0
  local kill_times = {}                              -- recent kills (multi-kill window)
  local blasts = {}                                  -- blast serial -> kills so far
  local nuking = false
  local kq = {}                                      -- insta-kill victims waiting for the next game tick
  local floor_pads = {}                              -- pads on the pit floor { id, x, y, z }
  local q = nil                                      -- { chr, pad, x, y, z }
  local q_label, q_acc, q_anim = nil, 0, 0
  local prompt_acc = 0
  local boosted, user_health = false, 100            -- enemy_health held at the Q floor by Q DAMAGE / INSTA-KILL
  local held, offer, menu = false, nil, nil          -- the cards
  local auto_note = nil
  local test_kind = 0
  local armed = false

  local function log(fmt, ...) print("ge: arena: PLUS " .. string.format(fmt, ...)) end
  local function stacks(id) return stack[id] or 0 end
  local function sfx(id) if arena and arena.sfx then pcall(arena.sfx, id) end end
  local function ok_bonds()
    local t = {}
    for p = 0, bond.count() - 1 do if H.bond_ok(p) then t[#t + 1] = p end end
    return t
  end
  local function callout(text, level, sub) if T and ARENA_THEME and ARENA_THEME.callout then pcall(ARENA_THEME.callout, text, level, sub) end end

  -- ---------------------------------------------------------------- credits --
  local function double_on() return now < until_.double end
  function P.credits() return credits end
  function P.add_credits(n, why)
    if not C.credits or n == 0 then return end
    credits = math.max(0, credits + n)
    if n > 0 then earned = earned + n end
    if why then log("credits %+d (%s) -> %d", n, why, credits) end
  end

  -- what a kill earns: the tier, the wave, the stylish bonuses (Bond p got it; nil = nobody did: no bonuses)
  local function kill_credits(victim, killer, e)
    local part, weapon, hp, dist, age, blast
    part, weapon, hp, dist, age, blast = spawn.last_hit(victim)
    local p
    if part and hp ~= nil and age <= 3 then p = hp
    elseif killer ~= nil and killer ~= CHR_NONE then p = bond.player(killer) end
    local base = C.credit_kill[e and e.tier or "normal"] or C.credit_kill.normal
    local amt = base * (1 + C.credit_growth * (math.max(1, H.wave()) - 1))
    local extra, tags = 0, {}
    if p ~= nil and not nuking then
      if part == "head" then extra = extra + C.credit_head; tags[#tags + 1] = "head" end
      if part ~= "blast" and (dist or 0) >= C.credit_long_dist then extra = extra + C.credit_long; tags[#tags + 1] = "long" end
      if weapon == "fist" or weapon == "knife" then extra = extra + C.credit_melee; tags[#tags + 1] = "melee" end
      local keep = {}
      for _, t in ipairs(kill_times) do if now - t <= C.multi_window then keep[#keep + 1] = t end end
      keep[#keep + 1] = now
      kill_times = keep
      if #keep >= 2 then extra = extra + math.min(C.credit_multi_max, C.credit_multi * (#keep - 1)); tags[#tags + 1] = "x" .. #keep end
      local health = bond.vitals(p)
      if health and health < C.close_health then extra = extra + C.credit_close; tags[#tags + 1] = "close" end
      if part == "blast" and blast and blast > 0 then
        blasts[blast] = (blasts[blast] or 0) + 1
        if blasts[blast] >= 2 then extra = extra + C.credit_blast; tags[#tags + 1] = "blast" end
      end
    end
    amt = (amt + extra) * (1 + (C.card.bounty.more or 0) * stacks("bounty"))
    if double_on() then amt = amt * 2 end
    return math.max(1, math.floor(amt + 0.5)), table.concat(tags, "+")
  end

  -- ---------------------------------------------------------------- floor ----
  -- the pit floor pads (the loot prop and Q stand on one); the stands are raised (y > 1) and the gates are past 4500
  local function gather_pads()
    floor_pads = {}
    for i = 0, pads.count() - 1 do
      local x, y, z = pads.pos(i)
      if x and y <= 1 and math.abs(x) <= 4500 and math.abs(z) <= 4500 then floor_pads[#floor_pads + 1] = { id = i, x = x, y = y, z = z } end
    end
  end
  local function nearest_pad(x, z)
    local best, bd
    for _, f in ipairs(floor_pads) do
      local d = (f.x - x) ^ 2 + (f.z - z) ^ 2
      if not bd or d < bd or (d == bd and f.id < best.id) then best, bd = f, d end
    end
    return best
  end

  local function marker(label, x, y, z, rgba)
    if ar and ar.point then pcall(ar.point, x, y + 30, z, label, rgba) end
  end
  local function unmarker(label) if ar and ar.unpoint and label then pcall(ar.unpoint, label) end end

  -- ------------------------------------------------------------ enemy health --
  -- Q DAMAGE and INSTA-KILL (vs bosses) hold the enemy_health modifier at its floor, 25 (damage x4). Other things set it too (the IRON
  -- SKIN mutator, the sponsor's double damage): while this runs each tick puts 25 back, and when it ends the value the others want.
  local function want_boost() return now < until_.qdamage or now < until_.insta end
  local function health_base()
    local m = H.mut_mod and H.mut_mod("enemy_health")
    if m then return m end
    if H.double_on and H.double_on() then return 50 end
    return user_health
  end
  function P.boosted() return boosted end
  function P.user_health() return (H.double_on and H.double_on()) and 100 or user_health end
  local function health_tick()
    local floor_ = math.max(25, math.floor(100 / C.qdamage_mult + 0.5))
    if want_boost() then
      if not boosted then
        boosted = true
        user_health = mods.get("enemy_health") or 100
        if (H.mut_mod and H.mut_mod("enemy_health")) or (H.double_on and H.double_on()) then user_health = 100 end
        log("enemy_health held at %d (was %s)", floor_, tostring(mods.get("enemy_health")))
      end
      if mods.get("enemy_health") ~= floor_ then mods.set("enemy_health", floor_) end
    elseif boosted then
      boosted = false
      mods.set("enemy_health", health_base())
      log("enemy_health back to %s", tostring(mods.get("enemy_health")))
    end
  end

  -- -------------------------------------------------------------- power-ups --
  local function secs_long() return C.pu_secs * (1 + (C.card.batteries.more or 0) * stacks("batteries")) end
  local function chance() return C.pu_chance * (1 + (C.card.airdrop.more or 0) * stacks("airdrop")) end

  local function pick_kind()
    local total = 0
    for _, k in ipairs(C.pu_order) do total = total + (C.pu_weights[k] or 0) end
    local r = math.random() * total
    for _, k in ipairs(C.pu_order) do
      r = r - (C.pu_weights[k] or 0)
      if r < 0 then return k end
    end
    return C.pu_order[1]
  end

  local RGBA = { maxammo = 0xFFE040FF, insta = 0xFF5030FF, double = 0xF0C040FF, nuke = 0xFF9020FF, qdamage = 0x40D8F0FF }
  local CHIP = { insta = 0xC83A28FF, double = 0xB8901CFF, qdamage = 0x2898C0FF }

  -- a power-up of `kind` on the sand at (x, z), `born` now; false when none was placed (one is on the field, or no room)
  local function drop_at(kind, x, z)
    if pu then return false end
    local f = nearest_pad(x, z)
    if not f then return false end
    local th = TP[kind] or {}
    local model = (kind == "double") and "money" or "goldbar"
    local dx, dz = clamp(x - f.x, -400, 400), clamp(z - f.z, -400, 400)
    local h
    local ok, r = pcall(spawn.item, f.id, model, { offset = { dx, dz } })
    if ok and r then h = r else
      ok, r = pcall(spawn.item, f.id, model)   -- (the offset walked into a wall or off the floor: the pad itself)
      if ok and r then h = r; dx, dz = 0, 0 end
    end
    if not h then log("%s could not be placed near pad %d", kind, f.id); return false end
    local label = th.label or kind
    pu = { h = h, kind = kind, born = now, x = f.x + dx, y = f.y, z = f.z + dz, label = label, pad = f.id, dx = dx, dz = dz }
    drops = drops + 1
    marker(label, pu.x, pu.y, pu.z, RGBA[kind])
    H.say(string.format((T.drop or "Q-BRANCH DROP: %s"), th.say or kind), 4)
    sfx(160)
    log("DROP %s (%s) at (%.0f, %.0f) pad %d, handle %d, t=%.1f", kind, model, pu.x, pu.z, f.id, h, now)
    return true
  end

  local function end_pu(why)
    if not pu then return end
    unmarker(pu.label)
    if why == "ttl" then
      spawn.item_remove(pu.h)
      expired = expired + 1
      H.say(string.format(T.gone or "THE %s IS GONE", (TP[pu.kind] or {}).say or pu.kind), 3)
    end
    log("%s %s after %.0f s", pu.kind, why, now - pu.born)
    pu = nil
  end

  local function nuke_all()
    local list = {}
    H.each_live(function(e) if not e.boss then list[#list + 1] = e.chr end end)
    nuking = true
    local n = 0
    for _, c in ipairs(list) do
      local ok, r = pcall(spawn.kill, c)
      if ok and r then n = n + 1 end
    end
    nuking = false
    nukes = nukes + 1
    nuked = nuked + n
    return n
  end

  local function apply(kind)
    local th = TP[kind] or {}
    local sub
    local secs = math.floor(secs_long() + 0.5)
    if kind == "maxammo" then
      for _, p in ipairs(ok_bonds()) do pcall(bond.give_ammo, p, 1.0) end
      sub = th.sub
    elseif kind == "insta" then
      until_.insta = now + secs_long()
      sub = string.format(th.sub or "%d", secs) .. "   " .. (T.insta_boss or "")
    elseif kind == "double" then
      until_.double = now + secs_long()
      sub = string.format(th.sub or "%d", secs)
    elseif kind == "qdamage" then
      until_.qdamage = now + secs_long()
      sub = string.format(th.sub or "%d", secs)
    elseif kind == "nuke" then
      local n = nuke_all()
      sub = string.format(th.sub or "%d", n)
      if H.react then pcall(H.react, "boss_kill") end
      log("NUKE: %d enemies eliminated", n)
    end
    callout(th.title or kind, (kind == "nuke") and 6 or ((kind == "maxammo" or kind == "double") and 3 or 4), sub)
    H.say(th.say or kind, 3)
    collected = collected + 1
    log("COLLECTED %s", kind)
  end

  local function pu_tick()
    if not pu then return end
    if not spawn.item_alive(pu.h) then
      local k = pu.kind
      end_pu("collected")
      apply(k)
    elseif now - pu.born > C.pu_ttl then
      end_pu("ttl")
    end
  end

  -- a forced power-up of kind at (x, z) now (the test keys, tests); replaces the one on the field
  function P.drop(kind, x, z)
    if not C.powerups then return false end
    if pu then spawn.item_remove(pu.h); end_pu("replaced") end
    return drop_at(kind, x, z)
  end
  function P.field() return pu end
  function P.active() return { insta = now < until_.insta, double = now < until_.double, qdamage = now < until_.qdamage } end
  function P.left(kind) return math.max(0, (until_[kind] or -1) - now) end

  -- ---------------------------------------------------------------- crate ----
  local function price()
    local base = C.crate_price_base + C.crate_price_per_wave * math.max(1, H.wave())
    local less = clamp((C.card.discount.less or 0) * stacks("discount"), 0, 0.8)
    return math.max(5, math.floor(base * (1 - less) / 5 + 0.5) * 5)
  end
  function P.price() return price() end

  local function give_gun(p, gun, rounds)
    local ok, err = pcall(bond.give, p, gun, rounds)
    if not ok then log("bond.give %s: %s", gun, tostring(err)); return false end
    pcall(bond.equip, p, gun)
    return true
  end

  local function rounds_for(gun, minimum)
    local ok, mag = pcall(weapon.get, gun, "magsize")
    return math.max(minimum or 1, (ok and tonumber(mag)) or 0)
  end

  -- one roll for Bond p. forced: nil, "dud", "golden" or a gun name (tests); free = no price
  function P.roll(p, forced, free)
    local cost = free and 0 or price()
    if not free and credits < cost then
      H.say(string.format(TC.poor or "NEED %d CREDITS (YOU HAVE %d)", cost, credits), 3)
      sfx(54)
      return false
    end
    if cost > 0 then credits = credits - cost; spent = spent + cost end
    rolls = rolls + 1
    local r = math.random()
    local outcome = forced
    if not outcome then
      if r < C.crate_dud then outcome = "dud"
      elseif r < C.crate_dud + C.crate_golden then outcome = "golden"
      else
        local total = 0
        for _, g in ipairs(C.crate_table) do total = total + g[2] end
        local x = math.random() * total
        outcome = C.crate_table[1][1]
        for _, g in ipairs(C.crate_table) do
          x = x - g[2]
          if x < 0 then outcome = g[1]; break end
        end
      end
    end
    if outcome == "dud" then
      duds = duds + 1
      local quips = TC.quips or { "Do try to be careful with it, 007." }
      local line = quips[math.random(#quips)]
      callout(TC.dud or "Q-BRANCH", 2, line)
      if ar and ar.say and q then pcall(ar.say, q.chr, line, 4) end
      H.say(line, 4)
      log("ROLL for %d: DUD (%q), credits %d", cost, line, credits)
      return true, "dud"
    end
    local rounds, name
    if outcome == "golden" then
      goldens = goldens + 1
      outcome, rounds = "goldengun", C.crate_golden_rounds
    else
      local minimum = 1
      for _, g in ipairs(C.crate_table) do if g[1] == outcome then minimum = g[3] end end
      rounds = rounds_for(outcome, minimum)
    end
    name = (TC.weapons or {})[outcome] or outcome:upper()
    local given = give_gun(p, outcome, rounds)
    callout(outcome == "goldengun" and (TC.golden or name) or name, outcome == "goldengun" and 6 or 3,
            string.format(TC.got or "Q-BRANCH ISSUE: %s", name))
    if ar and ar.say and q then pcall(ar.say, q.chr, name, 3) end
    sfx(160)
    log("ROLL for %d: %s x%d%s, credits %d", cost, outcome, rounds, given and "" or " (REFUSED)", credits)
    return true, outcome
  end

  local function crate_tick(dt)
    if not (C.crate and q) then return end
    local label = string.format(TC.marker or "Q-BRANCH  %d", price())
    if label ~= q_label then
      unmarker(q_label)
      q_label = label
      marker(label, q.x, q.y, q.z, 0xF0C040FF)
    end
    q_anim = q_anim - dt
    if q_anim <= 0 then   -- Q fidgets now and then
      q_anim = 7 + math.random() * 8
      pcall(spawn.anim, q.chr, (math.random(2) == 1) and "adjust" or "listen")
    end
    prompt_acc = prompt_acc + dt
    if prompt_acc >= 0.5 then
      prompt_acc = 0
      for _, p in ipairs(ok_bonds()) do
        local bx, _, bz = bond.pos(p)
        if bx and (bx - q.x) ^ 2 + (bz - q.z) ^ 2 <= C.crate_radius ^ 2 then
          local cost = price()
          H.say(string.format(credits >= cost and (TC.prompt or "USE: Q-BRANCH CRATE - %d CREDITS") or (TC.poor or "%d (%d)"), cost, credits), 0.8)
          break
        end
      end
    end
  end

  on("use", function(p, proptype, idx, objtype)
    if held or not (C.crate and q) or not bond.use_key(p) or not H.bond_ok(p) then return end
    local bx, _, bz = bond.pos(p)
    if bx and (bx - q.x) ^ 2 + (bz - q.z) ^ 2 <= C.crate_radius ^ 2 then P.roll(p) end
  end)

  -- ------------------------------------------------------------------ cards --
  local apply_clips_done = -1
  local mag_base = {}
  local function apply_clips()
    local n = stacks("clips")
    if apply_clips_done == n then return end
    apply_clips_done = n
    local ok, items = pcall(weapon.items)
    if not ok or type(items) ~= "table" then log("weapon.items: %s", tostring(items)); return end
    if next(mag_base) == nil then   -- the stock sizes first, before anything is changed: aliases (ar33 / m16) share one record
      for _, name in ipairs(items) do
        local okg, mag = pcall(weapon.get, name, "magsize")
        if okg and type(mag) == "number" and mag > 0 then mag_base[name] = mag end
      end
    end
    local changed = 0
    for _, name in ipairs(items) do
      local base = mag_base[name]
      if base then
        local want = math.max(base, math.floor(base * (1 + C.card.clips.mag * n) + 0.5))
        local okg, mag = pcall(weapon.get, name, "magsize")
        if okg and want ~= mag and pcall(weapon.set, name, "magsize", want) then changed = changed + 1 end
      end
    end
    log("EXTENDED CLIPS x%d: %d magazine sizes changed", n, changed)
  end

  local CARD_FX = {
    clips = function() apply_clips() end,
    armour = function() for _, p in ipairs(ok_bonds()) do bond.heal(p, 0, 1.0) end end,
  }

  local function eligible()
    local t = {}
    for _, id in ipairs(C.card_order) do
      if stacks(id) < (C.card[id] and C.card[id].max or 1) then t[#t + 1] = id end
    end
    return t
  end

  local function take(id)
    stack[id] = stacks(id) + 1
    picked[#picked + 1] = id
    if CARD_FX[id] then CARD_FX[id]() end
    local th = TK[id] or {}
    log("REQUISITION %s (x%d)", id, stack[id])
    H.say(string.format(TK.picked or "REQUISITION: %s", th[1] or id), 4)
    sfx(160)
  end

  local function close_menu()
    held = false
    if plan and plan.hold then pcall(plan.hold, false) end
    if ui and ui.view then pcall(ui.view, { layout = "off" }) end
    menu, offer = nil, nil
  end

  local function stack_text(id)
    local n = stacks(id)
    return n > 0 and string.format(TK.owned or "OWNED x%d", n) or (TK.fresh or "NEW")
  end

  local function page_main(M)
    local it = {}
    for i, id in ipairs(offer.ids) do
      local th = TK[id] or { id, "" }
      local mx = C.card[id] and C.card[id].max or 1
      it[#it + 1] = { kind = "card", label = th[1], value = (stacks(id) + 1 > 1) and string.format("x%d / %d", stacks(id) + 1, mx) or string.format("x1 / %d", mx),
                      sub = th[2], right = stack_text(id), act = "card", idx = i, chosen = (M.cursor == i) and 1 or 0, desc = th[2] }
    end
    local inf = {}
    for _, id in ipairs(C.card_order) do
      if stacks(id) > 0 then inf[#inf + 1] = { (TK[id] or { id })[1], "x" .. stacks(id) } end
    end
    if #inf == 0 then inf[1] = { TK.none or "NOTHING YET", "" } end
    if C.credits then inf[#inf + 1] = { "", "" }; inf[#inf + 1] = { TK.credits or "CREDITS", tostring(credits), 0xF0C040FF } end
    return { layout = "lobby", items = it, title = TK.title or "REQUISITION", kicker = TK.kicker or "", cursor_act = "card",
             subtitle = string.format(TK.subtitle or "WAVE %d CLEARED", offer.w), info = inf, info_title = TK.dossier or "", info_sub = "" }
  end

  local function open_menu()
    local U = UI_MENU
    menu = U.new{ name = "arena-cards", pages = { main = page_main }, footer = TK.footer or "", quiet = 0.8,
      sfx = function(id) sfx(id) end, SFX = { move = 18, change = 43, select = 199, page = 77, back = 78 },
      act = function(_, a, item) if a == "card" and offer then local id = offer.ids[item.idx]; if id then take(id); close_menu() end end end }
    menu.cursor = 1
    held = true
    pcall(plan.hold, true)
    menu.show()
    log("cards: %s offered after wave %d; the run holds", table.concat(offer.ids, ", "), offer.w)
  end

  local function automated()
    if mode.env_flag("GE_ARENA_CARDS") then return false end   -- (=ui: the menu even in an automated run)
    return mode.env_flag("GE_FRAMES") or mode.env_flag("GE_PLAYERBOT") or mode.env_flag("GE_INPUTPLAY")
  end

  -- after a cleared wave (not the last): three random cards that can still stack (tests give the ids)
  function P.offer(w, forced)
    if not C.cards or held then return end
    local pool = eligible()
    if #pool == 0 then return end
    local ids = {}
    if forced then ids = forced else
      for _ = 1, math.min(C.cards_offered, #pool) do
        local k = math.random(#pool)
        ids[#ids + 1] = table.remove(pool, k)
      end
    end
    offer = { w = w, ids = ids }
    local can_ui = UI_MENU and ui and ui.view and plan and plan.hold and not ui.netplay() and not automated()
    if not can_ui then
      local id = ids[math.random(#ids)]
      log("cards: %s offered after wave %d; %s taken at once (%s)", table.concat(ids, ", "), w, id, ui and ui.netplay() and "netplay" or "automated run")
      take(id)
      offer = nil
      return
    end
    open_menu()
  end
  function P.held() return held end
  function P.offered() return offer and offer.ids or nil end
  function P.pick(i)   -- tests: take the i'th offered card as the menu would
    if offer and offer.ids[i] then take(offer.ids[i]); close_menu(); return true end
    return false
  end
  function P.stacks(id) return stacks(id) end
  function P.chance_now() return chance() end                                   -- tests: the power-up drop chance now
  function P.take(id) take(id) end                                              -- tests
  function P.menu_press(mask) if menu then menu.press(mask) end end             -- tests: a board event (UI_MENU.EV.*) as if pressed
  function P.crate() return q end                                               -- tests: Q ({ chr, pad, x, y, z })

  -- ------------------------------------------------------------ the arena's hooks --
  function P.stage_start()
    if not P.on then return end
    gather_pads()
    if ar and ar.on then
      pcall(ar.on, true)
      for _, l in ipairs({ "suspicion", "doors", "cameras", "keys" }) do pcall(ar.layer, l, false) end
      pcall(ar.layer, "points", true)
      pcall(ar.layer, "speech", true)
      pcall(ar.reset)
    end
    if C.crate then
      local f = nearest_pad(C.crate_spot[1], C.crate_spot[2])
      if f then
        local c = spawn.chr(f.id, { skin = C.crate_skin, weapon = "none", behaviour = "spectator", face = { 0, 0 } })
        if c then q = { chr = c, pad = f.id, x = f.x, y = f.y, z = f.z } end
      end
      log("Q-Branch crate: %s", q and string.format("Q is chr %d on pad %d at (%.0f, %.0f)", q.chr, q.pad, q.x, q.z) or "no spot (the crate is off)")
    end
    armed = true
    log("ON: credits %s, power-ups %s, crate %s, cards %s; %d floor pads", tostring(C.credits), tostring(C.powerups), tostring(C.crate), tostring(C.cards), #floor_pads)
  end

  function P.kill(victim, killer, e)
    if not P.on then return end
    if C.credits then
      local n, tags = kill_credits(victim, killer, e)
      P.add_credits(n)
      kills_paid = kills_paid + 1
      if S.debug_credits then log("kill chr %d paid %d%s, credits %d", victim, n, tags ~= "" and (" (" .. tags .. ")") or "", credits) end
    end
    if C.powerups and not nuking and not pu then
      local ch = (e and e.boss) and C.pu_chance_boss or chance()
      if math.random() < ch then
        local x, z = spawn.where(victim)
        if x then drop_at(pick_kind(), x, z) end
      end
    end
  end

  function P.wave_start(w)
    if not P.on then return end
    local b = stacks("briefing")
    if b > 0 then
      local secs = C.card.briefing.first + C.card.briefing.more * (b - 1)
      until_.qdamage = math.max(until_.qdamage, now + secs)
    end
    if stacks("armour") > 0 then CARD_FX.armour() end
    if stacks("cache") > 0 then
      for _, p in ipairs(ok_bonds()) do pcall(bond.give_ammo, p, math.min(1.0, C.card.cache.frac * stacks("cache"))) end
    end
  end

  function P.wave_cleared(w)
    if not P.on then return end
    local n = stacks("swiss")
    if C.credits and n > 0 then
      local gain = math.min(C.card.swiss.cap * n, math.floor(credits * C.card.swiss.rate * n))
      if gain > 0 then P.add_credits(gain, "interest") end
    end
    log("wave %d cleared: credits %d (earned %d, spent %d), crate price %d, cards %s", w, credits, earned, spent, price(),
      #picked > 0 and table.concat(picked, ",") or "-")
  end

  function P.tick(dt, phase)
    if not P.on then return end
    now = now + dt
    if C.powerups then pu_tick() end
    health_tick()
    crate_tick(dt)
    for _, p in ipairs(ok_bonds()) do   -- FIELD MEDIC
      local n = stacks("medic")
      if n > 0 then pcall(bond.heal, p, C.card.medic.rate * n * dt, 0) end
    end
    mode.state(P.state_text())
  end

  function P.state_text()
    return string.format("PLUS %d %d %s %d %d %d %s %s", credits, spent, pu and pu.kind or "-", math.floor(math.max(0, until_.insta - now) * 4),
      math.floor(math.max(0, until_.double - now) * 4), math.floor(math.max(0, until_.qdamage - now) * 4), table.concat(picked, ","),
      menu and menu.state_text() or "-")
  end

  -- the panel's extras: the credits (-1 = off) and the timer chips
  function P.panel()
    local chips = {}
    for _, k in ipairs({ "insta", "double", "qdamage" }) do
      local left = until_[k] - now
      if left > 0 then chips[#chips + 1] = { label = (TP[k] or {}).chip or k, secs = math.ceil(left), rgba = CHIP[k] } end
    end
    return C.credits and credits or -1, chips
  end

  function P.hud_text() return C.credits and string.format("CREDITS %d", credits) or "" end

  -- the run is over: nothing of it outlives the run (the timed effects, the power-up on the floor, an open card menu)
  function P.close()
    if held then close_menu() end
    if pu then pcall(spawn.item_remove, pu.h); end_pu("run over") end
    until_.insta, until_.double, until_.qdamage = -1, -1, -1
    health_tick()
  end

  function P.summary()
    local list = {}
    for _, id in ipairs(C.card_order) do
      if stacks(id) > 0 then list[#list + 1] = { (TK[id] or { id })[1], stacks(id) } end
    end
    table.sort(list, function(a, b) if a[2] ~= b[2] then return a[2] > b[2] end return a[1] < b[1] end)
    local top = {}
    for i = 1, math.min(12, #list) do top[i] = list[i] end   -- (all of them: the post-game flows them as chips, the stock report line takes four)
    return { earned = C.credits and earned or -1, spent = spent, cards = top, ncards = #picked, rolls = rolls, duds = duds, goldens = goldens,
             drops = drops, collected = collected, nukes = nukes, nuked = nuked }
  end

  function P.debrief_lines()
    local l = {}
    if C.credits then l[#l + 1] = string.format("CREDITS EARNED %d   SPENT %d", earned, spent) end
    if #picked > 0 then
      local names = {}
      for i, c in ipairs(P.summary().cards) do if i <= 4 then names[#names + 1] = string.format("%s x%d", c[1], c[2]) end end
      l[#l + 1] = (T.cards and TK.picked and "REQUISITIONS: " or "REQUISITIONS: ") .. table.concat(names, ", ")
    end
    return l
  end

  -- the test keys: Delete = the next power-up kind at Bond's feet, Shift+Delete = +500 credits
  function P.test(name)
    if not P.on then return end
    if name == "credits" then
      credits = credits + 500
      earned = earned + 500
      H.say("TEST: +500 CREDITS", 2)
      log("TEST +500 credits -> %d", credits)
    elseif name == "powerup" then
      test_kind = test_kind % #C.pu_order + 1
      local kind = C.pu_order[test_kind]
      local bx, _, bz = bond.pos(0)
      if bx then
        -- (a short walk off Bond's feet: the loot is collected within 90 units, so it would be taken at once)
        if P.drop(kind, bx + 160, bz) then H.say("TEST: " .. ((TP[kind] or {}).say or kind), 2) end
      end
    end
  end

  -- ---------------------------------------------------------------- events ----
  -- INSTA-KILL: a Bond's hit on a non-boss enemy queues it; the next game tick kills it (never from inside the damage path)
  on("damage", function(victim, attacker, amount, weapon_)
    if now >= until_.insta or victim < 0 or (amount or 0) <= 0 then return end
    if bond.player(attacker) == nil then return end
    local e = H.enemy(victim)
    if e and not e.boss and not e.dead_at then kq[#kq + 1] = victim end
    -- BLOOD MONEY: a hit a Bond lands heals him a little
  end)
  on("damage", function(victim, attacker, amount, weapon_)
    local n = stacks("vampire")
    if n == 0 or victim < 0 or (amount or 0) <= 0 then return end
    local p = bond.player(attacker)
    if p == nil or not H.bond_ok(p) then return end
    local e = H.enemy(victim)
    if e and not e.dead_at then pcall(bond.heal, p, math.min(C.card.vampire.cap, C.card.vampire.heal * n), 0) end
  end)
  on("ui_tick", function(dt)
    if held and menu then
      menu.tick(dt)
      return
    end
    if #kq > 0 then
      local list = kq
      kq = {}
      local seen = {}
      for _, c in ipairs(list) do
        if not seen[c] then
          seen[c] = true
          local e = H.enemy(c)
          if e and not e.dead_at then pcall(spawn.kill, c) end
        end
      end
    end
  end)

  return P
end
