-- heist_interlock.lua: the bank's door interlocks, the staff entrance mantrap and the armoured-car sally port (designs/bank-heist.md
-- section 29). Included by tools/mode_heist.lua after the gadgets; S.interlock = false turns it off (every door stock), a table tunes it.
--
-- The C side (port/src/port_interlock.c, Lua interlock.*) enforces the rule for everyone: of a group's doors only one may stand open,
-- an order to open another is queued until every door is shut and the verification hold is over, and each door closes itself. This
-- script reads the groups from the layout (LEVEL.script.interlocks) and adds the procedures around them:
--   * VERIFY. When the last door of a cycle shuts, the group holds every door for `hold` seconds. A Bond inside the verify room then
--     passes if he holds the group's badge (the mantrap: GREEN) or wears a disguise (gadget.disguise). Otherwise, if someone watches (the
--     group's camera is live: power on, not switched off, not on a tape loop, and someone in the security room is up), he is flagged: the
--     mantrap locks down (LOCKDOWN, `lock` seconds) and the security room raises the alarm security_delay later, as for a camera spot.
--   * TAILGATING. A Bond who follows a staffer (a chr opened the cycle) or the armoured car through rides on their badge. The camera
--     still catches him with chance `tailgate_spot`: risky, but legit.
--   * THE SALLY PORT. Its outer roller wants the DOCK KEY (0x20, carried by a dock guard, the "dock foreman"), and no guard opens it.
--     The armoured car comes every `car_every` seconds (first at `car_first`): the outer roller opens, shuts, then the inner one.
--   * POWER. The mantrap fails SAFE (an exit route: life-safety egress releases its maglocks, so with the power out every door opens by
--     hand, keys and all). The sally port fails SECURE (motorised shutters with no power do not move).
--   * THE OVERRIDE. Lockpicks in hand, hold FIRE at an interlocked door that will not open (WAIT, VERIFYING, LOCKDOWN, NO POWER): after
--     `pick_time` seconds the mechanical override cylinder turns. That door ignores the group for 6 s and opens. A guard who sees the
--     work sees an act.
-- All of it is sim state: it rides in mode.state (the hashed text), and the Lua arena is the rollback snapshot. HUD prompts, AR words
-- (port_arhud.c reads the C labels) and the board's notes are presentation.
--
-- API (for other streams, e.g. the randomiser's job of the day): HEIST_INTERLOCK.list / .by_name[name] (each: id, name, label, doors,
-- outer, inner, room, cam, badge, kind); HEIST_INTERLOCK.enable(name, on) defines or clears a group (cleared: plain doors);
-- HEIST_INTERLOCK.car_now() sends the armoured car now; HEIST_INTERLOCK.info(name) -> interlock.get(id).

local H = HEIST
local S = H.S
local DOCKKEY = 0x20
local IL = { list = {}, by_name = {}, stats = { flags = 0, passes = 0, tailgates = 0, overrides = 0, cars = 0, released = 0 },
             players = {}, power_out = false, dock_key = nil, foreman = nil, first = {}, rekeyed = {} }
HEIST_INTERLOCK = IL

local T = { hold = 3, close = 4, lock = 12, tailgate_spot = 0.35, pick_time = 18, pick_reach = 150, override_time = 6,
            car = true, car_first = 150, car_every = 240, car_open = 8, dock_key = true, hint_reach = 420 }
if type(S.interlock) == "table" then for k, v in pairs(S.interlock) do T[k] = v end end
IL.T = T
IL.DOCKKEY = DOCKKEY

local function log(fmt, ...) print(string.format("ge: heist: interlock: " .. fmt, ...)) end
local function flash_p(p, text, secs) H.pflash[p] = { text, H.elapsed + (secs or 4) } end
local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end
local function num(v) if type(v) == "string" then return tonumber(v) end return v end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the groups

local function define(g)
  if g.id then return true end
  local id = interlock.define{ doors = g.doors, hold = g.hold, close = g.close, fail = g.fail }
  if not id then log("%s: could not be defined (doors %s)", g.name, table.concat(g.doors, ",")); return false end
  g.id = id
  return true
end

local function undefine(g)
  if g.id then interlock.clear(g.id); g.id = nil end
end

function IL.enable(name, on)
  local g = IL.by_name[name]
  if not g then return false end
  if on then return define(g) end
  undefine(g)
  return true
end

function IL.info(name)
  local g = IL.by_name[name]
  return g and g.id and interlock.get(g.id) or nil
end

local function load_groups()
  local cfg = LEVEL and LEVEL.script and LEVEL.script.interlocks
  if type(cfg) ~= "table" then return end
  for _, c in ipairs(cfg) do
    local g = { name = c.name, label = c.label or c.name, kind = c.kind or "mantrap", doors = {}, names = {},
                hold = num(c.hold) or T.hold, close = num(c.close) or T.close, fail = c.fail or "safe",
                badge = num(c.badge), saved = {}, prev = "ready", pick = {} }
    local ok = true
    for _, dn in ipairs(c.doors or {}) do
      local o = LEVEL.objects[dn]
      if o and o.index then g.doors[#g.doors + 1] = o.index; g.names[#g.names + 1] = dn else ok = false; log("%s: no door %s", g.name, tostring(dn)) end
    end
    g.outer, g.inner = g.doors[1], g.doors[2]
    local v = c.verify or {}
    g.room = v.room and LEVEL.rooms[v.room] and LEVEL.rooms[v.room].id or nil
    g.cam = v.camera and LEVEL.objects[v.camera] and LEVEL.objects[v.camera].index or nil
    g.outer_key = num(c.outer_key)
    g.car = c.car and T.car
    if ok and #g.doors >= 2 then
      IL.list[#IL.list + 1] = g
      IL.by_name[g.name] = g
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- verification

-- someone watches the verify room: its camera is live and someone in the security room is up
local function watched(g)
  if not g.cam or not power.on() then return false, "no camera" end
  local c = cctv.get(g.cam)
  if not c or c.disabled or c.destroyed then return false, "camera off" end
  if GADGETS and GADGETS.loops and GADGETS.loops[g.cam] then return false, "camera on a loop" end
  if H.security_up and H.security_up() == 0 then return false, "nobody in the security room" end
  return true, "camera"
end
IL.watched = watched

local function report(g, p)
  IL.stats.flags = IL.stats.flags + 1
  interlock.lockdown(g.id, T.lock)
  log("%s: VERIFY FAILED: Bond %d is neither badged nor disguised; lockdown %d s", g.name, p, T.lock)
  flash_p(p, string.format("%s: VERIFY FAILED - doors locked, security called", g.label:upper()), 6)
  if S.cover and not H.hot and not H.ended and not H.report then
    H.report = { at = H.elapsed + S.security_delay, cam = g.cam or -1, p = p }
    for _, gd in ipairs(H.guards) do   -- the relay is heard from the security wing
      if gd.area == "security" and spawn.alive(gd.chr) and not spawn.down(gd.chr) then cover.radio(gd.chr, "relay"); break end
    end
  end
end

local function verify(g, info)
  if not g.room then return end
  local tail = (info.by and info.by >= 0) or g.car_cycle
  for p = 0, bond.count() - 1 do
    if not bond.dead(p) and bond.room(p) == g.room then
      local badged = g.badge and bond.has_key(p, g.badge)
      local disguised = gadget and gadget.disguise and gadget.disguise(p) > 0
      local seen, why = watched(g)
      if badged or disguised then
        IL.stats.passes = IL.stats.passes + 1
        log("%s: Bond %d verified (%s)", g.name, p, badged and "badge" or "disguise")
        flash_p(p, g.label:upper() .. ": VERIFYING... cleared", 3)
      elseif not seen then
        IL.stats.passes = IL.stats.passes + 1
        log("%s: Bond %d not verified, but unwatched (%s)", g.name, p, why)
        flash_p(p, g.label:upper() .. ": VERIFYING... nobody is watching (" .. why .. ")", 3)
      elseif tail then
        IL.stats.tailgates = IL.stats.tailgates + 1
        local r = math.random()
        if r < T.tailgate_spot then
          log("%s: Bond %d tailgated %s and the camera caught it (%.2f < %.2f)", g.name, p, g.car_cycle and "the armoured car" or ("chr " .. tostring(info.by)), r, T.tailgate_spot)
          report(g, p)
          return
        end
        log("%s: Bond %d tailgated %s through (%.2f)", g.name, p, g.car_cycle and "the armoured car" or ("chr " .. tostring(info.by)), r)
        flash_p(p, g.label:upper() .. ": you rode in on their badge - cleared", 3)
      else
        report(g, p)
        return
      end
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the power: fail-safe groups release their badge readers, fail-secure groups freeze (the C side)

local function power_tick()
  local out = not power.on()
  if out == IL.power_out then return end
  IL.power_out = out
  for _, g in ipairs(IL.list) do
    if g.id and g.fail ~= "secure" then
      for i, d in ipairs(g.doors) do
        local dg = door.get(d)
        if out then
          g.saved[i] = dg and dg.keyflags or 0
          door.set_keyflags(d, 0)
        elseif g.saved[i] and g.saved[i] ~= 0 and dg and dg.keyflags == 0 then
          door.set_keyflags(d, g.saved[i])
        end
      end
      if out then IL.stats.released = IL.stats.released + 1 end
    end
    log("%s: power %s (%s)", g.name, out and "OUT" or "back", g.fail == "secure" and (out and "fail-secure: frozen" or "running") or (out and "fail-safe: released" or "relocked"))
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the armoured car (the sally port's own cycle)

local function car_tick(g, info)
  local c = g.carstate
  if not c then return end
  if c.step == 0 then
    if H.elapsed < c.next_at or H.ended then return end
    c.step, c.since = 1, H.elapsed
    g.car_cycle = true
    IL.stats.cars = IL.stats.cars + 1
    door.open(g.outer)
    log("%s: the armoured car arrives (%d)", g.name, IL.stats.cars)
    for p = 0, bond.count() - 1 do
      local r = bond.room(p)
      for _, n in ipairs({ "alley", "dock", "garage", "sally_port" }) do
        if LEVEL.rooms[n] and r == LEVEL.rooms[n].id then flash_p(p, "The armoured car is at the sally port: the outer roller is going up", 5) end
      end
    end
  elseif c.step == 1 then   -- the outer roller up, then down again behind the car
    if info.open[1] then c.seen = true end
    if c.seen and not info.open[1] then c.step, c.seen = 2, false; door.open(g.inner) end
    if H.elapsed - c.since > 40 then c.step = 3 end
  elseif c.step == 2 then   -- the inner roller (queued behind the hold), up and down
    if info.open[2] then c.seen = true end
    if not c.seen and info.pending ~= g.inner and info.phase == "ready" then door.open(g.inner) end
    if (c.seen and not info.open[2]) or H.elapsed - c.since > 40 then c.step = 3 end
  else
    g.car_cycle = false
    c.step, c.seen, c.next_at = 0, false, H.elapsed + T.car_every
    log("%s: the armoured car is in; next in %d s", g.name, T.car_every)
  end
end

function IL.car_now()
  for _, g in ipairs(IL.list) do
    if g.carstate and g.carstate.step == 0 then g.carstate.next_at = H.elapsed; return true end
  end
  return false
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the override: lockpicks at an interlocked door that will not open

local REFUSED = { WAIT = true, ["WAIT (QUEUED)"] = true, VERIFYING = true, ["VERIFYING > NEXT"] = true, LOCKDOWN = true, ["NO POWER"] = true }

local function pick_tick(p, dt)
  local me = IL.players[p]
  if bond.dead(p) or bond.weapon(p) ~= 54 then me.pick = nil; return end   -- ITEM_GASKEYRING: the gadgets' lockpicks
  local _, held = gadget.fire(p)                                           -- (the presses are the gadgets'; held is shared)
  local x, _, z = bond.pos(p)
  local best, bd
  for _, g in ipairs(IL.list) do
    if g.id then
      for _, d in ipairs(g.doors) do
        local dg = door.get(d)
        local word = interlock.label(d)
        if dg and dg.open < 0.05 and REFUSED[word] and math.abs(dg.y - select(2, bond.pos(p))) < 260 then
          local dd = d2(x, z, dg.x, dg.z)
          if dd <= T.pick_reach and (not bd or dd < bd) then best, bd = d, dd end
        end
      end
    end
  end
  if best and GADGETS and GADGETS.pick_target and GADGETS.pick_target(p) == best then best = nil end   -- the gadgets pick its key first
  if not best then me.pick = nil; return end
  local st = me.pick
  if not st or st.door ~= best then st = { door = best, prog = 0 }; me.pick = st end
  if held then
    st.prog = st.prog + dt
    if S.cover and not cover.alert() and cover.seen(p) then cover.act(p); log("Bond %d seen at the override cylinder", p) end
    if st.prog >= T.pick_time then
      interlock.override(best, T.override_time)
      door.open(best)
      IL.stats.overrides = IL.stats.overrides + 1
      me.pick = nil
      log("Bond %d turned the override cylinder of door %d", p, best)
      flash_p(p, "Click. The override turns: this door ignores the interlock for a moment", 4)
      gadget.bar(p, nil)
      return
    end
  end
  gadget.bar(p, st.prog / T.pick_time, 0x60E0FFFF)
  H.pflash[p] = { string.format("OVERRIDE CYLINDER: hold FIRE  %d%%  (%d s, slow)", math.floor(st.prog / T.pick_time * 100), T.pick_time), H.elapsed + 0.6 }
  if GADGETS and GADGETS.players and GADGETS.players[p] then GADGETS.players[p].my = H.pflash[p] end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- events and the tick

on("use", function(p, kind, idx, objtype)
  if H.phase == "off" or not idx or idx < 0 or not interlock.of(idx) then return end
  local word = interlock.label(idx)
  local msg = ({ WAIT = "INTERLOCK: another door is open - you're queued", ["WAIT (QUEUED)"] = "INTERLOCK: queued - it opens when the other shuts",
                 VERIFYING = "VERIFYING: hold on, it opens after the check", ["VERIFYING > NEXT"] = "VERIFYING: hold on, it opens after the check",
                 LOCKDOWN = "LOCKDOWN: the doors are held for security", ["NO POWER"] = "NO POWER: the shutters cannot move" })[word]
  if msg then flash_p(p, msg, 3) end
end)

on("stage_start", function(stage)
  IL.list, IL.by_name, IL.power_out, IL.dock_key, IL.foreman, IL.first, IL.rekeyed = {}, {}, false, nil, nil, {}, {}
  for p = 0, 3 do IL.players[p] = {} end
  if H.phase == "off" or S.interlock == false then return end
  load_groups()
  for _, g in ipairs(IL.list) do
    define(g)
    if g.kind == "sally" and g.outer_key then
      door.set_keyflags(g.outer, g.outer_key)
      IL.rekeyed[g.outer] = g.outer_key                                -- (its layout tier, BLUE, no longer opens it)
      if S.guard_doors then door.guard_access(g.outer, "never") end   -- only the car's cycle, the dock key or a lockpick
    end
    if g.car then g.carstate = { step = 0, next_at = T.car_first } end
  end
  if #IL.list > 0 then log("%d interlock group(s): %s", #IL.list, (function() local t = {} for _, g in ipairs(IL.list) do t[#t + 1] = g.name .. "=" .. table.concat(g.doors, "/") end return table.concat(t, " ") end)()) end
end)

-- the dock key: a dock guard carries it (after the guards are placed)
local function give_dock_key()
  if IL.dock_key ~= nil or not T.dock_key then return end
  IL.dock_key = false
  local need = false
  for _, g in ipairs(IL.list) do if g.outer_key == DOCKKEY then need = true end end
  if not need then return end
  for _, gd in ipairs(H.guards) do
    if gd.area == "dock" and gd.kind == "post" and spawn.alive(gd.chr) then
      IL.dock_key = spawn.key(gd.chr, DOCKKEY) or false
      IL.foreman = gd.chr
      log("the dock foreman (chr %d) carries the DOCK KEY", gd.chr)
      return
    end
  end
end

on("tick", function(dt)
  if H.phase == "off" or H.ended or not H.ready or #IL.list == 0 then return end
  give_dock_key()
  power_tick()
  for _, g in ipairs(IL.list) do
    if g.id then
      local info = interlock.get(g.id)
      if info then
        if info.phase == "verifying" and g.prev ~= "verifying" then verify(g, info) end
        g.prev = info.phase
        car_tick(g, info)
      end
    end
  end
  for p = 0, bond.count() - 1 do pick_tick(p, dt) end
end)

function IL.state()
  local t = {}
  for _, g in ipairs(IL.list) do
    local c = g.carstate
    t[#t + 1] = string.format("%s:%s/%s/%s/%s", g.name, tostring(g.id), g.prev, c and string.format("%d.%.2f.%s", c.step, c.next_at, tostring(c.seen)) or "-",
                              tostring(g.car_cycle))
  end
  for p = 0, bond.count() - 1 do
    local m = IL.players[p]
    if m and m.pick then t[#t + 1] = string.format("p%d=%d.%.3f", p, m.pick.door, m.pick.prog) end
  end
  local s = IL.stats
  return string.format(" IL[%s] %d/%d/%d/%d/%d/%d %s %s", table.concat(t, ","), s.flags, s.passes, s.tailgates, s.overrides, s.cars, s.released,
                       tostring(IL.power_out), tostring(IL.foreman))
end
do   -- ride in the mode's hashed text (heist_gadgets' tick writes H.state_text() .. its own)
  local base = H.state_text
  H.state_text = function() return base() .. IL.state() end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- presentation: the guide's prompt, the board's note and key line

function H.interlock_prompt(p, x, z)
  if #IL.list == 0 then return nil end
  for _, g in ipairs(IL.list) do
    if g.id then
      local info = interlock.get(g.id)
      local inside = g.room and bond.room(p) == g.room
      if inside and info and info.phase == "verifying" then return "VERIFYING: badge or disguise passes; else the camera flags you" end
      if inside and info and info.phase == "lockdown" then return "LOCKDOWN: held for security. Lockpicks: hold FIRE at a door" end
      for _, d in ipairs(g.doors) do
        local dg = door.get(d)
        if dg and d2(x, z, dg.x, dg.z) < T.hint_reach then
          IL.first[g.name] = IL.first[g.name] or H.elapsed
          if H.elapsed - IL.first[g.name] < 12 then
            return g.kind == "sally" and "SALLY PORT: one roller at a time. DOCK KEY, or slip in with the armoured car"
              or "MANTRAP: one door at a time, 3 s check. Badge GREEN, wear a disguise or tailgate"
          end
        end
      end
    end
  end
  return nil
end

function H.interlock_note()
  if #IL.list == 0 then return nil end
  return { "INTERLOCKS", string.format("Staff entrance mantrap: one door at a time, then %s VERIFYING - GREEN or a disguise passes, else the camera locks you in. Tailgate staff: risky. Sally port: the DOCK KEY or the armoured car. Power cut: the mantrap releases, the sally port freezes.",
    tostring(math.floor(T.hold)) .. " s") }
end

function H.interlock_keyline()
  if #IL.list == 0 then return nil end
  return "DOCK KEY: a dock guard has it - the sally port's outer roller."
end
