-- heist_hostages.lua - hostages and the human shield (designs/bank-heist.md section 31). Included at the end of tools/mode_heist.lua
-- (S.hostages = false leaves it out: the C layer stays off, exactly the old game). The hold itself is in C (port/src/port_shield.c,
-- Lua shield.*): the grab on a use press, the hostage pinned in front of the holder, his walk speed, his shots passing through it, the
-- guards holding fire with it in their line (and going round for a clear one), the releases. This file:
--   * lets go for two-handed work: the vault door (a use press or the decoder at it), the breaker / the generator, a piton line or a vent;
--   * the police STAND-OFF: a Bond holding a hostage in a police officer's line, once per hostage, buys `standoff` seconds (30): the next
--     wave holds back and the heat clock (police waves, SWAT) moves on by as much. "Negotiator: We can talk about this."
--   * the fines for the debrief (H.hostage_fine, H.hostage_report): `fine_take` a hostage taken, `fine_minute` a minute held (pro rata),
--     `hurt_fine` a hostage hurt while held; the rating is capped at COWBOY; "Negotiator" and "Hostile Takeover" commendations;
--   * presentation: HUD lines for the holder, the AR "HOSTAGE" bracket, the guide's hint, the INTEL notes card.
-- Settings: S.hostages = true | false | { fine_take =, fine_minute =, hurt_fine =, standoff =, standoff_range =, tune = { shield.tune keys } }
-- Sim state: the C records are hashed; this file's own (the stand-off clock) rides in mode.state. No math.random, no local player.

local H = HEIST
local S = H.S
local HS = { on = false, players = {}, standoffs = 0, hint = {}, last = {} }
HEIST_HOSTAGES = HS
local DEF = { fine_take = 50000, fine_minute = 50000, hurt_fine = 150000, standoff = 30, standoff_range = 2500 }
local T = {}
for k, v in pairs(DEF) do T[k] = v end
HS.T = T

local function log(fmt, ...) print(string.format("ge: heist: hostages: " .. fmt, ...)) end
local function flash(text, secs) H.flash = text; H.flash_until = H.elapsed + (secs or 4) end
local function flash_p(p, text, secs) H.pflash[p] = { text, H.elapsed + (secs or 4) } end
local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end
local function job_text(k, dflt) local jt = H.job_text or {}; return jt[k] or dflt end

on("stage_start", function()
  HS.players, HS.standoffs, HS.hint, HS.last = {}, 0, {}, {}
  H.standoff, H.standoff_until = 0, nil
  HS.on = H.phase ~= "off" and S.hostages ~= false and shield ~= nil
  if shield then shield.enable(HS.on) end
  if not HS.on then return end
  for k, v in pairs(DEF) do T[k] = v end
  local cfg = type(S.hostages) == "table" and S.hostages or {}
  for k, v in pairs(cfg) do if DEF[k] ~= nil then T[k] = v end end
  if type(cfg.tune) == "table" then shield.tune(cfg.tune) end
  log("on: fines %d a hostage + %d a minute, %d hurt; stand-off %d s", T.fine_take, T.fine_minute, T.hurt_fine, T.standoff)
end)

-- the stand-off pauses the waves (mode_heist.lua's and the police's `active`) and moves the heat clock on (heist_combat.lua)
function H.standoff_hold() return H.standoff_until ~= nil and H.elapsed < H.standoff_until end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- two hands

local function two_hands(p, what)
  if HS.on and shield.held(p) then
    shield.release(p, "two_hands")
    flash_p(p, "Both hands for " .. what .. ": you let the hostage go", 3)
  end
end

local function vault_door(idx)
  local D = H.M and H.M.DOORS or {}
  for _, g in ipairs({ D.vault_a or {}, D.vault_b or {} }) do
    for _, d in ipairs(g) do if d == idx then return true end end
  end
  return false
end

on("use", function(p, kind, idx, objtype)
  if HS.on and idx and idx >= 0 and vault_door(idx) then two_hands(p, "the vault") end
end)
on("gadget_used", function(p, item, idx, objtype)
  if HS.on and idx and idx >= 0 and vault_door(idx) then two_hands(p, "the vault") end
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the stand-off

local function standoff(p, c, cop)
  if not shield.standoff(c) then return end
  HS.standoffs = HS.standoffs + 1
  H.standoff = (H.standoff or 0) + T.standoff
  H.standoff_until = math.max(H.standoff_until or 0, H.elapsed) + T.standoff
  log("STAND-OFF: Bond %d holds hostage chr %d in police chr %d's line: the waves hold back %d s (heat clock +%d, %d in all)", p, c, cop,
      T.standoff, T.standoff, H.standoff)
  local line = job_text("negotiator", "Negotiator: We can talk about this. Nobody else has to get hurt.")
  flash(line .. string.format("  (STAND-OFF %d s)", T.standoff), 6)
  if ai and ai.bark and spawn.alive(cop) then ai.bark(cop, "radio", line) end
end

local function police()
  local C = rawget(_G, "HEIST_COMBAT")
  return C and C.on and C.police and combat and combat.stage() >= 1 and C.police or nil
end

local function standoff_tick(p, c)
  local P = police()
  if not P then return end
  local inf = shield.info(c)
  if not inf or inf.standoff then return end
  local bx, _, bz = bond.pos(p)
  for cop in pairs(P.mine) do
    if spawn.alive(cop) and not spawn.down(cop) then
      local x, z = spawn.where(cop)
      if x and d2(x, z, bx, bz) <= T.standoff_range and combat.los(cop, p) then standoff(p, c, cop); return end
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the tick: who holds whom (HUD, AR), two-handed work, the stand-off

local acc = 0
on("tick", function(dt)
  if not HS.on or H.phase == "off" or H.ended or not H.ready then return end
  acc = acc + dt
  local G = rawget(_G, "GADGETS")
  for p = 0, bond.count() - 1 do
    local c = shield.held(p)
    local me = HS.players[p]
    -- two-handed work while holding: the breaker / the generator, a line or a vent
    if c then
      if H.power and H.power.task and H.power.by == p then two_hands(p, "the panel"); c = nil
      elseif G and G.players and G.players[p] and G.players[p].reel then two_hands(p, "the line"); c = nil end
    end
    if c and (not me or me.chr ~= c) then
      HS.players[p] = { chr = c, at = H.elapsed }
      log("Bond %d takes chr %d hostage", p, c)
      flash_p(p, "HOSTAGE: guards in line hold fire. You walk. USE: let go", 5)
      if ar then ar.mark(c, { label = "HOSTAGE", rgba = 0xFF5030FF, bracket = true }); ar.say(c, "Please! Don't shoot!", 2.5, "huh") end
    elseif not c and me then
      HS.players[p] = nil
      local inf = shield.info(me.chr) or {}
      log("Bond %d lets chr %d go (%s, %.1f s%s)", p, me.chr, tostring(inf.why), inf.secs or 0, inf.hurt and ", hurt" or "")
      local why = ({ hurt = "You were hit and lost your grip", weapon = "Two-handed gun: you let them go", bond_down = nil,
                     hostage_down = "The hostage is down", use = "You let the hostage go" })[inf.why or ""]
      if why then flash_p(p, why, 3) end
      if ar then ar.clear(me.chr) end
    end
    -- what a refused press says
    local what, serial = shield.last(p)
    if serial ~= (HS.last[p] or 0) then
      HS.last[p] = serial
      if what == "nogun" then flash_p(p, "Take a hostage: one hand free - draw a pistol (PP7)", 3)
      elseif what == "front" then flash_p(p, "Get beside or behind them to take a hostage", 3) end
    end
    if c and acc >= 0.5 then standoff_tick(p, c) end
  end
  if acc >= 0.5 then acc = 0 end
end)

-- the civilians who surrender: the guide's hint (the first time one does near you)
on("civilian_surrendered", function(c, p)
  if HS.on and p and p >= 0 and not HS.hint[p] then HS.hint[p] = { at = H.elapsed, chr = c } end
end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the fines, the report (the debrief) and the presentation hooks

function H.hostage_report()
  if not HS.on then return nil end
  local R = { taken = 0, takes = 0, secs = 0, hurt = 0, standoffs = HS.standoffs, negotiator = false, fines = 0 }
  for _, c in ipairs(shield.list()) do
    local inf = shield.info(c)
    if inf then
      R.taken = R.taken + 1
      R.takes = R.takes + inf.takes
      R.secs = R.secs + inf.secs
      if inf.hurt then R.hurt = R.hurt + 1 end
      if inf.standoff and inf.clean and not inf.held and not inf.hurt then R.negotiator = true end
    end
  end
  R.fines = math.floor((R.takes * T.fine_take + R.secs / 60 * T.fine_minute + R.hurt * T.hurt_fine) / 1000 + 0.5) * 1000
  return R
end

function H.hostage_fine()
  local R = H.hostage_report()
  return R and R.fines or 0
end

function H.hostage_prompt(p)
  if not HS.on then return nil end
  if shield.held(p) then return "HOSTAGE: USE lets go. Guards in line hold fire. You walk" end
  local h = HS.hint[p]
  if h and H.elapsed - h.at < 10 then return "USE beside/behind them, pistol drawn: take a hostage" end
  return nil
end

function H.hostage_note()   -- (the board is built in the mode's stage_start, before this file's: read the settings, not HS.on)
  if S.hostages == false or not shield then return nil end
  local cfg = type(S.hostages) == "table" and S.hostages or {}
  for k, v in pairs(cfg) do if DEF[k] ~= nil then T[k] = v end end
  return { "HOSTAGES", string.format("Hands up, then step beside or behind them: USE with a pistol out takes a human shield. Guards with it in line hold fire - watch the flanks. Police stand off %d s, once each. USE lets go. Fined %s each + %s a minute; rating COWBOY at best.",
    T.standoff, H.money and H.money(T.fine_take) or tostring(T.fine_take), H.money and H.money(T.fine_minute) or tostring(T.fine_minute)) }
end

function HS.state()
  if (H.standoff or 0) == 0 and HS.standoffs == 0 then return "" end
  return string.format(" HS[%d/%s/%d]", H.standoff or 0, H.standoff_until and string.format("%.3f", H.standoff_until) or "-", HS.standoffs)
end
do   -- ride in the mode's hashed text
  local base = H.state_text
  H.state_text = function() return base() .. HS.state() end
end
