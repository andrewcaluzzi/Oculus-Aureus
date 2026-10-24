-- Heist CCTV test (P1 T1 + T2): the camera feeds and the control-room intel, on the real tools/mode_heist.lua (Bunker 2).
-- Run: tests/heist/run_cctv.sh (headless; GE_FULLHEIGHT=0 GE_EDGEINSET=1 under Xvfb). Screenshots via cctv.test_shot() land in
-- build_pc/heist_cctv_shots/ (the "gfx: wrote shot_N.bmp" line follows each "HEIST SHOT <name>").
-- Covers: a real B press at the hall's screens opens the feeds (cctv_view event), the feed's room walk starts at the camera, Bond is held
-- (the stick does not move him), A / C-left cycle, a camera that is off shows static (offline), damage throws him out, B leaves (and the
-- held B does not reopen), cctv.view / view_off / viewing, the intel box (in room 6 and on a feed, not elsewhere), coverage (cctv.sees).
HEIST_SETTINGS = { test = true }
include("heist")
mods.set("invincible", true)   -- a hundred guards around a warping test Bond
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end

local views = {}
on("cctv_view", function(p, cam, why)
  views[#views + 1] = { p = p, cam = cam, why = why }
  print(string.format("HEIST event cctv_view p=%d cam=%d why=%d", p, cam, why))
end)
local function last_view() return views[#views] or {} end

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield() end
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function warp_xz(x, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 900)) do
    local px, _, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = p, d end
  end
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(0, best, x - px, z - pz)
end
local function has(t, v) for _, x in ipairs(t or {}) do if x == v then return true end end return false end
local function next_cam(list, cam, step)
  for i, c in ipairs(list) do if c == cam then return list[(i - 1 + step) % #list + 1] end end
end

local function shot(name)
  print("HEIST SHOT " .. name)
  cctv.test_shot()
  wait(0.5)
end

local function body()
  while not HEIST.ready do wait(0.25) end
  wait(1)
  local cams = cctv.list()
  check("six cameras", #cams == 6, "n=" .. #cams)
  check("camera names from the mode", cctv.name(95) == "BANKING HALL" and cctv.name(87) == "SECURITY WING", cctv.name(95))

  -- 1. a real use press (B) at the hall's screen wall opens the feeds
  warp_xz(-385, 470)
  bond.face(0, -477, 470)
  wait(1)
  check("Bond is in the control room (room 6)", bond.room(0) == 6, "room=" .. tostring(bond.room(0)))
  check("at a screen", HEIST.near_monitor(0))
  local lines = hud.panel_lines()
  check("intel box in the control room", #lines >= 7 and lines[1]:find("CONTROL ROOM") ~= nil, lines[1] or "none")
  cctv.test_press("b")
  wait(1)
  local v = cctv.viewing(0)
  check("a use press at the screens opens the feeds", v ~= nil, "cam=" .. tostring(v))
  check("cctv_view event (p, cam, why 0)", last_view().p == 0 and last_view().cam == v and last_view().why == 0)
  if not v then v = cams[1]; cctv.view(0, v); wait(0.5) end

  -- 2. the feed's room visibility is the camera's, not Bond's
  local ri = cctv.render_info()
  local cg = cctv.get(v)
  check("the feed's room walk starts in the camera's room", ri ~= nil and ri.cam == v and ri.start == cg.room,
        string.format("cam %s start %s camroom %s", tostring(ri and ri.cam), tostring(ri and ri.start), tostring(cg.room)))
  check("... and draws that room", ri ~= nil and has(ri.rooms, cg.room), "rooms=" .. (ri and table.concat(ri.rooms, ",") or ""))
  check("intel box on the feed", (hud.panel_lines()[1] or ""):find("CAMERA FEEDS") ~= nil, hud.panel_lines()[1] or "none")

  -- 3. Bond stands at the console: the stick does not move him
  local x0, _, z0 = bond.pos(0)
  for _ = 1, 8 do bond.walk(0, x0 + 600, z0); wait(0.25) end
  bond.walk_stop()
  local x1, _, z1 = bond.pos(0)
  check("held while watching (the stick is ignored)", d2(x0, z0, x1, z1) < 5, string.format("moved %.1f", d2(x0, z0, x1, z1)))
  check("still watching after the stick", cctv.viewing(0) == v)

  -- 4. cycling: A = next, C-left = previous
  cctv.test_press("a")
  wait(0.75)
  local want = next_cam(cams, v, 1)
  check("A: the next camera", cctv.viewing(0) == want and last_view().why == 1, "now " .. tostring(cctv.viewing(0)) .. " want " .. tostring(want))
  cctv.test_press("cl")
  wait(0.75)
  check("C-left: back to the previous camera", cctv.viewing(0) == v, "now " .. tostring(cctv.viewing(0)))
  cctv.test_press("cr")
  wait(0.75)
  check("C-right: the next camera", cctv.viewing(0) == want, "now " .. tostring(cctv.viewing(0)))

  -- 5. three feeds for the screenshots; each walk starts in that camera's room; people only the camera sees are drawn
  local most = 0
  for _, cam in ipairs({ 95, 87, 97 }) do
    cctv.view(0, cam)
    wait(1.5)
    local r = cctv.render_info()
    local g = cctv.get(cam)
    check("feed " .. cam .. " (" .. cctv.name(cam) .. ") walks from room " .. tostring(g.room), r ~= nil and r.cam == cam and r.start == g.room and #r.rooms > 0,
          r and string.format("start %d, %d rooms (%d geometry only), %d chrs posed: %s", r.start, #r.rooms, r.geom_only, r.chrs, table.concat(r.rooms, ",")) or "nil")
    if r and r.chrs > most then most = r.chrs end
    shot("feed_" .. cam)
  end
  check("the feeds show people Bond cannot see (posed for the camera)", most > 0, "most=" .. most)

  -- 6. a camera that is off shows static
  cctv.disable(91)
  cctv.view(0, 91)
  wait(1)
  local r6 = cctv.render_info()
  check("an offline camera's feed is static", r6 ~= nil and r6.offline == true and cctv.viewing(0) == 91, r6 and tostring(r6.offline) or "nil")
  shot("feed_91_offline")
  cctv.enable(91)
  wait(0.5)
  check("back on: no static", cctv.render_info().offline == false)

  -- 7. damage throws him out
  cctv.view(0, 95)
  wait(0.5)
  bond.heal(0, -0.1, 0)
  wait(0.5)
  check("hit: he leaves the feed (why 3)", cctv.viewing(0) == nil and last_view().cam == -1 and last_view().why == 3,
        "viewing " .. tostring(cctv.viewing(0)) .. " why " .. tostring(last_view().why))
  bond.heal(0, 1, 1)

  -- 8. B leaves; the B still held does not reopen the feeds
  wait(1)
  cctv.test_press("b")
  wait(1)
  check("B at the screens again: watching", cctv.viewing(0) ~= nil)
  cctv.test_press("b")
  wait(1.5)
  check("B leaves the feed (why 2)", cctv.viewing(0) == nil and last_view().why == 2, "why " .. tostring(last_view().why))
  check("Bond moves again after leaving", (function()
    local ax, _, az = bond.pos(0)
    for _ = 1, 6 do bond.walk(0, ax + 300, az); wait(0.25) end
    bond.walk_stop()
    local bx, _, bz = bond.pos(0)
    return d2(ax, az, bx, bz) > 20
  end)())

  -- 9. the API
  check("cctv.view", cctv.view(0, 87) == true and cctv.viewing(0) == 87)
  check("cctv.view_off", cctv.view_off(0) == true and cctv.viewing(0) == nil)
  check("cctv.view_off again: false", cctv.view_off(0) == false)
  check("cctv.view: not a camera", cctv.view(0, 75) == false)

  -- 10. the intel box: not outside the control room, back on a feed anywhere
  bond.warp(0, 104)   -- the west wing
  wait(1)
  check("no intel box outside the control room", #hud.panel_lines() == 0, "lines=" .. #hud.panel_lines())
  cctv.view(0, 95)
  wait(1)
  local il = hud.panel_lines()
  local joined = table.concat(il, " | ")
  check("intel on a feed from anywhere", #il >= 7, joined)
  check("intel: vault, cards, guards", joined:find("VAULT:") and joined:find("GREEN") and joined:find("BLUE") and joined:find("RED")
        and joined:find("GOLD") and joined:find("GUARDS") and joined:find("alarm:"), joined)
  print("HEIST intel: " .. joined)
  cctv.view_off(0)

  -- 11. coverage: the cameras see someone (they sweep: poll a while); a camera that is off sees nothing
  local any, seen_by = false, nil
  for _ = 1, 40 do
    for _, cam in ipairs(cams) do
      for _, g in ipairs(HEIST.guards) do if cctv.sees(cam, g.chr) then any, seen_by = true, cam end end
    end
    if any then break end
    wait(0.25)
  end
  check("a working camera sees a guard (cctv.sees)", any, "camera " .. tostring(seen_by))
  if not any then   -- diagnostics: camera 97 against the east wing guards
    local g97 = cctv.get(97)
    print(string.format("HEIST dbg cam97 %.0f,%.0f face %.0f room %d", g97.x, g97.z, g97.face, g97.room))
    local k = 0
    for _, g in ipairs(HEIST.guards) do
      if g.area == "east" and k < 6 then
        k = k + 1
        local x, z = spawn.where(g.chr)
        local b = math.deg(math.atan(x - g97.x, z - g97.z)) % 360
        print(string.format("HEIST dbg guard %d at %.0f,%.0f bearing %.0f room %s sees %s", g.chr, x or 0, z or 0, b, tostring(spawn.room(g.chr)), tostring(cctv.sees(97, g.chr))))
      end
    end
  end
  cctv.disable(95)
  local none = true
  for _, g in ipairs(HEIST.guards) do if cctv.sees(95, g.chr) then none = false end end
  check("a camera that is off sees nothing", none)
  cctv.enable(95)
  wait(1)
  local up = (hud.panel_lines()[1] or "")
  check("the box is gone after leaving the feed (west wing)", up == "", up)

  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished_ok = true
  mission.fail("cctv test done")
end

local co = coroutine.wrap(body)
local finished = false
on("tick", function(dt)
  if finished or finished_ok then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST cctv error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
