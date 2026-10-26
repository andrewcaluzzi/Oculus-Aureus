-- heist_combat.lua - the LOUD route (designs/bank-heist.md section 24). Included by mode_heist.lua. The guards' fighting is in C
-- (port/src/port_combat.c, Lua combat.*): cover, peek-and-shoot, flanking pairs, falling back. This file turns it on and tunes it from
-- the settings and the difficulty, registers the guards, and runs the heat: after the alarm, police at the job's entrances (the bank: the street and the dock), then the
-- SWAT response (the mode's own `response`), with a megaphone line and the vault-stair doors slamming shut.
--
-- Settings: S.combat = { ... } (every key optional; S.combat = false turns the whole layer off: stock guards and the mode's own waves):
--   tune = {}                 combat.tune{} overrides: react, hide_min, hide_max, burst_min, burst_max, cover_min, cover_max, peek_dist,
--                             retreat_hp, flank_chance, flank_range, lose, cooldown, kneel, engage, cover (0/1), flank (0/1), retreat (0/1)
--   difficulty = {}           per difficulty (0 Agent .. 3 007) tables on top of DIFF below (combat.tune keys and the heat keys)
--   police = true             police waves at the street and the dock entrances, police_at seconds after the alarm
--   police_at, police_size, police_interval, police_max      (from DIFF by difficulty: Agent 45 s / 2 a wave / 35 s / 6 alive ..
--                             007 25 s / 4 / 25 s / 12)
--   swat = true               announce the SWAT response (S.response_after) and send it in with the combat layer
--   shutters = true           when SWAT arrives the vault-stair (RED) doors slam shut: a RED card still opens them, the exits never close
--   megaphone = true          the police megaphone line (a radio bark over the first officer in, and the HUD flash)

HEIST_COMBAT = HEIST_COMBAT or {}
local C = HEIST_COMBAT

local KIND_CODE = { post = 1, raiser = 2, carrier = 3, investigator = 4, squad = 5, vault = 6, wave = 7, response = 8, police = 9 }
local DIFF = {
  [0] = { react = 2.2, hide_min = 1.8, hide_max = 3.2, burst_min = 1.0, burst_max = 1.6, flank_chance = 0.15, retreat_hp = 0.45, kneel = 0.6,
          police_at = 45, police_size = 2, police_interval = 35, police_max = 6 },                     -- Agent
  [1] = { react = 1.6, hide_min = 1.5, hide_max = 2.8, burst_min = 1.1, burst_max = 1.9, flank_chance = 0.25, retreat_hp = 0.5, kneel = 0.5,
          police_at = 35, police_size = 3, police_interval = 30, police_max = 8 },                     -- Secret Agent
  [2] = { react = 1.2, hide_min = 1.2, hide_max = 2.4, burst_min = 1.2, burst_max = 2.2, flank_chance = 0.35, retreat_hp = 0.5, kneel = 0.5,
          police_at = 30, police_size = 3, police_interval = 28, police_max = 10 },                    -- 00 Agent
  [3] = { react = 0.9, hide_min = 1.0, hide_max = 2.0, burst_min = 1.4, burst_max = 2.4, flank_chance = 0.45, retreat_hp = 0.55, kneel = 0.4,
          police_at = 25, police_size = 4, police_interval = 25, police_max = 12 },                    -- 007
}
local HEAT_KEYS = { police_at = true, police_size = true, police_interval = true, police_max = true }
local POLICE_LINES = { "POLICE! The building is surrounded - drop your weapons!", "This is the militia! Come out with your hands up!" }

local function log(fmt, ...) print(string.format("ge: heist: combat: " .. fmt, ...)) end

-- at stage start (after HEIST_AI.setup): ctx = { S, H, P, DOORS, flash, hostiles, reinforce_opts }
function C.setup(ctx)
  C.ctx = ctx
  local S = ctx.S
  local cfg = S.combat
  C.on = cfg ~= false and combat ~= nil and not mode.env_flag("GE_COMBAT_OFF")   -- (GE_COMBAT_OFF=1: an A/B switch for tests)
  if not C.on then
    if combat then combat.enable(false) end
    return
  end
  cfg = type(cfg) == "table" and cfg or {}
  C.cfg = cfg
  combat.enable(true)
  local d = mission.difficulty()
  local t = {}
  for k, v in pairs(DIFF[d] or DIFF[1]) do t[k] = v end
  for k, v in pairs((cfg.difficulty or {})[d] or {}) do t[k] = v end
  for k, v in pairs(cfg) do if HEAT_KEYS[k] then t[k] = v end end
  C.heat = { police_at = t.police_at, police_size = t.police_size, police_interval = t.police_interval, police_max = t.police_max }
  local tune = {}
  for k, v in pairs(t) do if not HEAT_KEYS[k] then tune[k] = v end end
  combat.tune(tune)
  combat.tune(cfg.tune or {})
  C.stage, C.said_police, C.said_swat = 0, false, false
  combat.stage(0)
  local H, P = ctx.H, ctx.P
  -- the police: at the job's entrances (its map's P.police pads if it names them, else its front and dock wave pads, round robin;
  -- tools/jobs/<job>.lua), so any job gets them
  C.police = nil
  local pads = {}
  if type(P.police) == "table" and #P.police > 0 then
    for _, id in ipairs(P.police) do pads[#pads + 1] = id end
  else
    local fr, dk = P.waves_front or {}, P.waves_dock or {}
    for i = 1, math.max(#fr, #dk) do
      if fr[i] then pads[#pads + 1] = fr[i] end
      if dk[i] then pads[#pads + 1] = dk[i] end
    end
  end
  if cfg.police ~= false and #pads > 0 then
    C.police = heist.reinforce{ pads = pads, interval = C.heat.police_interval, per_wave = C.heat.police_size, max_alive = C.heat.police_max,
      first = 0, reserve = S.reserve_slots,
      opts = function() return ctx.guard_opts({ behaviour = "chase", body = "Russian_Infantry", weapon = (math.random(3) == 1) and "dd44" or "kf7" }) end,
      on_spawn = function(c) spawn.role(c, "wave"); C.guard(c, "police"); C.first_cop = C.first_cop or c end,
      active = function() return H.hot and not H.ended and H.hot_at and H.elapsed - H.hot_at - (H.standoff or 0) >= C.heat.police_at and ctx.hostiles() < S.hostile_cap
                                 and not (H.standoff_hold and H.standoff_hold()) end }   -- (a hostage stand-off holds them back: section 31)
  end
  log("on: difficulty %d, react %.1f s, flank chance %.2f, police %s s after the alarm (%d a wave, every %d s, %d alive)", d, t.react,
      t.flank_chance, tostring(C.police and C.heat.police_at or "off"), C.heat.police_size, C.heat.police_interval, C.heat.police_max)
end

-- every armed chr of the heist (mode_heist.lua note_guard, the waves' and the response's on_spawn)
function C.guard(c, kind)
  if C.on and c then combat.guard(c, KIND_CODE[kind] or 0) end
end

local function say_cop(text)
  local c = C.first_cop
  if c and spawn.alive(c) and not spawn.down(c) and ai and ai.bark then ai.bark(c, "radio", text) end
end

-- the mode's tick (H.ready): the heat stages
function C.tick()
  if not C.on then return end
  local ctx = C.ctx
  local H, S = ctx.H, ctx.S
  if not H.hot or not H.hot_at or H.ended then return end
  local heat = H.elapsed - H.hot_at - (H.standoff or 0)   -- (a hostage stand-off moves the heat clock on: section 31)
  if C.stage < 1 and C.police and heat >= C.heat.police_at then
    C.stage = 1
    combat.stage(1)
    log("heat: POLICE at %.0f s after the alarm", heat)
    if C.cfg.megaphone ~= false then ctx.flash("POLICE at the entrances! Fight your way out - or find another way", 6) end
  end
  if C.stage == 1 and not C.said_police and C.first_cop and C.cfg.megaphone ~= false then
    C.said_police = true
    say_cop(POLICE_LINES[1])
  end
  if C.stage < 2 and C.cfg.swat ~= false and S.response_after and heat >= S.response_after then
    C.stage = 2
    combat.stage(2)
    log("heat: SWAT at %.0f s after the alarm", heat)
    if C.cfg.megaphone ~= false then ctx.flash("SWAT is in the building - armoured, from both entrances", 6) end
    if C.cfg.shutters ~= false and ctx.DOORS and ctx.DOORS.red then
      for _, d in ipairs(ctx.DOORS.red) do door.close(d) end
      log("lockdown: the vault-stair doors slam shut (a RED card opens them)")
    end
    say_cop(POLICE_LINES[2])
  end
end
