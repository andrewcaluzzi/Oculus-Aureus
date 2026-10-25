-- The generated level's setup objects (designs/bank-heist.md P2.2, modding/levelgen.md "Objects"). Run: tests/levelgen/run_objects.sh.
-- On tools/levelgen/objects.json (the P2.1 L plus an office D behind a GREEN door, a vault E behind a vault door and a padlocked shutter):
--   1 every object is built (a live prop), the doors take their doorways' portals: a shut door culls the room behind it
--   2 the GREEN door: refuses Bond, he cannot walk through; the card on its pad in C; then it opens and he walks into D
--   3 the camera in D spots Bond and raises the alarm (cctv_spotted, alarm.active)
--   4 the monitor in D: a use press there opens the camera feed (the `use` event names the monitor; cctv.view)
--   5 the alarm box: a raiser runs to it and sounds the alarm; shot out, it no longer works for the next raiser
--   6 the safe: its door refuses Bond without GOLD; the GOLD key on its pad; then it swings open
--   7 the padlocked shutter D-E: refuses while the padlock is on; shot off, it opens
--   8 the vault door C-E: no card opens it; the cracker (gadget_used) names it; door.open (the mode's crack) opens it, Bond walks into the
--     vault, the loot pads take gold bars, door.snap shuts it and the vault is culled again
-- Prints "LEVELGEN PASS|FAIL <name> ...", "LEVELGEN SHOT <name>" before each screenshot, "LEVELGEN DONE <n> passed, <m> failed".
include("levelgen_layout")
mode.keep_locks()
mods.set("invincible", true)

local passed, failed = 0, 0
local function check(name, ok, info)
  if ok then passed = passed + 1 else failed = failed + 1 end
  print(string.format("LEVELGEN %s %s %s", ok and "PASS" or "FAIL", name, info or ""))
end

local face = nil
local function wait(sec)
  local t = 0
  while t < sec do
    t = t + coroutine.yield()
    bond.heal(0, 1, 1)
    if face then bond.face(0, face[1], face[2]) end
  end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end

local function rid(name) return LEVEL.rooms[name] and LEVEL.rooms[name].id end
local A, B, C, D, E = rid("A"), rid("B"), rid("C"), rid("D"), rid("E")
local OB = LEVEL.objects
local function doorway(name) for _, d in ipairs(LEVEL.doors) do if d.name == name then return d end end end
local BD, CE, DE = doorway("B-D"), doorway("C-E"), doorway("D-E")

local function drawn()
  local s, l = {}, {}
  for _, r in ipairs(bond.rooms_drawn(0)) do s[r] = true; l[#l + 1] = tostring(r) end
  return s, table.concat(l, ",")
end
local function shot(name)
  print("LEVELGEN SHOT " .. name)
  cctv.test_shot()
  wait(0.2)
end
local function padxz(id) local x, _, z = pads.pos(id); return { x, z } end
local function walk(points, limit)
  face = nil
  for _, p in ipairs(points) do
    local t = 0
    while true do
      local d = bond.walk(0, p[1], p[2])
      if d and d < 45 then break end
      t = t + coroutine.yield()
      if t > (limit or 15) then bond.walk_stop(); return false, p end
    end
  end
  bond.walk_stop()
  return true
end
local function stand(pad, look)          -- warp onto a pad and look at a point
  bond.warp(0, pad)
  face = look
  wait(0.6)
end
local function dstate(i) local g = door.get(i); return g and string.format("%s %.2f kf=0x%x", g.state, g.open, g.keyflags) or "nil" end

-- events the checks read
local spotted, uses, gadgets, alarms = {}, {}, {}, {}
on("cctv_spotted", function(cam, p) spotted[#spotted + 1] = cam end)
on("use", function(p, kind, idx, objtype)
  uses[#uses + 1] = { idx = idx, objtype = objtype }
  if p == 0 and idx == OB.monitor_office.index and objtype == 10 then cctv.view(0, OB.cam_office.index) end   -- the monitor opens the feed
end)
on("gadget_used", function(p, item, idx, objtype) gadgets[#gadgets + 1] = { item = item, idx = idx } end)
on("alarm", function(a) alarms[#alarms + 1] = a end)

local function body()
  wait(1.5)
  -- 1: built
  local bad = {}
  for name, o in pairs(OB) do
    local g = obj.get(o.index)
    if not (g and g.live) then bad[#bad + 1] = name end
  end
  table.sort(bad)
  check("every object built (a live prop)", #bad == 0, #bad > 0 and ("not live: " .. table.concat(bad, ", ")) or "")
  local cams = cctv.list()
  check("the camera is a stock CCTV", #cams == 1 and cams[1] == OB.cam_office.index, "cctv.list " .. table.concat(cams, ","))
  check("doors carry their key flags", door.get(OB.office_door.index).keyflags == 0x2 and door.get(OB.vault_door.index).keyflags == 0x40000000
        and door.get(OB.office_safe.index).keyflags == 0x10)
  check("the shutter is padlocked", door.get(OB.shutter.index).padlocked == true)
  local og = obj.get(OB.alarm_office.index)
  check("object types: alarm 5, monitor 10, camera 6, safe body 43", og.type == 5 and obj.get(OB.monitor_office.index).type == 10
        and obj.get(OB.cam_office.index).type == 6 and obj.get(OB.office_safe.body).type == 43, "alarm in room " .. tostring(og.room))

  -- the shut GREEN door culls D
  stand(BD.pad_a, { BD.x, BD.z })
  local s, l = drawn()
  check("a shut door culls the room behind it (D not drawn from B)", s[B] and not s[D], "drawn {" .. l .. "} D = " .. tostring(D))
  shot("b_shut_office_door")

  -- 2: the GREEN door
  check("the GREEN door refuses Bond without the card", door.try(0, OB.office_door.index) == false, dstate(OB.office_door.index))
  walk({ padxz(BD.pad_b) }, 4)
  check("... and he cannot walk through it", bond.room(0) == B, "room " .. tostring(LEVEL.room_name[bond.room(0)]))
  bond.warp(0, OB.green_card.pad)
  local got = wait_for(4, function() return bond.has_key(0, 0x2) end)
  check("the GREEN card on its pad in C is picked up", got, string.format("keys 0x%x", bond.keys(0)))
  stand(BD.pad_a, { BD.x, BD.z })
  check("the GREEN door opens with the card", door.try(0, OB.office_door.index) == true, dstate(OB.office_door.index))
  wait(1.5)
  s, l = drawn()
  check("the open door shows D (its portal opened)", s[B] and s[D], "drawn {" .. l .. "}")
  shot("b_open_office_door")
  local ok = walk({ padxz(BD.pad_b), padxz(LEVEL.pads.d_mid) }, 10)
  check("Bond walks through into D", ok and bond.room(0) == D, "room " .. tostring(LEVEL.room_name[bond.room(0)]))

  -- 3: the camera
  alarm.stop()
  cctv.set_alarm_time(3, OB.cam_office.index)
  stand(LEVEL.pads.d_mid, { 1100, 0 })
  local seen = wait_for(10, function() return #spotted > 0 end)
  local cg = cctv.get(OB.cam_office.index)
  check("the camera spots Bond (cctv_spotted)", seen and spotted[1] == OB.cam_office.index,
        string.format("spotted %s, timer %.1f, cam at (%.0f, %.0f, %.0f) room %s", tostring(spotted[1]), cg.timer, cg.x, cg.y, cg.z, tostring(cg.room)))
  check("... and raises the alarm", alarm.active())
  face = { cg.x, cg.z }
  wait(0.5)
  shot("d_camera")
  alarm.stop()
  cctv.alarm_on_spot(false)             -- (the camera stays on for the feed, but no more alarms from it)

  -- 4: the monitor
  stand(LEVEL.pads.d_monitor, { 1600, -500 })
  shot("d_monitor_and_alarm")
  uses = {}
  cctv.test_use(0)
  local watching = wait_for(3, function() return cctv.viewing(0) ~= nil end)
  local u = uses[1] or {}
  check("a use press at the monitor names it (use event: its index, objtype 10)", u.idx == OB.monitor_office.index and u.objtype == 10,
        string.format("use idx %s objtype %s (monitor %d)", tostring(u.idx), tostring(u.objtype), OB.monitor_office.index))
  check("... and opens the camera feed (cctv.view)", watching and cctv.viewing(0) == OB.cam_office.index, tostring(cctv.viewing(0)))
  wait(0.5)
  shot("d_camera_feed")
  cctv.view_off(0)
  cctv.disable(OB.cam_office.index)     -- (off: the raiser test needs no camera)
  wait(0.5)

  -- 5: the alarm box
  alarm.stop()
  stand(LEVEL.pads.d_mid, { 1500, 300 })
  local r1 = spawn.chr(LEVEL.pads.d_raiser, { weapon = "pp7", behaviour = "hold", alarm_pad = OB.alarm_office.pad })
  local raised, t1 = wait_for(25, function() return alarm.active() end)
  local rx, rz = 0, 0
  if r1 then rx, rz = spawn.where(r1) end
  check("a raiser runs to the alarm box and sounds the alarm", r1 ~= nil and raised,
        string.format("%.1f s, raiser at (%.0f, %.0f), box pad %d", t1, rx or 0, rz or 0, OB.alarm_office.pad))
  if r1 then spawn.kill(r1) end
  alarm.stop()
  obj.test_damage(OB.alarm_office.index, 5000, 0)
  wait(0.5)
  check("the alarm box can be shot out", obj.get(OB.alarm_office.index).destroyed, string.format("damage %.1f of %.1f", obj.get(OB.alarm_office.index).damage,
        obj.get(OB.alarm_office.index).health))
  local r2 = spawn.chr(LEVEL.pads.d_raiser, { weapon = "pp7", behaviour = "hold", alarm_pad = OB.alarm_office.pad })
  local raised2 = wait_for(20, function() return alarm.active() end)
  check("... and then a raiser cannot sound it", r2 ~= nil and not raised2, "alarm " .. tostring(alarm.active()))
  if r2 then spawn.kill(r2) end
  alarm.stop()

  -- 6: the safe
  local sd = door.get(OB.office_safe.index)
  check("the safe refuses Bond without GOLD", door.try(0, OB.office_safe.index) == false, dstate(OB.office_safe.index))
  bond.warp(0, OB.gold_key.pad)
  got = wait_for(4, function() return bond.has_key(0, 0x10) end)
  check("the GOLD key on its pad is picked up", got, string.format("keys 0x%x", bond.keys(0)))
  stand(LEVEL.pads.d_mid, { sd.x, sd.z })
  local sopen = door.try(0, OB.office_safe.index)
  local swung = wait_for(4, function() return door.get(OB.office_safe.index).open > 0.3 end)
  check("the safe opens with GOLD (its door swings)", sopen == true and swung, dstate(OB.office_safe.index))
  bond.warp(0, LEVEL.pads.d_mid, 0, 200)
  face = { sd.x, sd.z }
  wait(0.6)
  shot("d_safe_open")

  -- 7: the padlocked shutter D-E
  stand(DE.pad_a, { DE.x, DE.z })
  check("the padlocked shutter refuses Bond", door.try(0, OB.shutter.index) == false, dstate(OB.shutter.index))
  shot("d_padlocked_shutter")
  obj.test_damage(DE.padlock, 5000, 0)
  wait(0.5)
  -- (door.get().padlocked marks a door that HAS a padlock record; whether it still holds is the padlock object's health)
  check("the padlock is shot off", obj.get(DE.padlock).destroyed, string.format("damage %.0f of %.0f", obj.get(DE.padlock).damage,
        obj.get(DE.padlock).health))
  local sh = door.try(0, OB.shutter.index)
  local up = wait_for(4, function() return door.get(OB.shutter.index).open > 0.6 end)
  check("... and the shutter opens", sh == true and up, dstate(OB.shutter.index))
  wait(0.5)
  s, l = drawn()
  check("the open shutter shows E", s[E], "drawn {" .. l .. "}")
  shot("d_shutter_open")

  -- 8: the vault door C-E
  stand(CE.pad_a, { CE.x, CE.z })
  local vd = OB.vault_door.index
  check("no card opens the vault door", door.try(0, vd) == false and bond.has_key(0, 0x12), dstate(vd))
  s, l = drawn()
  check("the shut vault door culls E from C", s[C] and not s[E], "drawn {" .. l .. "}")
  gadgets = {}
  bond.test_gadget(0, "doordecoder")
  local gu = gadgets[1] or {}
  check("the cracker at the vault door names it (gadget_used)", gu.idx == vd, "gadget idx " .. tostring(gu.idx) .. " vault " .. vd)
  door.open(vd)                           -- what the mode's crack does when it is done
  local vopen = wait_for(5, function() return door.get(vd).open > 0.8 end)
  check("the crack's door.open opens the vault door", vopen, dstate(vd))
  shot("c_vault_open")
  ok = walk({ padxz(CE.pad_b), padxz(LEVEL.loot.vault_gold[2]) }, 10)
  check("Bond walks into the vault (E)", ok and bond.room(0) == E, "room " .. tostring(LEVEL.room_name[bond.room(0)]))
  local placed = 0
  for _, pad in ipairs(LEVEL.loot.vault_gold) do if spawn.item(pad, "goldbar") then placed = placed + 1 end end
  check("gold bars on the vault's loot pads", #LEVEL.loot.vault_gold == 6 and placed == 6, placed .. " of " .. #LEVEL.loot.vault_gold)
  stand(CE.pad_a, { CE.x, CE.z })
  door.snap(vd, false)
  wait(0.5)
  s, l = drawn()
  check("door.snap shuts the vault door and E is culled again", door.get(vd).open < 0.05 and not s[E], dstate(vd) .. " drawn {" .. l .. "}")
  print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed))
  mission.fail("levelgen objects test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("LEVELGEN error: " .. tostring(err)); print(string.format("LEVELGEN DONE %d passed, %d failed", passed, failed + 1)) end
end)
