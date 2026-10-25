-- The Bank Heist played by the player bot. Shipped to Data/Scripts/modes/heist_bot.lua by the build.
--   GE_MODE=heist_bot GE_STAGE=bunker2 GE_PLAYERBOT=1          the vault job (the full heist)
--   GE_MODE=heist_bot_cells ...                                 the small job: the deposit cells' cash (tools/bot/heist_bot_cells.lua)
-- It runs the real heist (tools/mode_heist.lua, unchanged) and feeds the bot (port_playerbot.c) its steps through the Lua
-- `bot` library as the heist moves on (modding/bot.md). The bot plays by the controller only; this script only reads the
-- heist's state (HEIST) and pushes plan steps.
--
-- The vault job: quiet (silenced PP7, unaware guard carriers knocked out from behind and searched, staff robbed at gunpoint,
-- never a shot at a civilian) -> GREEN card -> BLUE card -> the security console (cameras off 90 s) -> RED card -> the vault
-- antechamber: start the cracker, hold the zone, restart it when it jams -> the vault opens: grab bars until the klaxon ->
-- out by the nearer exit. The alarm does not stop it: the bot fights (cover, raisers first) and carries on.
--
-- Easy tunings (the heist's defaults are AC's to tune; these only set HEIST_SETTINGS before the include, like the tests do, and are a
-- difficulty ladder for measuring the bot): GE_HEIST_TUNE1 .. GE_HEIST_TUNE5 (the highest one set wins; none = the defaults).
--   1  no posted guards, raisers, vault guards, squads, waves or response; guards (the card carriers) at stock accuracy; jams 50%
--   2  20 posted, 3 raisers, 2 vault guards, 1 crack squad, response at 300 s, stock accuracy
--   3  30 posted, 4 raisers, 3 vault guards, 1 crack squad, response at 240 s, stock accuracy
--   4  45 posted, 6 raisers, 4 vault guards, 2 crack squads, response at 180 s, stock accuracy
--   5  the default counts and timers, stock accuracy (the defaults' only difference: heist accuracy 10 at Agent = 2x stock)
-- `acc` is the accuracy rating given to every armed chr the heist spawns (the mode's own is 10/20/30/40 by difficulty).
-- GE_HEIST_CIVOK=1 on top: no civilian limit (civ_limit 99), to measure the fighting apart from the guards' crossfire.
local OFF = 1e9
local TUNES = {
  { guards_post = 0, guards_raiser = 0, guards_vault = 0, crack_squads = 0, wave_first = OFF, response_after = OFF, jam_chance = 0.5, acc = 0 },
  { guards_post = 20, guards_raiser = 3, guards_vault = 2, crack_squads = 1, response_after = 300, acc = 0 },
  { guards_post = 30, guards_raiser = 4, guards_vault = 3, crack_squads = 1, response_after = 240, acc = 0 },
  { guards_post = 45, guards_raiser = 6, guards_vault = 4, crack_squads = 2, response_after = 180, acc = 0 },
  { acc = 0 },
}
local tune_n
for n = 1, #TUNES do if mode.env_flag("GE_HEIST_TUNE" .. n) then tune_n = n end end
HEIST_TUNE = tune_n
if mode.env_flag("GE_HEIST_CIVOK") then
  HEIST_SETTINGS = HEIST_SETTINGS or {}
  HEIST_SETTINGS.civ_limit = 99
  print("ge: heist_bot: no civilian limit")
end
if tune_n then
  HEIST_SETTINGS = HEIST_SETTINGS or {}
  local acc
  for k, v in pairs(TUNES[tune_n]) do
    if k == "acc" then acc = v else HEIST_SETTINGS[k] = v end
  end
  if acc then
    local real_chr = spawn.chr
    spawn.chr = function(pad, o)
      if type(o) == "table" and o.accuracy ~= nil then o.accuracy = acc end
      return real_chr(pad, o)
    end
  end
  print(string.format("ge: heist_bot: easy tuning %d", tune_n))
end
include("heist")
local H = HEIST
local CFG = HEIST_BOT or {}
local PLAN = CFG.plan or "vault"
local GREEN, BLUE, RED, GOLD, FLOORKEY = 0x2, 0x4, 0x8, 0x10, 0x1
local CRACK_PAD, CONSOLE_PAD = 85, 82
local EXIT_A, EXIT_B = 51, 116
local VAULT_DOOR = 68

local function log(fmt, ...) print(string.format("ge: heist_bot: " .. fmt, ...)) end
local function push(line)
  local ok, err = bot.push(line)
  if not ok then log("bad step '%s': %s", line, tostring(err)) end
end
local function idle()
  local s, n = bot.step()
  return s > n
end
local function has(k) return bond.has_key(0, k) end
local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end

local state, since = "start", 0
local looted = {}
local console_done = false
local function go(s)
  log("%s -> %s at %.0f s (alarm %s, cards %s%s%s)", state, s, H.elapsed, H.hot and "yes" or "no", has(GREEN) and "G" or "-",
      has(BLUE) and "B" or "-", has(RED) and "R" or "-")
  state, since = s, H.elapsed
end

local function face_vault()
  local d = door.get(VAULT_DOOR)
  if d then push(string.format("face %.0f %.0f %.0f", d.x, d.y, d.z)) end
end

local function start_crack()
  push("goto pad " .. CRACK_PAD .. " 50")
  face_vault()
  push("press b 3")
  push("wait 20")
end

-- the loot left in places of kind `where` (or any of a set), nearest first from Bond
local function loot_left(where)
  local x, _, z = bond.pos(0)
  local t = {}
  for i, it in ipairs(H.loot_items) do
    if (where == nil or where[it.where]) and not looted[i] and spawn.item_alive(it.h) then
      t[#t + 1] = { i = i, it = it, d = x and d2(x, z, it.x, it.z) or 0 }
    end
  end
  table.sort(t, function(a, b) return a.d < b.d end)
  return t
end

local function push_loot(where, max)
  local t = loot_left(where)
  local n = 0
  for _, e in ipairs(t) do
    if n >= max then break end
    local _, y = pads.pos(e.it.pad)
    push(string.format("goto %.0f %.0f %.0f 20", e.it.x, y, e.it.z))
    looted[e.i] = true
    n = n + 1
  end
  return n
end

local function exit_pad()
  if PLAN == "cells" then return EXIT_B end
  -- from the east wing the van and the dock are about as far; the van's tunnel is locked down once hot, but the bot has BLUE
  local x, _, z = bond.pos(0)
  local ax, _, az = pads.pos(EXIT_A)
  local bx, _, bz = pads.pos(EXIT_B)
  if x and d2(x, z, bx, bz) < d2(x, z, ax, az) * 0.8 then return EXIT_B end
  return EXIT_A
end

local function leave()
  local pad = exit_pad()
  log("leaving with %d bar(s), %d cash by pad %d", H.bars, H.cash, pad)
  push("goto pad " .. pad .. " 60")
  push("exit")
  go("out")
end

local tick_breach   -- below

local function tick_vault(ph)
  if state == "start" or state == "breach" then
    if tick_breach() then go("green") end
  elseif state == "green" then
    if has(GREEN) then go("blue") elseif idle() then push("key 0x2") end
  elseif state == "blue" then
    if has(BLUE) then go(CFG.console == false and "red" or "console") elseif idle() then push("key 0x4") end
  elseif state == "console" then
    if console_done or (H.cam_off_until and H.elapsed < H.cam_off_until) then
      console_done = true
      go("red")
    elseif idle() then
      if H.elapsed - since > 60 then log("console: giving up"); console_done = true; go("red")
      else push("goto pad " .. CONSOLE_PAD .. " 120"); push("press b 3"); push("wait 20") end
    end
  elseif state == "red" then
    if has(RED) then go("crack") elseif idle() then push("key 0x8") end
  elseif state == "crack" then
    if ph == "cracking" then go("hold")
    elseif ph == "open" then go("loot")
    elseif idle() then start_crack() end
  elseif state == "hold" then
    if ph == "jammed" then
      log("the cracker jammed at %.0f%% - restarting it", H.progress * 100)
      bot.clear()
      start_crack()
      go("crack")
    elseif ph == "open" then
      bot.clear()
      go("loot")
    elseif idle() then
      push("goto pad " .. CRACK_PAD .. " 120")   -- hold the zone, fight what comes
      push("kill 900")
    end
  elseif state == "loot" then
    if ph == "closing" or ph == "sealed" or ph == "trapped" or (ph == "open" and H.vault_left and H.vault_left < (CFG.leave_at or 28)) then
      -- (out by leave_at seconds before the doors shut: at the klaxon, 15 s, a bot in the far end of the vault was shut in)
      bot.clear()
      if ph == "trapped" then go("trapped") else leave() end
    elseif idle() then
      if push_loot({ vault = true }, 3) == 0 then leave() end
    end
  elseif state == "trapped" then
    if ph == "recracking" or ph == "escaped" then
      if ph == "escaped" then leave() end
    elseif idle() then
      push("goto pad 89 60")
      face_vault()
      push("press b 3")
      push("kill 600")
    end
  end
end

-- the banking hall: its guards see down the entrance tunnel through the glass blast doors, and an alarm raiser stands by the panel
-- just inside. Fighting in the hall gets the staff killed in the crossfire, so the bot holds the tunnel (room 2, pad 25) and lets the
-- hall come to it through the doors, until nobody has come for a while.
local hold_batch, hold_quiet_since = nil, 0
local function hall_alive()
  local n = 0
  for _, g in ipairs(H.guards) do
    if g.area == "hall" and spawn.alive(g.chr) and not spawn.down(g.chr) then n = n + 1 end
  end
  return n
end

-- The breach: hold a spot in the blast-door passage (room 1, ~400 north of the glass doors) and let the hall come to it: the
-- guards come through the doors one or two at a time (a run that sat there killed 71 for 0.3 health). When nobody comes for a
-- while, provoke: step up to the doors (they open), shoot what shows (the alarm raiser by the panel first), and fall back. Over
-- when the hall's posted guards are down to a few, or after breach_max seconds.
local HOLD = { -60, 167, -950 }
local DOORS_AT = { 0, 167, -620 }
local provokes = 0
tick_breach = function()
  if state == "start" then
    push("quiet " .. ((CFG.quiet == false) and "0" or "1"))
    if not CFG.breach then return true end   -- off by default: 0/12 either way, and room 8 overlooks the spot
    push(string.format("goto %d %d %d 40", HOLD[1], HOLD[2], HOLD[3]))
    go("breach")
    hold_quiet_since = H.elapsed
    return false
  end
  if idle() then
    local alive = hall_alive()
    if hold_batch and H.elapsed - hold_batch > 4 then hold_quiet_since = H.elapsed end   -- that batch had a fight in it
    if H.elapsed - since > (CFG.breach_max or 150) or alive <= (CFG.breach_left or 3) then
      log("breach over: %d hall guard(s) left after %.0f s, %d provoke(s)", alive, H.elapsed - since, provokes)
      return true
    end
    hold_batch = H.elapsed
    if H.elapsed - hold_quiet_since > 10 then
      provokes = provokes + 1
      hold_quiet_since = H.elapsed
      push(string.format("goto %d %d %d 40", DOORS_AT[1], DOORS_AT[2], DOORS_AT[3]))
      push("wait 45")
      push("kill 2500")
    end
    push(string.format("goto %d %d %d 40", HOLD[1], HOLD[2], HOLD[3]))
    push("kill 2500")
    push("wait 30")
  end
  return false
end

local function tick_cells()
  if state == "start" or state == "breach" then
    if tick_breach() then go("armour") end
  elseif state == "armour" then
    if idle() then
      if H.elapsed - since > 5 then go("key") else push("goto pad 48 20") end
    end
  elseif state == "key" then
    if has(FLOORKEY) then go("cash") elseif idle() then
      if H.elapsed - since > 240 then log("no floor key: trying the GREEN cell"); go("green") else push("key 0x1") end
    end
  elseif state == "cash" then
    if idle() then
      if push_loot({ cell_floor = true }, 4) == 0 then
        if has(GREEN) then go("cash2") else leave() end
      end
    end
  elseif state == "green" then
    if has(GREEN) then go("cash2") elseif idle() then push("key 0x2") end
  elseif state == "cash2" then
    if idle() then if push_loot({ cell_green = true }, 4) == 0 then leave() end end
  end
end

on("tick", function(dt)
  if bot.active() ~= 1 or not H.ready or H.ended then return end
  if state == "out" then
    if H.phase == "trapped" and H.trapped[0] then   -- shut in on the way out: re-crack from the inside
      bot.clear()
      go("trapped")
      return
    end
    if idle() then push("goto pad " .. exit_pad() .. " 60"); push("exit") end
    return
  end
  if PLAN == "cells" then tick_cells() else tick_vault(H.phase) end
end)

on("civilian_robbed", function(c, p) log("civilian %d robbed by Bond %d", c, p) end)
on("chr_knocked_out", function(c, p) log("chr %d knocked out by %d", c, p) end)
on("chr_searched", function(c, p) log("chr %d searched by %d", c, p) end)
