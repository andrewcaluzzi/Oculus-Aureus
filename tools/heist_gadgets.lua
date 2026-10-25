-- heist_gadgets.lua: the crew's spycraft gadgets for the bank heist (designs/bank-heist.md section 21). Included at the end of
-- tools/mode_heist.lua (shipped as Data/Scripts/modes/heist_gadgets.lua). The C side is port/src/port_gadgets.c (the trigger of a gadget
-- in hand, the piton line that carries Bond, the pickpocket's lift, the disguise the cover reads, the overlay); the Lua API is gadget.*.
--
-- What the crew carries (the loadout, S.gadgets in mode_heist.lua, or HEIST_SETTINGS.gadgets; DEFAULT below):
--   PITON GUN  (in hand: fire)   at a grapple point (pads grapple_<id>_low / _high) fire and the line carries you up to the high pad in
--                                about 2 s; at the top fire again to abseil down. Seen doing it by a guard or a camera: your cover is blown.
--   LOCKPICKS  (in hand: fire)   hold fire at a locked keyed door (not the vault, not a "never" door): GREEN 6 s, the floor key 8 s, BLUE
--                                10 s, RED 14 s. Let go and the progress stays. Tap fire to rush it: each tap is quicker but rattles
--                                (a "door" noise: a guard near comes to look). Picking a lock in a guard's view blows your cover.
--   COINS      (in hand: fire)   throw one: where it lands it makes a "coin" noise: the nearest guards walk over to look, then go back.
--   TAPE LOOP  (in hand: fire)   aim at a camera and fire: it shows a recorded loop and sees nothing for 60 s.
--   SLEEP GAS  (in hand: fire)   throw it: 3 s later everyone near where it landed is knocked out.
--              At a safe the lockpicks are a stethoscope: three tumblers, press fire while the sweeping needle is in the green.
--   THERMAL    (in hand: fire)   10 s of every chr within 35 m outlined through the walls (red armed, white not); 30 s to recharge.
--   PICKPOCKET (use)             behind a guard or a staffer who carries a card, who does not face you and is not on to you: press use
--                                within reach and the card is yours. Facing you: he catches your hand (a guard sees through you).
--   DISGUISE   (use)             at a knocked-out, searched body: press use and stay with it (4 s, unseen): you wear its uniform. A
--                                guard's: restricted and staff rooms are yours (guards look closer when you linger near them); a staffer's:
--                                the staff rooms.
--   VENTS      (use)             at a vent (pads vent_<id>_a / _b): press use and you crawl through to the other end. The vault's
--                                ventilator (vent_vault) only while the power is out (its fans stop).
-- The points are found by pad name on any layout (bank v2's and v3's; v3's heist_bank_map() also lists them as M.GRAPPLES / M.VENTS).
-- Gadget state is sim state: it is in this script's text state (mode.state, hashed) and the Lua arena (the rollback snapshot); the C
-- side's (triggers, the line, disguises) in port_gadgets.c's hash. Noises go through noise.emit when the AI layer has it, else the
-- nearest cover guards are walked over to look (spawn.go) and back.

local H = HEIST
local S = H.S
local G = { players = {}, pairs = {}, vents = {}, coins = {}, gas = {}, loops = {}, pulled = {}, picked = {}, never = {}, searched = {},
            used_bodies = {}, log = {}, stats = { grapples = 0, picks = 0, lifts = 0, coins = 0, loops = 0, disguises = 0, vents = 0, gassed = 0,
            noises = 0, blown = 0 } }
GADGETS = G

-- the stock items they borrow (in hand; port_gadgets.c shows the gadgets' own names): pitongun, gaskeyring, creditcard, datathief,
-- bombdefuser, camera
local ITEM = { piton = 36, lockpick = 54, coin = 53, loop = 55, gas = 39, thermal = 40 }
local GIVE = { piton = "pitongun", lockpick = "gaskeyring", coin = "creditcard", loop = "datathief", gas = "bombdefuser", thermal = "camera" }

local DEFAULT = {
  piton = true, lockpick = true, coins = 4, loops = 2, gas = 1, thermal = true, pickpocket = true, disguise = true, vents = true,
  -- tuning
  grapple_reach = 240,      -- units from the end pad you stand at
  grapple_speed = 650,      -- units a second up the line (down: 1.4x)
  pick_time = { [0x2] = 6, [0x1] = 8, [0x4] = 10, [0x8] = 14 },   -- seconds by the key the door wants (others: pick_other)
  pick_other = 12,
  pick_reach = 190,
  pick_hurry = 0.6,         -- seconds a tap adds
  pick_noise = 650,         -- the rattle's radius
  coin_range = 900,
  coin_noise = 900,
  coin_guards = 2,          -- guards a coin pulls (without the AI layer's noise.emit)
  pull_return = 12,         -- seconds a pulled guard looks before he walks back
  loop_time = 60,
  loop_range = 1400,
  loop_cone = 22,           -- degrees off the aim
  gas_range = 700,
  gas_fuse = 3,
  gas_radius = 330,
  pick_pocket_reach = 120,
  behind = 100,             -- degrees off his facing that count as behind him
  disguise_time = 4,
  disguise_reach = 150,
  vent_reach = 120,
  vent_time = 3,
  thermal_time = 10,        -- seconds the thermal imager shows every chr near through the walls
  thermal_recharge = 30,
  safe_reach = 320,         -- the stethoscope (lockpicks at a safe)
  safe_tumblers = 3,
  safe_period = 72,         -- ticks for the needle to sweep the dial
  safe_window = 0.16,       -- the click window, a fraction of the dial
  safe_noise = 400,
}

local T = {}
for k, v in pairs(DEFAULT) do T[k] = v end
if type(S.gadgets) == "table" then for k, v in pairs(S.gadgets) do T[k] = v end end
G.T = T

local GREEN, BLUE, RED, GOLD, FLOORKEY, VAULT = 0x2, 0x4, 0x8, 0x10, 0x1, 0x40000000
local KEYNAME = { [GREEN] = "GREEN", [BLUE] = "BLUE", [RED] = "RED", [GOLD] = "GOLD", [FLOORKEY] = "floor key" }

local function d2(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return math.sqrt(dx * dx + dz * dz) end
local function log(fmt, ...) print(string.format("ge: heist: gadgets: " .. fmt, ...)) end
local function wrap(a) while a > 180 do a = a - 360 end while a < -180 do a = a + 360 end return a end
local function deg(r) return r * 180 / math.pi end
local function flash_p(p, text, secs) H.pflash[p] = { text, H.elapsed + (secs or 4) }; G.players[p].my = nil end

-- the HUD line for the gadget in hand (line 3, under any flash of the heist's own)
local function hint(p, text)
  local pf, me = H.pflash[p], G.players[p]
  if pf and H.elapsed < pf[2] and me.my ~= pf then return end
  me.my = { text, H.elapsed + 0.6 }
  H.pflash[p] = me.my
end

local function guards_up(kinds)
  local out = {}
  for _, g in ipairs(H.guards) do
    if (not kinds or kinds[g.kind]) and spawn.alive(g.chr) and not spawn.down(g.chr) then out[#out + 1] = g end
  end
  return out
end
local COVER_KINDS = { post = true, raiser = true, carrier = true, investigator = true }

local function nearest_pad(x, z, maxd, waypoint)
  local best, bd = nil, 1e9
  for _, pd in ipairs(pads.far_from(x, z, 0, maxd or 500, waypoint)) do
    local px, _, pz = pads.pos(pd)
    local d = d2(px, pz, x, z)
    if d < bd then best, bd = pd, d end
  end
  return best
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- noise: the AI layer's noise.emit if it is there, else the nearest cover guards walk over to look and back

local function make_noise(x, y, z, radius, kind, p, n)
  G.stats.noises = G.stats.noises + 1
  local ai_noise = rawget(_G, "noise")
  if type(ai_noise) == "table" and ai_noise.emit then   -- (the AI layer's hearing)
    ai_noise.emit(x, y, z, radius, kind)
    return -1
  end
  local cands = {}
  for _, g in ipairs(guards_up(COVER_KINDS)) do
    local gx, gz = spawn.where(g.chr)
    if gx and d2(gx, gz, x, z) <= radius and not G.pulled_by[g.chr] and not ko.is(g.chr) then
      cands[#cands + 1] = { g = g, d = d2(gx, gz, x, z), x = gx, z = gz }
    end
  end
  table.sort(cands, function(a, b) return a.d < b.d end)
  local to = nearest_pad(x, z, 500)
  local sent = 0
  if not to then return 0 end
  for i = 1, math.min(n or 1, #cands) do
    local c = cands[i]
    local home = nearest_pad(c.x, c.z, 300)
    if home and spawn.go(c.g.chr, to, false) then
      G.pulled[#G.pulled + 1] = { chr = c.g.chr, home = home, back_at = H.elapsed + T.pull_return + c.d / 150 }
      G.pulled_by[c.g.chr] = true
      sent = sent + 1
      local ar = rawget(_G, "ar")
      if type(ar) == "table" and ar.say then ar.say(c.g.chr, kind == "coin" and "Huh? What was that?" or "Who's there?", 2) end
    end
  end
  log("%s noise at (%.0f %.0f) r %d: %d guard(s) come to look (%d in earshot)", kind, x, z, radius, sent, #cands)
  return sent
end
G.pulled_by = {}

local function pulled_tick()
  local keep = {}
  for _, e in ipairs(G.pulled) do
    if H.elapsed >= e.back_at then
      if spawn.alive(e.chr) and not spawn.down(e.chr) then spawn.go(e.chr, e.home, false) end
      G.pulled_by[e.chr] = nil
    else
      keep[#keep + 1] = e
    end
  end
  G.pulled = keep
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- seen doing something odd: an act in a cover guard's view (every guard who sees it sees through him), a camera's report to the security
-- room

-- a camera in the room he climbs from or to (rooms: a set by room id) that has him in sight for `need` seconds of Lua ticks
local function cam_report(p, why, rooms, need)
  local me = G.players[p]
  me.camseen = me.camseen or {}
  if not S.cover or H.hot or H.ended or H.report then return false end
  for _, cam in ipairs(cctv.list()) do
    local c = cctv.get(cam)
    local sees = c and not c.disabled and not G.loops[cam] and (not rooms or rooms[c.room]) and cctv.sees(cam, bond.id(p))
    me.camseen[cam] = sees and (me.camseen[cam] or 0) + 0.25 or nil
    if sees and me.camseen[cam] >= (need or 0) then
      if H.security_up() == 0 then return false end
      H.report = { at = H.elapsed + S.security_delay, cam = cam, p = p }
      log("camera %d (%s) saw Bond %d %s: the security room will raise the alarm in %d s", cam, cctv.name(cam), p, why, S.security_delay)
      flash_p(p, "A camera saw you " .. why .. "! The security room will raise the alarm", 4)
      return true
    end
  end
  return false
end

local function seen_act(p, why)
  if not S.cover or cover.alert() then return false end
  if cover.seen(p) then
    cover.test_act(p)
    G.stats.blown = G.stats.blown + 1
    log("Bond %d seen %s: an act in view", p, why)
    return true
  end
  return false
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the piton gun

local LABEL = { roof = "the roof", gallery = "the gallery", alley = "the archive window", canal = "the embankment",
                vault = "the ductwork", facade = "the facade", lantern_w = "the west lantern", lantern_e = "the east lantern",
                liftshaft = "the lift shaft" }

-- the layout room a point is in (its rect, the floor nearest below)
local function room_at(x, y, z)
  local best, bd
  for id = 1, #(LEVEL.room_name or {}) do
    local r = LEVEL.rooms[LEVEL.room_name[id]]
    if r and r.rect and x >= r.rect[1] and x <= r.rect[3] and z >= r.rect[2] and z <= r.rect[4] then
      local d = math.abs(y - (r.floor or 0))
      if y >= (r.floor or 0) - 900 and y <= (r.floor or 0) + (r.height or 400) and (not bd or d < bd) then best, bd = r.id, d end
    end
  end
  return best
end

local function load_pairs()
  G.pairs, G.vents = {}, {}
  local names = {}
  for n, _ in pairs(LEVEL.pads or {}) do names[#names + 1] = n end
  table.sort(names)
  for _, n in ipairs(names) do
    local id = n:match("^grapple_(.+)_low$")
    if id and LEVEL.pads["grapple_" .. id .. "_high"] then
      local lo, hi = LEVEL.pads[n], LEVEL.pads["grapple_" .. id .. "_high"]
      local lx, ly, lz = pads.pos(lo)
      local hx, hy, hz = pads.pos(hi)
      if lx and hx then
        G.pairs[#G.pairs + 1] = { id = id, lo = lo, hi = hi, l = { lx, ly, lz }, h = { hx, hy, hz }, label = LABEL[id] or id,
                                  lroom = room_at(lx, ly, lz), hroom = room_at(hx, hy, hz) }
      end
    end
    local vid = n:match("^vent_(.+)_a$")
    if vid and LEVEL.pads["vent_" .. vid .. "_b"] then
      local a, b = LEVEL.pads[n], LEVEL.pads["vent_" .. vid .. "_b"]
      local ax, ay, az = pads.pos(a)
      local bx, by, bz = pads.pos(b)
      if ax and bx then G.vents[#G.vents + 1] = { id = vid, a = a, b = b, pa = { ax, ay, az }, pb = { bx, by, bz } } end
    end
  end
  log("%d grapple points, %d vents", #G.pairs, #G.vents)
end

-- the grapple Bond p can take from where he stands: { pair, up, anchor }
local function grapple_target(p)
  local yaw, pitch, x, y, z, feet = gadget.aim(p)
  if not yaw then return nil end
  local best, bd
  for _, g in ipairs(G.pairs) do
    for _, up in ipairs({ true, false }) do
      local here, there = up and g.l or g.h, up and g.h or g.l
      local d = d2(x, z, here[1], here[3])
      if d <= T.grapple_reach and math.abs(feet - here[2]) < 140 then
        -- roughly facing it (straight up does not need aiming)
        local hd = d2(x, z, there[1], there[3])
        local a = deg(math.atan(-(there[1] - x), there[3] - z))
        if hd < 350 or math.abs(wrap(a - yaw)) < 70 then
          if not bd or d < bd then best, bd = { pair = g, up = up, anchor = there }, d end
        end
      end
    end
  end
  return best
end
G.grapple_target = grapple_target

local function grapple_go(p, t)
  local _, _, x, _, z, feet = gadget.aim(p)
  local g, path, secs, from, to = t.pair, nil, nil, nil, nil
  if t.up then
    local top = g.h[2] + 70
    path = { { x, feet, z }, { x, top, z }, { g.h[1], top, g.h[3] }, { g.h[1], g.h[2], g.h[3] } }
    from, to = g.lo, g.hi
  else
    local top = math.max(g.h[2], feet) + 70
    path = { { x, feet, z }, { x, top, z }, { g.l[1], top, g.l[3] }, { g.l[1], g.l[2], g.l[3] } }
    from, to = g.hi, g.lo
  end
  local len = 0
  for i = 2, #path do
    local a, b = path[i - 1], path[i]
    len = len + math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2)
  end
  secs = len / (T.grapple_speed * (t.up and 1 or 1.4))
  if secs < 1.4 then secs = 1.4 elseif secs > 4 then secs = 4 end
  if not gadget.reel(p, path, secs, from, to) then return false end
  local me = G.players[p]
  me.reel = { id = g.id, up = t.up, until_t = H.elapsed + secs, to = to, rooms = { [g.lroom or -1] = true, [g.hroom or -2] = true } }
  G.stats.grapples = G.stats.grapples + 1
  log("Bond %d: piton %s to %s (%s, %.1f s, %.0f units)", p, t.up and "UP" or "DOWN", t.up and g.label or ("pad " .. to), g.id, secs, len)
  flash_p(p, t.up and ("Piton fired: up to " .. g.label) or "Abseiling down", 2)
  return true
end
G.grapple_go = grapple_go

local function reel_tick(p, me)
  local on, frac, done = gadget.reeling(p)
  if me.reel then
    -- on the line: anyone who sees a man going up the wall sees through him; a camera reports him (in a vent nobody sees him)
    if not me.reel.vent then
      seen_act(p, "on the piton line")
      cam_report(p, "on the piton line", me.reel.rooms, 0.5)
    end
    if not on then
      log("Bond %d: off the line at pad %d (reel %d)", p, me.reel.to, done)
      me.reel = nil
    end
  end
  return on
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- lockpicks

local function door_class(d)
  local g = door.get(d)
  if not g or not g.live then return nil end
  if g.keyflags == 0 or (g.keyflags & VAULT) ~= 0 or g.padlocked or G.never[d] then return nil end
  return g
end

local function pick_target(p)
  local yaw, _, x, y, z, feet = gadget.aim(p)
  if not yaw then return nil end
  local best, bd
  for _, d in ipairs(door.list()) do
    local g = door_class(d)
    if g and math.abs(g.y - feet) < 260 then
      local dd = d2(x, z, g.x, g.z)
      if dd <= T.pick_reach and (not bd or dd < bd) and not bond.has_key(p, g.keyflags) and g.open < 0.3 then
        local a = deg(math.atan(-(g.x - x), g.z - z))
        if dd < 90 or math.abs(wrap(a - yaw)) < 75 then best, bd = d, dd end
      end
    end
  end
  return best
end
G.pick_target = pick_target

local function pick_need(flags)
  for _, k in ipairs({ RED, BLUE, FLOORKEY, GREEN }) do
    if (flags & k) ~= 0 then return T.pick_time[k] or T.pick_other, KEYNAME[k] end
  end
  return T.pick_other, "keyed"
end

local function pick_done(p, d)
  local g = door.get(d)
  door.set_keyflags(d, 0)
  if g and g.linked and g.linked >= 0 then door.set_keyflags(g.linked, 0); door.open(g.linked) end
  door.open(d)
  G.picked[d] = nil
  G.stats.picks = G.stats.picks + 1
  log("Bond %d picked the lock of door %d", p, d)
  flash_p(p, "Click. The lock is open", 3)
end

-- the stethoscope: lockpicks at a safe. Three tumblers; a needle sweeps the dial and each tumbler clicks in a window: press fire
-- while the needle is in it (green). A miss rattles the door (a small "door" noise).
local function safe_target(p)
  local _, _, x, _, z, feet = gadget.aim(p)
  if not x then return nil end
  for _, sf in ipairs(G.safes) do
    local g = door.get(sf.idx)
    if g and g.keyflags ~= 0 and g.open < 0.3 and math.abs(g.y - feet) < 260 and d2(x, z, g.x, g.z) <= T.safe_reach
       and not bond.has_key(p, g.keyflags) then return sf, g end
  end
  return nil
end
G.safe_target = safe_target

local function safe_window() return 0.15 + 0.7 * math.random() end

local function safe_open(p, sf)
  door.set_keyflags(sf.idx, 0)
  door.open(sf.idx)
  G.stats.safes = (G.stats.safes or 0) + 1
  log("Bond %d cracked safe %s (door %d)", p, sf.name, sf.idx)
  flash_p(p, "The safe swings open", 4)
  local list = sf.loot and LEVEL.loot and LEVEL.loot[sf.loot]
  if list then   -- (a safe the heist does not fill itself: its loot pads get gold and cash)
    local n = 0
    for i, pd in ipairs(list) do
      if spawn.item(pd, i % 2 == 1 and "goldbar" or "money", { offset = { 0, 0 } }) then n = n + 1 end
    end
    log("safe %s: %d pieces of loot", sf.name, n)
  end
end

local function crack_tick(p, me, sf, presses)
  local now, edge = gadget.now(p)
  local c = me.crack
  if not c or c.safe ~= sf.idx then
    if presses == 0 then
      gadget.dial(p, nil)
      hint(p, "LOCKPICKS: a safe. FIRE to listen at the dial")
      return
    end
    c = { safe = sf.idx, tumbler = 1, start = now, center = safe_window() }
    me.crack = c
    presses = 0
    log("Bond %d listens at safe %s", p, sf.name)
  end
  if presses > 0 then
    local ph = ((edge - c.start) % T.safe_period) / T.safe_period
    if math.abs(ph - c.center) <= T.safe_window / 2 then
      c.tumbler = c.tumbler + 1
      log("Bond %d: tumbler %d clicks (%.2f in %.2f)", p, c.tumbler - 1, ph, c.center)
      if c.tumbler > T.safe_tumblers then
        me.crack = nil
        gadget.dial(p, nil)
        safe_open(p, sf)
        return
      end
      c.center, c.start = safe_window(), now
      flash_p(p, string.format("Click. Tumbler %d of %d", c.tumbler - 1, T.safe_tumblers), 1.5)
    else
      c.misses = (c.misses or 0) + 1
      log("Bond %d: missed the tumbler (%.2f, window %.2f)", p, ph, c.center)
      make_noise(sf.x, sf.y, sf.z, T.safe_noise, "door", p, 1)
      flash_p(p, "Clunk. The tumbler slipped", 1.5)
    end
  end
  gadget.dial(p, c.start, T.safe_period, c.center, T.safe_window)
  seen_act(p, "at a safe")
  hint(p, string.format("SAFE: tumbler %d of %d - FIRE when the needle is in the green", c.tumbler, T.safe_tumblers))
end

local function lockpick_tick(p, me, presses, held, dt)
  local sf = safe_target(p)
  if sf then gadget.bar(p, nil); crack_tick(p, me, sf, presses); return end
  if me.crack then me.crack = nil; gadget.dial(p, nil) end
  local d = pick_target(p)
  if not d then
    me.pick = nil
    gadget.bar(p, nil)
    hint(p, "LOCKPICKS: hold FIRE at a locked door")
    return
  end
  local g = door.get(d)
  local need, kname = pick_need(g.keyflags)
  local st = G.picked[d] or { prog = 0, need = need }
  if presses > 0 or held then
    if me.pick ~= d then
      me.pick = d
      presses = presses - 1          -- the press that starts the work is quiet
      log("Bond %d starts picking door %d (%s, %d s; %.1f done)", p, d, kname, need, st.prog)
    end
    if held then st.prog = st.prog + dt end
    if presses > 0 then             -- rushed: quicker, but it rattles
      st.prog = st.prog + presses * T.pick_hurry
      make_noise(g.x, g.y, g.z, T.pick_noise, "door", p, 1)
      log("Bond %d rushes the lock (%d taps): it rattles", p, presses)
    end
    G.picked[d] = st
    seen_act(p, "picking a lock")
    if st.prog >= st.need then pick_done(p, d); me.pick = nil; gadget.bar(p, nil); return end
  end
  gadget.bar(p, st.prog / st.need, 0xE0E0E0FF)
  if me.pick == d and held then
    hint(p, string.format("PICKING THE %s LOCK  %d%%   (tap: faster, noisy)", kname, math.floor(st.prog / st.need * 100)))
  else
    hint(p, string.format("LOCKPICKS: hold FIRE: %s lock, %d s%s", kname, need, st.prog > 0 and string.format(" (%d%% done)", math.floor(st.prog / st.need * 100)) or ""))
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- coins and the sleep gas: thrown

local function throw(p, range)
  local yaw, pitch = gadget.aim(p)
  if not yaw then return nil end
  local r = range * math.max(0.25, math.min(1, 0.6 + (pitch or 0) / 60))   -- look up to throw further
  return gadget.floor_point(p, yaw, r)
end

local function coin_throw(p, me)
  if me.coins <= 0 then flash_p(p, "Out of coins", 2); return end
  local x, y, z = throw(p, T.coin_range)
  if not x then return end
  me.coins = me.coins - 1
  G.coins[#G.coins + 1] = { x = x, y = y, z = z, at = H.elapsed + 0.75, p = p }
  G.stats.coins = G.stats.coins + 1
  log("Bond %d throws a coin to (%.0f %.0f %.0f), %d left", p, x, y, z, me.coins)
  flash_p(p, string.format("Coin thrown (%d left)", me.coins), 2)
end

local function gas_throw(p, me)
  if me.gas <= 0 then flash_p(p, "No sleep gas left", 2); return end
  local x, y, z = throw(p, T.gas_range)
  if not x then return end
  me.gas = me.gas - 1
  G.gas[#G.gas + 1] = { x = x, y = y, z = z, at = H.elapsed + T.gas_fuse, p = p }
  log("Bond %d throws the sleep gas to (%.0f %.0f %.0f)", p, x, y, z)
  flash_p(p, string.format("Sleep gas: %d s...", T.gas_fuse), 3)
end

local function thrown_tick()
  local keep = {}
  for _, c in ipairs(G.coins) do
    if H.elapsed >= c.at then
      local n = make_noise(c.x, c.y, c.z, T.coin_noise, "coin", c.p, T.coin_guards)
      if n > 0 then flash_p(c.p, string.format("Clink. %d guard%s coming to look", n, n == 1 and " is" or "s are"), 3)
      elseif n == 0 then flash_p(c.p, "Clink. Nobody heard it", 2) end
    else
      keep[#keep + 1] = c
    end
  end
  G.coins = keep
  keep = {}
  for _, g in ipairs(G.gas) do
    if H.elapsed >= g.at then
      local n = 0
      local lists = { H.guards, H.civs }
      for _, list in ipairs(lists) do
        for _, e in ipairs(list) do
          local c = e.chr
          if spawn.alive(c) and not spawn.down(c) then
            local cx, cz = spawn.where(c)
            local _, cy = gadget.chr_yaw(c)
            if cx and cy and math.abs(cy - g.y) < 200 and d2(cx, cz, g.x, g.z) <= T.gas_radius and spawn.ko(c, g.p) then n = n + 1 end
          end
        end
      end
      G.stats.gassed = G.stats.gassed + n
      log("sleep gas at (%.0f %.0f): %d knocked out", g.x, g.z, n)
      flash_p(g.p, string.format("Pssst... %d out cold", n), 3)
    else
      keep[#keep + 1] = g
    end
  end
  G.gas = keep
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the tape loop

local function loop_target(p)
  local yaw, pitch, x, y, z = gadget.aim(p)
  if not yaw then return nil end
  local fx, fy, fz = -math.sin(math.rad(yaw)) * math.cos(math.rad(pitch)), math.sin(math.rad(pitch)), math.cos(math.rad(yaw)) * math.cos(math.rad(pitch))
  local best, ba
  for _, cam in ipairs(cctv.list()) do
    local c = cctv.get(cam)
    if c and not c.destroyed then
      local dx, dy, dz = c.x - x, c.y - y, c.z - z
      local d = math.sqrt(dx * dx + dy * dy + dz * dz)
      if d > 1 and d <= T.loop_range then
        local cosa = (dx * fx + dy * fy + dz * fz) / d
        -- (degrees off the aim from the sine of the cross product: math.acos is not usable in this build)
        local cx_, cy_, cz_ = dy * fz - dz * fy, dz * fx - dx * fz, dx * fy - dy * fx
        local a = deg(math.atan(math.sqrt(cx_ * cx_ + cy_ * cy_ + cz_ * cz_) / d, cosa))
        if G.debug then log("loop aim: camera %d at %.0f, %.1f deg off", cam, d, a) end
        if a <= T.loop_cone and (not ba or a < ba) then best, ba = cam, a end
      end
    end
  end
  return best
end
G.loop_target = loop_target

local function loop_plant(p, me, cam)
  cam = cam or loop_target(p)
  if not cam then flash_p(p, "TAPE LOOP: aim at a camera", 2); return false end
  if me.loops <= 0 then flash_p(p, "No tape loops left", 2); return false end
  me.loops = me.loops - 1
  local prev = G.loops[cam]
  G.loops[cam] = { until_t = H.elapsed + T.loop_time, name = prev and prev.name or cctv.name(cam) }
  cctv.set_alarm_time(1000000, cam)
  cctv.name(cam, G.loops[cam].name .. " [LOOP]")
  G.stats.loops = G.stats.loops + 1
  log("Bond %d loops camera %d (%s) for %d s", p, cam, G.loops[cam].name, T.loop_time)
  flash_p(p, string.format("%s is on a loop for %d s", G.loops[cam].name, T.loop_time), 4)
  return true
end
G.loop_plant = loop_plant

local function loops_tick()
  local cams = {}
  for cam, _ in pairs(G.loops) do cams[#cams + 1] = cam end
  table.sort(cams)
  for _, cam in ipairs(cams) do
    local l = G.loops[cam]
    if H.elapsed >= l.until_t then
      cctv.set_alarm_time(0, cam)
      cctv.name(cam, l.name)
      G.loops[cam] = nil
      log("camera %d: the loop ran out", cam)
    end
  end
end

-- ------------------------------------------------------------------------------------------------------------------------------------
-- use: the pickpocket, the disguise, the vents

local function body_near(p, reach, want_down)
  local _, _, x, _, z, feet = gadget.aim(p)
  if not x then return nil end
  local best, bd, kind
  for _, pair in ipairs({ { H.guards, "guard" }, { H.civs, "staff" } }) do
    for _, e in ipairs(pair[1]) do
      local c = e.chr
      if spawn.alive(c) and (spawn.down(c) == want_down) then
        local cx, cz = spawn.where(c)
        if cx then
          local d = d2(cx, cz, x, z)
          if d <= reach and (not bd or d < bd) then best, bd, kind = c, d, pair[2] end
        end
      end
    end
  end
  return best, kind, bd
end

local function pickpocket(p)
  if not T.pickpocket then return false end
  local c, kind = body_near(p, T.pick_pocket_reach, false)
  if not c then return false end
  local _, _, x, _, z = gadget.aim(p)
  local cx, cz = spawn.where(c)
  local gyaw = gadget.chr_yaw(c)
  local to_bond = deg(math.atan(x - cx, z - cz))
  local off = math.abs(wrap(to_bond - (gyaw or 0)))
  local behind = off >= T.behind
  local aware = H.hot or cover.alert()
  local st = cover.state(c)
  if st and (((st.witness or 0) >> p) & 1) == 1 then aware = true end
  if cover.suspicion(c, p) and cover.suspicion(c, p) >= 0.5 then aware = true end
  local ai = rawget(_G, "ai")
  if type(ai) == "table" and ai.awareness then
    local s = ai.awareness(c)
    if s and s >= 2 then aware = true end
  end
  if behind and not aware then
    local flags = gadget.lift_key(p, c)
    if flags > 0 then
      local nm = {}
      for _, k in ipairs({ GREEN, BLUE, RED, GOLD, FLOORKEY }) do if (flags & k) ~= 0 then nm[#nm + 1] = KEYNAME[k] end end
      G.stats.lifts = G.stats.lifts + 1
      log("Bond %d picked chr %d's pocket: %s (0x%x), %.0f deg off his facing", p, c, table.concat(nm, "+"), flags, off)
      flash_p(p, "Lifted: the " .. table.concat(nm, " and ") .. " card", 4)
      return true
    end
    log("Bond %d went through chr %d's pockets: nothing", p, c)
    flash_p(p, "Nothing in his pockets", 2)
    return true
  end
  -- caught
  log("Bond %d: pickpocket on chr %d caught (%s, %.0f deg off his facing)", p, c, aware and "on to him" or "facing him", off)
  if kind == "guard" and S.cover and not cover.alert() and cover.state(c) then
    cover.blow(c, p)
    G.stats.blown = G.stats.blown + 1
    flash_p(p, "He caught your hand! Drop him before he radios", 4)
  else
    flash_p(p, "Caught! They saw your hand", 3)
  end
  return true
end
G.pickpocket = pickpocket

local function task_start(p, me, kind, need, extra)
  local _, _, x, _, z = gadget.aim(p)
  me.task = { kind = kind, t = 0, need = need, x = x, z = z }
  for k, v in pairs(extra or {}) do me.task[k] = v end
end

local function disguise_try(p, me)
  if not T.disguise then return false end
  local c, kind = body_near(p, T.disguise_reach, true)
  if not c or not ko.is(c) or G.used_bodies[c] then return false end
  if not G.searched[c] then return false end   -- (the first press searches it: the stock search)
  task_start(p, me, "disguise", T.disguise_time, { chr = c, as = kind })
  log("Bond %d takes chr %d's uniform (%s)", p, c, kind)
  flash_p(p, "Changing clothes: stay with him, stay unseen", 2)
  return true
end

local function vent_try(p, me)
  if not T.vents then return false end
  local _, _, x, _, z, feet = gadget.aim(p)
  for _, v in ipairs(G.vents) do
    for _, ab in ipairs({ { v.pa, v.a, v.pb, v.b }, { v.pb, v.b, v.pa, v.a } }) do
      local here = ab[1]
      if d2(x, z, here[1], here[3]) <= T.vent_reach and math.abs(feet - here[2]) < 150 and v.id == "vault" and not (H.power and H.power.out) then
        -- (the controller's ruling: the emergency vault ventilator can be crawled only while the power is out and its fans stop)
        flash_p(p, "The vault ventilator fans are running - cut the power first", 3)
        log("Bond %d: the vault vent refused (the power is on)", p)
        return true
      end
      if d2(x, z, here[1], here[3]) <= T.vent_reach and math.abs(feet - here[2]) < 150 then
        if gadget.reel(p, { { x, feet, z }, { x, feet, z } }, T.vent_time, ab[2], ab[4]) then
          me.reel = { id = "vent_" .. v.id, up = false, until_t = H.elapsed + T.vent_time, to = ab[4], vent = true }
          G.stats.vents = G.stats.vents + 1
          log("Bond %d crawls into vent %s (pad %d -> %d)", p, v.id, ab[2], ab[4])
          flash_p(p, "Crawling through the vent...", T.vent_time)
          if type(rawget(_G, "sound")) == "table" and sound.play_sfx then sound.play_sfx("DOOR_METAL_OPEN", 0.6, 0.8) end
          return true
        end
      end
    end
  end
  return false
end

local function task_tick(p, me, dt)
  local t = me.task
  if not t then return end
  local _, _, x, _, z = gadget.aim(p)
  if not x or bond.dead(p) or d2(x, z, t.x, t.z) > 60 then
    me.task = nil; gadget.bar(p, nil)
    flash_p(p, "Interrupted: you moved", 2)
    return
  end
  if S.cover and cover.seen(p) and not cover.alert() then
    me.task = nil; gadget.bar(p, nil)
    seen_act(p, "undressing a body")
    flash_p(p, "Seen! Somebody saw you with the body", 3)
    return
  end
  t.t = t.t + dt
  gadget.bar(p, t.t / t.need, 0x80C0FFFF)
  hint(p, string.format("CHANGING INTO HIS UNIFORM  %d%%", math.floor(t.t / t.need * 100)))
  if t.t >= t.need then
    me.task = nil; gadget.bar(p, nil)
    if t.kind == "disguise" then
      G.used_bodies[t.chr] = true
      gadget.disguise(p, t.as == "guard" and 2 or 1)
      G.stats.disguises = G.stats.disguises + 1
      log("Bond %d now wears a %s's uniform", p, t.as)
      flash_p(p, t.as == "guard" and "GUARD UNIFORM: restricted areas are yours. Don't linger near guards" or
              "STAFF CLOTHES: the staff rooms are yours", 6)
    end
  end
end

on("use", function(p, kind, idx, objtype)
  if H.phase == "off" or H.ended or not H.ready then return end
  local me = G.players[p]
  if not me or me.task or me.reel then return end
  if vent_try(p, me) then return end
  if disguise_try(p, me) then return end
  if idx < 0 then pickpocket(p) end
end)

on("chr_searched", function(c, p) G.searched[c] = true; log("chr %d searched by Bond %d", c, p) end)

-- ------------------------------------------------------------------------------------------------------------------------------------
-- the tick

local function player_tick(p, dt)
  local me = G.players[p]
  if bond.dead(p) then gadget.marker(p, nil); gadget.bar(p, nil); return end
  if reel_tick(p, me) then gadget.marker(p, nil); gadget.bar(p, nil); return end
  task_tick(p, me, dt)
  local item = bond.weapon(p)
  local presses, held = gadget.fire(p)
  if item ~= ITEM.lockpick and me.pick then me.pick = nil end
  if item ~= ITEM.lockpick and me.crack then me.crack = nil; gadget.dial(p, nil) end
  if item ~= ITEM.lockpick and not me.task then gadget.bar(p, nil) end
  gadget.marker(p, nil)
  if item == ITEM.piton then
    local t = grapple_target(p)
    if t then
      gadget.marker(p, t.anchor[1], t.anchor[2] + 40, t.anchor[3])
      hint(p, t.up and ("PITON GUN: FIRE - up to " .. t.pair.label) or "PITON GUN: FIRE - abseil down")
      if presses > 0 then grapple_go(p, t) end
    else
      hint(p, "PITON GUN: no grapple point here")
    end
  elseif item == ITEM.lockpick then
    lockpick_tick(p, me, presses, held, dt)
  elseif item == ITEM.coin then
    hint(p, string.format("COINS x%d: FIRE to throw (look up: further)", me.coins))
    if presses > 0 then coin_throw(p, me) end
  elseif item == ITEM.loop then
    local cam = loop_target(p)
    hint(p, string.format("TAPE LOOP x%d: %s", me.loops, cam and ("FIRE: loop " .. cctv.name(cam)) or "aim at a camera"))
    if presses > 0 then loop_plant(p, me, cam) end
  elseif item == ITEM.gas then
    hint(p, string.format("SLEEP GAS x%d: FIRE to throw", me.gas))
    if presses > 0 then gas_throw(p, me) end
  elseif item == ITEM.thermal then
    if presses > 0 and H.elapsed >= (me.xray_ready or 0) then
      me.xray_until = H.elapsed + T.thermal_time
      me.xray_ready = me.xray_until + T.thermal_recharge
      G.stats.thermal = (G.stats.thermal or 0) + 1
      log("Bond %d: thermal imager on for %d s", p, T.thermal_time)
    end
    if H.elapsed < (me.xray_until or 0) then hint(p, string.format("THERMAL ON  %d s", math.ceil(me.xray_until - H.elapsed)))
    elseif H.elapsed < (me.xray_ready or 0) then hint(p, string.format("THERMAL: recharging %d s", math.ceil(me.xray_ready - H.elapsed)))
    else hint(p, string.format("THERMAL IMAGER: FIRE to see through walls (%d s)", T.thermal_time)) end
  end
  gadget.xray(p, H.elapsed < (me.xray_until or 0))
end

on("stage_start", function(stage)
  if H.phase == "off" then return end
  load_pairs()
  G.never, G.safes = {}, {}
  local onames = {}
  for n, _ in pairs(LEVEL.objects or {}) do onames[#onames + 1] = n end
  table.sort(onames)
  for _, n in ipairs(onames) do
    local o = LEVEL.objects[n]
    if o.type == "safe" then
      local g = door.get(o.index)
      G.safes[#G.safes + 1] = { idx = o.index, name = n, x = g and g.x, y = g and g.y, z = g and g.z,
                                loot = (n == "director_safe") and "director_loot" or nil }
    end
  end
  for _, o in pairs(LEVEL.objects or {}) do
    if (o.type == "door" or o.type == "safe") and (o.guard_access == "never" or o.type == "safe") then
      G.never[o.index] = true
      if o.pair then G.never[o.pair] = true end
    end
  end
  local items = {}
  for _, k in ipairs({ "piton", "lockpick", "coin", "loop", "gas", "thermal" }) do items[#items + 1] = ITEM[k] end
  gadget.items(items)
  for p = 0, bond.count() - 1 do
    G.players[p] = { coins = T.coins or 0, loops = T.loops or 0, gas = T.gas or 0 }
    if T.piton then bond.give(p, GIVE.piton) end
    if T.lockpick then bond.give(p, GIVE.lockpick) end
    if (T.coins or 0) > 0 then bond.give(p, GIVE.coin) end
    if (T.loops or 0) > 0 then bond.give(p, GIVE.loop) end
    if (T.gas or 0) > 0 then bond.give(p, GIVE.gas) end
    if T.thermal then bond.give(p, GIVE.thermal) end
    gadget.disguise(p, 0)
  end
  log("loadout: piton %s, lockpicks %s, coins %d, loops %d, gas %d, pickpocket %s, disguise %s, vents %s", tostring(T.piton), tostring(T.lockpick),
      T.coins or 0, T.loops or 0, T.gas or 0, tostring(T.pickpocket), tostring(T.disguise), tostring(T.vents))
end)

function G.state()
  local t = {}
  for p = 0, bond.count() - 1 do
    local me = G.players[p]
    if me then
      t[#t + 1] = string.format("%d:%d/%d/%d/%s/%s/%s/%d/%s/%s", p, me.coins, me.loops, me.gas, me.pick and tostring(me.pick) or "-",
        me.task and string.format("%s%.2f", me.task.kind, me.task.t) or "-", me.reel and me.reel.id or "-", gadget.disguise(p),
        tostring(me.xray_until), me.crack and string.format("%d.%d.%d.%.3f", me.crack.safe, me.crack.tumbler, me.crack.start, me.crack.center) or "-")
    end
  end
  local pk = {}
  local ds = {}
  for d, _ in pairs(G.picked) do ds[#ds + 1] = d end
  table.sort(ds)
  for _, d in ipairs(ds) do pk[#pk + 1] = string.format("%d=%.2f", d, G.picked[d].prog) end
  local lp = {}
  local cs = {}
  for c, _ in pairs(G.loops) do cs[#cs + 1] = c end
  table.sort(cs)
  for _, c in ipairs(cs) do lp[#lp + 1] = string.format("%d@%.1f", c, G.loops[c].until_t) end
  return string.format(" G[%s] pk[%s] lp[%s] c%d g%d pl%d s%d/%d/%d/%d/%d/%d/%d/%d/%d", table.concat(t, ","), table.concat(pk, ","), table.concat(lp, ","),
    #G.coins, #G.gas, #G.pulled, G.stats.grapples, G.stats.picks, G.stats.lifts, G.stats.coins, G.stats.loops, G.stats.disguises,
    G.stats.vents, G.stats.gassed, G.stats.noises)
end

on("tick", function(dt)
  if H.phase == "off" then return end
  mode.state(H.state_text() .. G.state())   -- (after the heist's own: the gadgets' state rides in the same hashed text)
  if H.ended or not H.ready then return end
  for p = 0, bond.count() - 1 do if G.players[p] then player_tick(p, dt) end end
  thrown_tick()
  loops_tick()
  pulled_tick()
end)
