-- The Bank Heist played by the player bot, on the generated Janus bank (v3). Shipped to Data/Scripts/modes/heist_bot.lua by the build.
--   GE_MODE=heist_bot GE_STAGE=levelgen GE_PLAYERBOT=1 GE_SKIPINTRO=1      the vault job, solo (Bond 0 is the bot)
--   tests/heist/run_botheist.sh                                           headless, per-stage PASS / FAIL over seeds
-- It runs the real heist (tools/mode_heist.lua, unchanged) and feeds the bot (port_playerbot.c) its steps through the Lua `bot`
-- library as the heist moves on (modding/bot.md). The bot plays by the controller only: it walks the stan tiles (its own A*), opens
-- doors with B, and every act is a button press. This script reads the heist's state (HEIST, HEIST.M, LEVEL) - which is how it knows
-- where the card carriers are (a test driver's licence) - and pushes plan steps. (Until 2026-10-26 it targeted the retired Bunker 2
-- heist; designs/bank-heist.md section 23e.)
--
-- The quiet route (designs/bank-heist.md 23d's order):
--   street -> the hall (`holster 1`, `peace 1`: the banker cover holds while the bot never draws or shoots)
--   -> GREEN, BLUE, RED: each carrier's pocket picked from behind (walk to 85 units behind him, face him, press B: the gadgets'
--      pickpocket takes a use press with nothing in reach). After pick_tries misses, pick_secs seconds, or once the run is hot, the
--      bot's own `key` step takes over (staff: held at gunpoint until they hand it over; a guard: knocked out from behind if unaware,
--      else shot; then searched).
--   -> the security console (BLUE: the cameras off for 90 s) on the way down
--   -> the crack spot by vault door A: face the door, press B (the mode's use hook starts the cracker); hold the zone (fight what
--      comes: `peace 0` from the vault level on), restart it when it jams
--   -> the vault opens: walk onto the bars nearest first until leave_at seconds before the time lock
--   -> out by the nearer exit (the van or the loading dock) and `exit`.
-- Once the cover is blown or the alarm is up the bot fights (`peace 0`, quiet guns first) and carries on with the same plan.
--
-- Checkpoints: one line each, "BOTHEIST REACHED <name> <seconds>", in order: ready inside green blue console red vault_level cracking
-- vault_open gold out win. "BOTHEIST STATUS ..." every 30 s; "BOTHEIST END ..." when the mission ends.
--
-- Easy tunings (the heist's defaults are AC's to tune; these only set HEIST_SETTINGS before the include, like the tests do, and are a
-- difficulty ladder for measuring the bot): GE_HEIST_TUNE1 .. GE_HEIST_TUNE3 (the highest one set wins; none = the defaults).
--   1  no posted guards, raisers, vault guards, squads, waves or response; the carriers and the staff only; no jams
--   2  1/3 of the posted guards and raisers (about 10 posted on Agent), one crack squad, no vault guards
--   3  the default counts; no jams (the crack is the only change)
-- GE_HEIST_CIVOK=1 on top: no civilian limit. GE_BOTHEIST_NOPICK=1: no pickpocketing (the bot's `key` step from the start).
local OFF = 1e9
local TUNES = {
  { guards_post = 0, guards_raiser = 0, guards_vault = 0, crack_squads = 0, wave_first = OFF, response_after = OFF, jam_chance = 0 },
  { guards_post = 30, guards_raiser = 4, guards_vault = 0, crack_squads = 1 },
  { jam_chance = 0 },
}
local tune_n
for n = 1, #TUNES do if mode.env_flag("GE_HEIST_TUNE" .. n) then tune_n = n end end
HEIST_TUNE = tune_n
HEIST_SETTINGS = HEIST_SETTINGS or {}
HEIST_SETTINGS.briefing = false
if mode.env_flag("GE_HEIST_CIVOK") then
  HEIST_SETTINGS.civ_limit = 99
  print("ge: heist_bot: no civilian limit")
end
if tune_n then
  for k, v in pairs(TUNES[tune_n]) do HEIST_SETTINGS[k] = v end
  print(string.format("ge: heist_bot: easy tuning %d", tune_n))
end
include("heist")
local H = HEIST
local CFG = HEIST_BOT or {}
local NOPICK = mode.env_flag("GE_BOTHEIST_NOPICK") or CFG.pick == false
local GREEN, BLUE, RED = 0x2, 0x4, 0x8
local CARDS = { { GREEN, "green", "GREEN" }, { BLUE, "blue", "BLUE" }, { RED, "red", "RED" } }
local PICK_TRIES, PICK_SECS = CFG.pick_tries or 6, CFG.pick_secs or 120
local LEAVE_AT = CFG.leave_at or 25

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

-- checkpoints
local ORDER = { "ready", "inside", "green", "blue", "console", "red", "vault_level", "cracking", "vault_open", "gold", "out", "win" }
local reached = {}
local function reach(name)
  if reached[name] then return end
  reached[name] = H.elapsed
  print(string.format("BOTHEIST REACHED %s %.0f (alarm %s, blown %d)", name, H.elapsed, H.hot and "yes" or "no", H.blown or 0))
end
local function summary(how)
  local t = {}
  for _, n in ipairs(ORDER) do t[#t + 1] = n .. "=" .. (reached[n] and string.format("%.0f", reached[n]) or "-") end
  print(string.format("BOTHEIST END %s at %.0f s: %s; alarm %s at %s, cover blown %d, bars %d, cash %d, civilians killed %d",
        how, H.elapsed, table.concat(t, " "), H.hot and "yes" or "no", tostring(H.hot_at and math.floor(H.hot_at)), H.blown or 0, H.bars,
        H.cash, civilian.killed()))
end
do
  local real_win, real_fail = mission.win, mission.fail
  mission.win = function(msg) reach("win"); summary("WIN"); return real_win(msg) end
  mission.fail = function(msg) summary("FAIL " .. tostring(msg)); return real_fail(msg) end
end

local W                    -- the map (filled once the mode is ready)
local function setup_map()
  local M = H.M
  W = { M = M, crack = M.P.crack, exit_a = M.P.exit_a, exit_b = M.P.exit_b, console = M.P.console,
        hall = LEVEL.rooms.hall and LEVEL.rooms.hall.id, vault_a = M.DOORS.vault_a[1], street = {} }
  for _, R in pairs(LEVEL.rooms) do if R.area == "street" then W.street[R.id] = true end end
  W.vault_level = {}
  for _, R in pairs(LEVEL.rooms) do
    if R.area == "vault_ante" or R.area == "vault" then W.vault_level[R.id] = true end
  end
end

local state, since = "start", 0
local peace = nil
local function set_peace(on)
  if peace == on then return end
  peace = on
  push("peace " .. (on and "1" or "0"))
  push("holster " .. (on and "1" or "0"))   -- (port_playerbot.c: the slappers in hand while not fighting)
  log("peace %s at %.0f s", on and "on (holstered)" or "off (armed)", H.elapsed)
end
local function go(s)
  log("%s -> %s at %.0f s (alarm %s, cards %s%s%s, phase %s)", state, s, H.elapsed, H.hot and "yes" or "no", has(GREEN) and "G" or "-",
      has(BLUE) and "B" or "-", has(RED) and "R" or "-", H.phase)
  state, since = s, H.elapsed
end

local function carrier_of(flags)
  for role, c in pairs(H.carriers) do if c.flags == flags then return c, role end end
end

-- the pickpocket: 85 units behind the carrier, facing him, a B press
local pick = { flags = 0, tries = 0, since = 0 }
local function pick_try(c)
  local cx, cz = spawn.where(c.chr)
  local yaw, cy = gadget.chr_yaw(c.chr)
  if not cx or not yaw then return false end
  local _, py = pads.pos(c.pad)
  local y = py or cy or 0
  local bx, bz = cx - math.sin(math.rad(yaw)) * 85, cz - math.cos(math.rad(yaw)) * 85
  if pick.at_back then
    -- there: press only if he is the nearest standing chr (the pickpocket takes the nearest within 120) and Bond is behind him
    pick.at_back = false
    local x, _, z = bond.pos(0)
    local dc = d2(x, z, cx, cz)
    local off = math.abs(((math.deg(math.atan(x - cx, z - cz)) - yaw + 540) % 360) - 180)
    local other
    for _, list in ipairs({ H.guards, H.civs }) do
      for _, e in ipairs(list) do
        if e.chr ~= c.chr and spawn.alive(e.chr) and not spawn.down(e.chr) then
          local ox, oz = spawn.where(e.chr)
          if ox and d2(x, z, ox, oz) <= dc + 15 then other = e.chr end
        end
      end
    end
    if dc <= 110 and off >= 110 and not other then
      log("pickpocket %d on chr %d: pressing (%.0f away, %.0f deg off his facing)", pick.tries, c.chr, dc, off)
      push("press b 3")
      push("wait 20")
      return true
    end
    log("pickpocket %d on chr %d: not yet (%.0f away, %.0f deg off, chr %s nearer)", pick.tries, c.chr, dc, off, tostring(other))
  end
  pick.tries = pick.tries + 1
  log("pickpocket %d on chr %d (facing %.0f): from %.0f, %.0f", pick.tries, c.chr, yaw, bx, bz)
  push("holster 1")
  push(string.format("goto %.0f %.0f %.0f 40", bx, y, bz))
  -- (face him level: looking down at his feet, the use press found something else in reach and the pickpocket never ran)
  local _, by = bond.pos(0)
  push(string.format("face %.0f %.0f %.0f", cx, by or (y + 150), cz))
  pick.at_back = true
  return true
end

-- one card: pickpocket first (quiet), the bot's own `key` step after that
local function tick_card(flags, label)
  if has(flags) then reach(label); return true end
  if not idle() then return false end
  local c = carrier_of(flags)
  if pick.flags ~= flags then pick = { flags = flags, tries = 0, since = H.elapsed } end
  local standing = c and c.chr and spawn.alive(c.chr) and not spawn.down(c.chr) and select(2, spawn.key_alive(c.key)) == true
  -- (the head teller stands behind the counter, out of reach from behind: the bot's key step holds a gun on her, the guide's way)
  local role = select(2, carrier_of(flags))
  if not NOPICK and standing and role ~= "head teller" and not H.hot and pick.tries < PICK_TRIES and H.elapsed - pick.since < PICK_SECS then
    pick_try(c)
  else
    if pick.tries >= 0 and not pick.keyed then
      log("%s: the bot's key step (picks %d, %s)", label, pick.tries, standing and "carrier standing" or "carrier down or the card loose")
      pick.keyed = true
      push("holster 0")   -- (staff hand a card over only at gunpoint)
    end
    if H.hot or (H.blown or 0) > 0 then set_peace(false) end
    push(string.format("key 0x%x", flags))
  end
  return false
end

local function face_vault()
  local d = door.get(W.vault_a)
  if d then push(string.format("face %.0f %.0f %.0f", d.x, d.y, d.z)) end
end
local function start_crack()
  push("goto pad " .. W.crack .. " 40")
  face_vault()
  push("press b 3")
  push("wait 20")
end

local looted = {}
local function push_loot(max)
  local x, _, z = bond.pos(0)
  local t = {}
  for i, it in ipairs(H.loot_items) do
    if it.where == "vault" and not looted[i] and spawn.item_alive(it.h) then
      t[#t + 1] = { i = i, it = it, d = x and d2(x, z, it.x, it.z) or 0 }
    end
  end
  table.sort(t, function(a, b) return a.d < b.d end)
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
  local x, _, z = bond.pos(0)
  local ax, _, az = pads.pos(W.exit_a)
  local bx, _, bz = pads.pos(W.exit_b)
  if x and d2(x, z, bx, bz) < d2(x, z, ax, az) then return W.exit_b end
  return W.exit_a
end
local function leave()
  local pad = exit_pad()
  log("leaving with %d bar(s), %d cash by pad %d", H.bars, H.cash, pad)
  push("goto pad " .. pad .. " 60")
  push("exit")
  go("out")
end

local console_tries = 0
local function tick_plan()
  local ph = H.phase
  if state == "start" then
    push("quiet 1")
    set_peace(true)
    go("green")
  elseif state == "green" then
    if tick_card(GREEN, "green") then go("blue") end
  elseif state == "blue" then
    if tick_card(BLUE, "blue") then go("red") end
  elseif state == "red" then
    if tick_card(RED, "red") then go(CFG.console == false and "vault" or "console") end
  elseif state == "console" then
    if H.cam_off_until and H.elapsed < H.cam_off_until then
      reach("console")
      go("vault")
    elseif idle() then
      if console_tries >= 3 or H.elapsed - since > 90 then log("console: giving up"); go("vault")
      else
        console_tries = console_tries + 1
        push("goto pad " .. W.console .. " 60")
        push("press b 3")
        push("wait 30")
      end
    end
  elseif state == "vault" then
    set_peace(false)   -- the vault level is restricted: no cover there; fight (silenced) what sees the bot
    if W.vault_level[bond.room(0)] then reach("vault_level") end
    if ph == "cracking" then reach("vault_level"); go("hold")
    elseif ph == "open" then go("loot")
    elseif idle() then start_crack() end
  elseif state == "hold" then
    if ph == "cracking" then reach("cracking") end
    if ph == "jammed" then
      log("the cracker jammed at %.0f%% - restarting it", H.progress * 100)
      bot.clear()
      start_crack()
      go("vault")
    elseif ph == "open" then
      bot.clear()
      go("loot")
    elseif idle() then
      push("goto pad " .. W.crack .. " 120")   -- hold the zone, fight what comes
      push("kill 900")
    end
  elseif state == "loot" then
    reach("cracking"); reach("vault_open")
    if H.bars > 0 then reach("gold") end
    if ph == "closing" or ph == "sealed" or ph == "trapped" or (ph == "open" and H.vault_left and H.vault_left < LEAVE_AT) then
      bot.clear()
      if ph == "trapped" and H.trapped[0] then go("trapped") else leave() end
    elseif idle() then
      if push_loot(3) == 0 then leave() end
    end
  elseif state == "trapped" then
    if ph == "escaped" then leave()
    elseif idle() then
      push("goto pad " .. H.M.P.crack .. " 60")   -- (the re-crack zones are inside door A / B: the nav ends at the door)
      face_vault()
      push("press b 3")
      push("kill 600")
    end
  end
end

local last_status = 0
local wd_step, wd_since = -1, 0   -- the watchdog: a step that runs 30 s (a goto onto a spot the tiles cannot reach) is dropped
on("tick", function(dt)
  if bot.active() ~= 1 or H.ended then return end
  if not H.ready then return end
  do
    local s, n = bot.step()
    if s ~= wd_step or s > n then wd_step, wd_since = s, H.elapsed
    elseif H.elapsed - wd_since > (CFG.step_secs or 30) and state ~= "out" and state ~= "hold" then
      log("watchdog: step %d of %d ran %.0f s - dropping the plan", s, n, H.elapsed - wd_since)
      bot.clear()
      pick.at_back = false
      wd_step, wd_since = -1, H.elapsed
    end
  end
  if not W then setup_map(); reach("ready") end
  if W.hall and bond.room(0) == W.hall then reach("inside") end
  if H.bars > 0 then reach("gold") end
  if (H.hot or (H.blown or 0) > 0) and peace then
    log("cover gone (alarm %s, blown %d): fighting", H.hot and "yes" or "no", H.blown or 0)
    bot.clear()
    set_peace(false)
  end
  if H.elapsed - last_status >= 30 then
    last_status = H.elapsed
    local x, _, z = bond.pos(0)
    local s, n = bot.step()
    print(string.format("BOTHEIST STATUS %.0f s: state %s, phase %s, room %s at %.0f,%.0f, step %d/%d, alarm %s, blown %d, cards 0x%x",
          H.elapsed, state, H.phase, tostring(bond.room(0)), x or 0, z or 0, s, n, H.hot and "yes" or "no", H.blown or 0, bond.keys(0)))
  end
  if state == "out" then
    if zone.contains(H.zones.exit_a, bond.id(0)) or zone.contains(H.zones.exit_b, bond.id(0)) then reach("out") end
    if H.phase == "trapped" and H.trapped[0] then bot.clear(); go("trapped"); return end
    if idle() then push("goto pad " .. exit_pad() .. " 60"); push("exit") end
    return
  end
  tick_plan()
end)

on("use", function(p, kind, idx, objtype)
  if p == 0 and not H.ended then log("use press: kind %s idx %s objtype %s at %.0f s", tostring(kind), tostring(idx), tostring(objtype), H.elapsed) end
end)
on("civilian_robbed", function(c, p) log("civilian %d robbed by Bond %d", c, p) end)
on("chr_knocked_out", function(c, p) log("chr %d knocked out by %d", c, p) end)
on("chr_searched", function(c, p) log("chr %d searched by %d", c, p) end)
on("alarm", function() log("ALARM at %.0f s", H.elapsed) end)
