-- heist_traits.lua (test) - guard personalities (designs/bank-heist.md section 32), the real mode on the bank, one scenario per run
-- (tests/heist/run_traits.sh):
--   GE_TR_DEAL=1     the deal: every posted guard / raiser / carrier has a trait, several kinds dealt, the values differ from the base;
--                    prints "HEIST TRAITS DEAL <signature>" (the shell compares seeds: the same seed deals the same, another seed not)
--   GE_TR_ROOKIE=1   two test guards the same distance from a noise: the rookie's awareness rises more; he radios it in
--   GE_TR_BOOK=1     a faint noise: a plain guard looks from where he stands, a by-the-book guard walks over and searches
--   GE_TR_PATROL=1   a long patrol: a veteran on a watchclock tour skips at least one station, a by-the-book guard on another skips none;
--                    Bond at the skipped station while the gap is open: "Clocked Out"
--   GE_TR_BRIBE=1    the BLUE carrier (bribable) takes the money face to face: he hands over BLUE, the cost goes on the books; a
--                    suspicious bribable guard refuses; the payout at the end is less the bribe (and the debrief says "Paid Off")
--   GE_TR_OFF=1      personalities left out: no layer, no deal, nothing in ai.traits()
--   GE_TR_SHOT=1     the PERSONNEL page with the GUARD FILE after watching the hall (a screenshot)
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE".
local RUN = "deal"
for _, r in ipairs({ "rookie", "book", "patrol", "bribe", "off", "shot" }) do
  if mode.env_flag("GE_TR_" .. r:upper()) then RUN = r end
end
HEIST_SETTINGS = {
  test = true, jam_chance = 0, jam_every = 1000, investigators = 0, generator_after = 0, station_late = 900,
  ai_tune = { checkin = 0, backup = 0 },
  personalities = RUN ~= "off",
  traits_tune = { skip = 0.5 },
}
if RUN == "shot" then HEIST_SETTINGS.intel = true end
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local real_fail = mission.fail
local ended = {}
mission.win = function(msg) ended.win = msg; return true end
mission.fail = function(msg) ended.fail = msg; return true end

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 1) end
end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function pad(n) return LEVEL.pads[n] end
local barks = {}
on("guard_bark", function(c, kind, text) barks[#barks + 1] = { c = c, kind = kind, text = text, t = HEIST.elapsed } end)
local function said(c, pat) for _, b in ipairs(barks) do if b.c == c and (not pat or tostring(b.text):find(pat, 1, true)) then return b end end end
local function info(c) return ai.info(c) or { state = -1, name = "?", sub = "?", level = 0, face = 0 } end
local function entry(c) for _, e in ipairs(ai.traits()) do if e.chr == c then return e end end end

local function pad_near(x, y, z)
  local best, bd = nil, 1e9
  for _, p in ipairs(pads.far_from(x, z, 0, 600)) do
    local px, py, pz = pads.pos(p)
    local d = d2(px, pz, x, z)
    if d < bd and math.abs(py - y) < 100 then best, bd = p, d end
  end
  return best
end
local function warp_xz(p, x, y, z)
  local best = pad_near(x, y, z)
  if not best then return false end
  local px, _, pz = pads.pos(best)
  return bond.warp(p, best, x - px, z - pz)
end
local function guard_at(x, y, z, fx, fz)
  local best = pad_near(x, y, z)
  if not best then return nil end
  local px, _, pz = pads.pos(best)
  local c = spawn.chr(best, { post = { fx, fz }, weapon = "kf7", body = "St_Petersburg_Guard", offset = { x - px, z - pz }, hearing = 1.0 })
  if not c then return nil end
  spawn.role(c, "hall")
  cover.guard(c, "guard")
  ai.guard(c, 1)
  return c
end
local function clear_area(area)
  for _, g in ipairs(HEIST.guards) do if g.area == area then spawn.remove(g.chr) end end
  for _, c in ipairs(HEIST.civs) do if c.area == area then spawn.remove(c.chr) end end
end
local function away() bond.warp(0, pad("street_e") or HEIST.M.P.start[1]) end

-- ---------------------------------------------------------------------------------------------------------------------------------

local function deal_path()
  local list = ai.traits()
  local want, kinds, sig = 0, {}, {}
  for _, g in ipairs(HEIST.guards) do if g.kind == "post" or g.kind == "raiser" or g.kind == "carrier" then want = want + 1 end end
  for _, e in ipairs(list) do kinds[e.trait] = (kinds[e.trait] or 0) + 1; sig[#sig + 1] = e.chr .. ":" .. e.trait:sub(1, 1) end
  local nk = 0
  for _ in pairs(kinds) do nk = nk + 1 end
  check("the layer is on", ai.trait_on())
  check("every posted guard, raiser and carrier has a trait", #list == want and want > 10, string.format("%d of %d", #list, want))
  check("several kinds are dealt", nk >= 4, string.format("rookie %d veteran %d book %d bribable %d none %d", kinds.rookie or 0, kinds.veteran or 0,
        kinds.book or 0, kinds.bribable or 0, kinds.none or 0))
  local function first(t) for _, e in ipairs(list) do if e.trait == t then return e.chr end end end
  local r, v, b = first("rookie"), first("veteran"), first("book")
  check("a rookie hears more and falls back sooner", r and ai.trait_val(r, "hear") > 1 and ai.trait_val(r, "retreat") > 1 and ai.trait_val(r, "susp") > 1)
  check("a veteran is slow to turn and holds his ground", v and ai.trait_val(v, "turn") > 1 and ai.trait_val(v, "retreat") < 1)
  check("a by-the-book guard searches fully, with more backup", b and ai.trait_val(b, "full") == 1 and ai.trait_val(b, "backup") >= 1)
  print("HEIST TRAITS DEAL " .. table.concat(sig, ","))
end

local function rookie_path()
  clear_area("hall")
  mode.body_check(false)
  away()
  local x, y, z = pads.pos(pad("hall_mid"))
  wait(0.5)
  local a = guard_at(x - 250, y, z, x - 250, z + 500)
  local b = guard_at(x + 250, y, z, x + 250, z + 500)
  wait(1.0)
  check("two test guards in the hall", a ~= nil and b ~= nil)
  if not (a and b) then return end
  ai.trait(a, "rookie")
  ai.trait(b, "none")
  local l0a, l0b = info(a).level, info(b).level
  noise.emit(x, y, z - 300, 1400, "door")   -- the same distance from both (about 390)
  local la, lb = info(a).level - l0a, info(b).level - l0b
  check("the rookie's awareness rises more than the plain guard's", la > lb * 1.4 and lb > 0, string.format("rookie +%.3f, plain +%.3f", la, lb))
  local radioed = wait_for(4, function() return said(a, "I think I saw something") ~= nil end)
  check("the rookie radios his suspicion in", radioed, said(a, "I think") and said(a, "I think").text or "none")
  check("... the plain guard does not", said(b, "I think I saw something") == nil)
  check("the rookie's line is his own (\"W-what was that?\")", said(a, "W-what") ~= nil, said(a) and said(a).text or "none")
  check("the trait stats count the radio", ai.trait_stats().radios >= 1, tostring(ai.trait_stats().radios))
end

local function book_path()
  clear_area("hall")
  mode.body_check(false)
  away()
  local x, y, z = pads.pos(pad("hall_mid"))
  wait(0.5)
  local a = guard_at(x - 250, y, z, x - 250, z + 500)
  local b = guard_at(x + 250, y, z, x + 250, z + 500)
  wait(1.0)
  check("two test guards in the hall", a ~= nil and b ~= nil)
  if not (a and b) then return end
  ai.trait(a, "book")
  ai.trait(b, "none")
  noise.emit(x, y, z - 300, 760, "door")   -- faint: about 0.24 of awareness each (over 0.15 suspicious, under 0.3 search)
  local la, lb = info(a).level, info(b).level
  check("a faint noise: both are suspicious, neither past the search level", la >= 0.15 and la < 0.3 and lb >= 0.15 and lb < 0.3,
        string.format("book %.2f, plain %.2f", la, lb))
  wait(2.25)
  local ia, ib = info(a), info(b)
  check("the plain guard looks from where he stands", ib.state == 1 and ib.sub == "look", string.format("%s %s", ib.name, ib.sub))
  check("the by-the-book guard walks over and searches", ia.state >= 2 and (ia.sub == "go" or ia.sub == "look"), string.format("%s %s", ia.name, ia.sub))
  local far = 0
  wait_for(8, function()
    local x1, z1 = spawn.where(a)
    local xb, zb = spawn.where(b)
    far = d2(x1, z1, x - 250, z)
    return far > 150
  end)
  local xb, zb = spawn.where(b)
  check("... he walks off his post towards it", far > 150, string.format("%.0f from his post", far))
  check("... the plain guard never left his", d2(xb, zb, x + 250, z) < 60, string.format("%.0f from his post", d2(xb, zb, x + 250, z)))
end

local function patrol_path()
  local tours = {}
  for _, t in ipairs(HEIST_AI.tours or {}) do if #t.stations >= 1 and spawn.alive(t.chr) then tours[#tours + 1] = t end end
  check("two watchclock tours", #tours >= 2, tostring(#tours))
  if #tours < 2 then return end
  -- by the book: the tour with the most stations; veterans: every other tour with a station
  table.sort(tours, function(a, b) return #a.stations > #b.stations or (#a.stations == #b.stations and a.chr < b.chr) end)
  local book = tours[1]
  local vets = {}
  for i = 2, #tours do vets[#vets + 1] = tours[i]; ai.trait(tours[i].chr, "veteran") end
  ai.trait(book.chr, "book")
  away()
  local punches, last = 0, book.punched
  local vet
  local skipped = wait_for(240, function()
    if book.punched ~= last then punches, last = punches + 1, book.punched end
    for _, t in ipairs(vets) do local e = entry(t.chr); if e and e.skips >= 1 then vet = t; return true end end
  end)
  local ev = vet and entry(vet.chr)
  check("a veteran skips at least one station on a long patrol", skipped and ev ~= nil,
        vet and string.format("%d skips (%s, %d stations) after %.0f s", ev.skips, vet.route, #vet.stations, HEIST.elapsed) or "none")
  wait_for(60, function() if book.punched ~= last then punches, last = punches + 1, book.punched end; return punches >= 3 end)
  local eb = entry(book.chr)
  check("the by-the-book guard punches his stations and never skips one", eb and eb.skips == 0 and punches >= 2,
        string.format("%d skips, %d punches (%s, %d stations)", eb and eb.skips or -1, punches, book.route, #book.stations))
  if not vet then return end
  -- the gap: be at the station he skipped while it is open
  local open = wait_for(120, function() local e = entry(vet.chr); return e and e.skip_pad ~= nil end)
  ev = entry(vet.chr)
  check("a skipped station's gap is open", open and ev.skip_pad ~= nil, tostring(ev and ev.skip_pad))
  if open then
    bond.warp(0, ev.skip_pad)
    local got = wait_for(3, function() return ai.trait_stats().clocked >= 1 end)
    check("Bond at the skipped station while the gap is open: Clocked Out", got, tostring(ai.trait_stats().clocked))
    away()
  end
  print(string.format("HEIST   skips %d, clocked %d", ai.trait_stats().skips, ai.trait_stats().clocked))
end

-- Bond stands right in front of guard c, facing him
local function face_to_face(c, floor_y)
  local cx, cz = spawn.where(c)
  local _, cy = gadget.chr_yaw(c)
  local gyaw = gadget.chr_yaw(c)
  local a = math.rad(gyaw or 0)
  local ok = warp_xz(0, cx + math.sin(a) * 100, floor_y, cz + math.cos(a) * 100)
  test.face(0, cx, cz)
  local bx, by, bz = bond.pos(0)
  print(string.format("HEIST   face to face with chr %d (yaw %.0f, y %s): warp %s, Bond %.0f from him, dy %.0f", c, gyaw or -1, tostring(cy), tostring(ok),
        d2(bx, bz, cx, cz), by - (cy or 0)))
end

local function bribe_path()
  local blue = HEIST.carriers.chief or HEIST.carriers.security_chief
  if not blue then for role, c in pairs(HEIST.carriers) do if c.flags == 0x4 then blue = c end end end
  check("the BLUE carrier is up", blue and blue.chr and spawn.alive(blue.chr))
  if not (blue and blue.chr) then return end
  ai.trait(blue.chr, "bribable")
  face_to_face(blue.chr, select(2, pads.pos(blue.pad)))
  wait(0.3)
  local s0 = cover.suspicion(0, blue.chr) or 0
  local ok, r = HEIST_TRAITS.try_bribe(0)
  check("face to face, unsuspicious: he takes the money", ok and r == "ok", string.format("%s %s, meter %.2f", tostring(ok), tostring(r), s0))
  check("... and hands over his card (BLUE)", bond.has_key(0, 0x4))
  local st = ai.trait_stats()
  check("the bribe is on the books", st.bribes == 1 and st.bribe_cost == HEIST.S.bribe_cost, string.format("%d bribes, %d", st.bribes, st.bribe_cost))
  check("he is paid off (bribed: he looks away)", entry(blue.chr).bribed and ai.trait_val(blue.chr, "susp") == 0)
  wait(3)
  local cs = cover.state(blue.chr)
  check("standing in front of him with the card, his meter stays down", cs and cs.susp < 0.05 and cs.witness == 0, cs and string.format("%.2f", cs.susp) or "-")
  -- a suspicious bribable guard refuses
  clear_area("hall")
  away()
  wait(0.5)
  local x, y, z = pads.pos(pad("hall_mid"))
  local g = guard_at(x, y, z, x, z + 500)
  wait(1)
  ai.trait(g, "bribable")
  local heard = noise.emit(x, y, z - 300, 1400, "door")
  print(string.format("HEIST   the noise: %d heard, his awareness %.2f", heard, info(g).level))
  wait(0.5)
  face_to_face(g, y)
  wait(0.2)
  local ok2, r2 = HEIST_TRAITS.try_bribe(0)
  check("a guard already suspicious refuses the money", ok2 and r2 == "refused", string.format("%s %s, awareness %.2f", tostring(ok2), tostring(r2), info(g).level))
  check("... and nothing more comes off the payout", ai.trait_stats().bribe_cost == HEIST.S.bribe_cost)
  -- the payout: loot + quiet bonus - the bribe
  away()
  HEIST.loot = 1000000
  HEIST.bars = 6
  HEIST.finish(true, "test")
  local want = 1000000 + math.floor(1000000 * HEIST.S.quiet_bonus) - HEIST.S.bribe_cost
  check("the payout is less the bribe", HEIST.score == want, string.format("score %d, want %d", HEIST.score or -1, want))
end

local function off_path()
  check("personalities off: the layer is off", not ai.trait_on())
  check("... nothing dealt", #ai.traits() == 0 and HEIST.traits == nil)
  local any = HEIST.guards[1] and HEIST.guards[1].chr
  check("... a guard's values are the base ones", any and ai.trait(any) == "none" and ai.trait_val(any, "hear") == 1 and ai.trait_val(any, "pads") == 0)
end

local function shot_path()
  bond.warp(0, pad("hall_mid"))
  test.face(0, select(1, pads.pos(pad("hall_mid"))), select(3, pads.pos(pad("hall_mid"))) - 800)
  local seen = wait_for(6, function() local n = 0; for _, e in ipairs(ai.traits()) do if e.observed then n = n + 1 end end; return n >= 4 end)
  local n = 0
  for _, e in ipairs(ai.traits()) do if e.observed then n = n + 1 end end
  check("from the hall the crew has had a look at some guards", n >= 2, tostring(n))
  wait(1)
  print("HEIST SHOT traits_ar")
  test.screenshot()
  wait(0.6)
  test.intel_show(true, 4, 0)
  wait(1.2)
  print("HEIST SHOT traits_personnel")
  test.screenshot()
  wait(0.6)
  test.intel_show(false)
end

local finished = false
local function body()
  local ready = wait_for(25, function() return HEIST.ready end)
  check("mode ready", ready, "run " .. RUN)
  wait(1.5)
  if RUN == "deal" then deal_path() elseif RUN == "rookie" then rookie_path() elseif RUN == "book" then book_path()
  elseif RUN == "patrol" then patrol_path() elseif RUN == "bribe" then bribe_path() elseif RUN == "off" then off_path()
  elseif RUN == "shot" then shot_path() end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  real_fail("traits test done")
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
