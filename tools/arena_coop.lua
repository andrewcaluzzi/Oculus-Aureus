-- Arena co-op (drop A7): two Bonds in the Colosseum. Included by mode_arena.lua when ARENA_SETTINGS.coop is set (the arena-coop
-- preset does that); shipped as Data/Scripts/modes/_arena_coop.lua. Needs a co-op stage (GE_COOP=1); with one Bond it does nothing,
-- so solo play is unchanged. See modding/arena.md "Drop A7 co-op".
--
--   * the partner: with coop_bot the second Bond is played by the arena bot (port_arenacoop.c): it fights the arena enemies, keeps
--     near you, revives you, and walks to the centre ammo / armour when it needs them (this script tells it where)
--   * waves scale for two: more enemies (coop_count_mult), more at once (coop_alive_mult), tougher (coop_hp_mult; bosses coop_boss_hp_mult)
--   * downed and revive: a Bond whose health runs out goes DOWN while his partner is up; the partner stands next to him for
--     coop_revive seconds to bring him back; after coop_bleed seconds he bleeds out (coop_bleedout: "wave" = back when the wave is
--     cleared, "run" = out for the rest of the run, "end" = the run is over). Nobody up = the run is over.
--   * supplies, health per kill and the centre drops are per Bond already (mode_arena.lua); a downed Bond gets neither
--
-- Settings (ARENA_SETTINGS, defaults below): coop_bot, coop_count_mult, coop_alive_mult, coop_hp_mult, coop_boss_hp_mult, coop_down,
-- coop_bleed, coop_revive, coop_revive_radius, coop_revive_health, coop_revive_grace, coop_bleedout, coop_bot_ammo_low,
-- coop_bot_armour_low, coop_debug.

local DEFAULTS = {
  coop_bot            = true,    -- the second Bond is the arena bot (local co-op; in netplay the partner is a person)
  coop_count_mult     = 1.5,     -- enemies per wave x this (bosses are not doubled)
  coop_alive_mult     = 1.35,    -- most alive at once x this
  coop_hp_mult        = 1.2,     -- enemy health and armour x this
  coop_boss_hp_mult   = 1.5,     -- boss health and armour x this (instead of coop_hp_mult)
  coop_down           = true,    -- downed and revive (false: the stock co-op death and 10 s respawn)
  coop_bleed          = 30,      -- seconds a downed Bond lasts
  coop_revive         = 3,       -- seconds the partner stands next to him
  coop_revive_radius  = 170,     -- how close, units
  coop_revive_health  = 0.35,    -- health after a revive
  coop_revive_grace   = 2,       -- seconds without damage after a revive
  coop_bleedout       = "wave",  -- "wave" | "run" | "end"
  coop_bot_ammo_low   = 40,      -- the bot fetches a centre ammo box below this many rounds for its gun (and in breathers)
  coop_bot_armour_low = 0.5,     -- ...and the centre armour below this
  coop_debug          = false,   -- bot status every 2 s, state changes
}

function ARENA_COOP(S)
  for k, v in pairs(DEFAULTS) do if S[k] == nil then S[k] = v end end
  local M = {}
  local on = false          -- two Bonds
  local bot_p = nil         -- the Bond the bot plays
  local last = {}           -- player -> last state string (for the log)
  local dbg_acc = 0
  local revive_log = {}

  local function n() return bond.count() end

  function M.downed(p) return on and coop.state(p) == "down" end

  function M.stage_start()
    on = n() >= 2
    print(string.format("ge: arena: co-op %s (%d Bonds)", on and "ON" or "off (one Bond: solo rules)", n()))
    if not on then return end
    coop.rules{ on = S.coop_down, bleed = S.coop_bleed, revive = S.coop_revive, radius = S.coop_revive_radius,
                revive_health = S.coop_revive_health, grace = S.coop_revive_grace, bleedout = S.coop_bleedout, debug = S.coop_debug }
    if S.coop_bot then
      if coop.bot(true) then
        bot_p = 1
        print("ge: arena: co-op partner bot drives player 1")
      else
        print("ge: arena: co-op partner bot not available (netplay: the partner is a person)")
      end
    end
    for p = 0, n() - 1 do last[p] = "up" end
  end

  -- more, tougher enemies for two (the queue the wave designer made; specs are shared tables, so copies get their own)
  function M.scale(queue, w)
    if not on then return end
    local base = #queue
    for _, e in ipairs(queue) do
      local m = e.boss and S.coop_boss_hp_mult or S.coop_hp_mult
      e.spec.health = (e.spec.health or 1) * m
      if e.spec.armour then e.spec.armour = e.spec.armour * m end
    end
    local plain = {}
    for _, e in ipairs(queue) do if not e.boss then plain[#plain + 1] = e end end
    local extra = math.floor(#plain * (S.coop_count_mult - 1) + 0.5)
    for i = 1, extra do
      local src = plain[(i - 1) % #plain + 1]
      local spec = {}
      for k, v in pairs(src.spec) do spec[k] = v end
      queue[#queue + 1] = { n = (src.n or 0) + i, gates = src.gates, gate = src.gate, mouth = src.mouth, spec = spec }
    end
    print(string.format("ge: arena: co-op wave %d scaled: %d -> %d enemies, health x%.2f (bosses x%.2f)", w, base, #queue,
      S.coop_hp_mult, S.coop_boss_hp_mult))
  end

  function M.cap(cap)
    if not on then return cap end
    return math.floor(cap * S.coop_alive_mult + 0.5)
  end

  -- a bled-out Bond comes back when the wave is cleared (bleedout "wave")
  function M.wave_cleared(w)
    if not on then return end
    for p = 0, n() - 1 do
      if coop.state(p) == "out" and coop.release(p) then
        print(string.format("ge: arena: co-op player %d back for wave %d", p, w + 1))
      end
    end
    local d, r, b = coop.counts()
    print(string.format("ge: arena: co-op after wave %d: downs %d, revives %d, bleed-outs %d", w, d, r, b))
  end

  local function hud_for(p)
    local s, left, frac = coop.state(p)
    local q = 1 - p
    local qs, qleft, qfrac = coop.state(q)
    if s == "down" then
      if frac and frac > 0 then return string.format("DOWN  %ds  -  BEING REVIVED %d%%", math.ceil(left), math.floor(frac * 100)) end
      return string.format("DOWN  %ds  -  WAIT FOR YOUR PARTNER", math.ceil(left))
    elseif s == "out" then
      return "BLED OUT  -  BACK WHEN THE WAVE IS CLEARED"
    elseif s == "up" and qs == "down" then
      if qfrac and qfrac > 0 then return string.format("REVIVING PARTNER %d%%  (%ds)", math.floor(qfrac * 100), math.ceil(qleft)) end
      return string.format("PARTNER DOWN  %ds  -  STAND NEXT TO HIM", math.ceil(qleft))
    elseif s == "up" and qs == "out" then
      return "PARTNER BLED OUT  -  CLEAR THE WAVE"
    end
    return ""
  end

  -- the bot's errands: centre ammo when it runs low (or in a breather), armour when it is low; reviving comes first (the bot does that itself)
  local function bot_errands(phase)
    if not bot_p or coop.state(bot_p) ~= "up" then return end
    if coop.state(1 - bot_p) == "down" then coop.bot_goal(bot_p); return end
    local info = coop.bot_info(bot_p)
    local _, arm = bond.vitals(bot_p)
    local bx, _, bz = bond.pos(bot_p)
    if not info or not bx then return end
    local best, bd = nil, 1e18
    for _, it in ipairs(ARENA_CENTRE_ITEMS and ARENA_CENTRE_ITEMS() or {}) do
      local want = (it.kind == "ammo" and (info.ammo < S.coop_bot_ammo_low or phase ~= "fight"))
                or (it.kind == "armour" and (arm or 1) < S.coop_bot_armour_low)
      if want then
        local x, z = spawn.item_pos(it.h)
        if x then
          local d = (x - bx) ^ 2 + (z - bz) ^ 2
          if d < bd then best, bd = { x, z, it.kind }, d end
        end
      end
    end
    if best then coop.bot_goal(bot_p, best[1], best[2]) else coop.bot_goal(bot_p) end
  end

  function M.tick(dt, phase)
    if not on then return end
    for p = 0, n() - 1 do
      local s = coop.state(p)
      if s ~= last[p] then
        print(string.format("ge: arena: co-op player %d %s -> %s", p, last[p] or "?", s or "?"))
        if last[p] == "down" and s == "up" and arena.callout and type(ARENA_THEME) == "table" and ARENA_THEME.events then
          -- revived: the theme's revive callout ("YOU ONLY LIVE TWICE")
          local subs = ARENA_THEME.event_subs or {}
          ARENA_THEME.callout(ARENA_THEME.events.revive, 3, string.format(subs.revive or "%s IS BACK", "P" .. (p + 1)))
        end
        last[p] = s
      end
      hud.set(3, hud_for(p), p)
    end
    bot_errands(phase)
    if S.coop_debug and bot_p then
      dbg_acc = dbg_acc + dt
      if dbg_acc >= 2 then
        dbg_acc = 0
        local i = coop.bot_info(bot_p)
        local h, a = bond.vitals(bot_p)
        if i then
          print(string.format("ge: arena: bot info act %s target %d ammo %d weapon %d shots %d hp %.2f armour %.2f",
            i.act, i.target, i.ammo, i.weapon, i.shots, h or -1, a or -1))
        end
      end
    end
  end

  return M
end
