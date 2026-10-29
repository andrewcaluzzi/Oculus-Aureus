-- Smoke of the sniper duel on Surface (tests/sniper/run.sh): the real ladder, Bond invulnerable, fast-forward. The test "kills" a marksman with a
-- real shot (test.shoot, the stock hit path) once he has taken his shot: in round 1 only after he has fired and moved once, so the log shows the
-- whole glint -> shot -> relocate -> glint cycle; in the later rounds as soon as he is aiming. The run ends in the debrief on the hub's stage.
-- Env: GE_SNIPER_SHOTS=1 takes pictures (a glint, the round callout).
SNIPER_SETTINGS = {}
if mode.env_flag("GE_SNIPER_RIDGE") then SNIPER_SETTINGS.stage = "ridge" end   -- (GE_STAGE=levelgen GE_LEVELGEN=ridge, the ridge build)
if mode.env_flag("GE_SNIPER_LOSE") then   -- the loss path quickly: a marksman who nearly always hits and moves fast (round 1 five times over: "ROUND 1/5" still)
  local hard = { foe = 1, snipers = 1, rushers = 0, hit = 0.9, glint = 1.0, dwell = { 6, 8 }, volley_gap = { 2, 3 }, volley_shots = { 1, 1 } }
  SNIPER_SETTINGS.rounds_table, SNIPER_SETTINGS.first_delay = { hard, hard, hard, hard, hard }, 1
end
include("sniper")
local ST = SNIPER
local t = 0
local seen = {}
local SHOTS = mode.env_flag("GE_SNIPER_SHOTS")
local LOSE = mode.env_flag("GE_SNIPER_LOSE")   -- nobody shoots back: Bond sits (no invuln cheat) until the marksmen kill him: the loss path
local rush_t = {}
local rlog = 0
local blog

local shot_at = {}
local function snap(tag, at) shot_at[#shot_at + 1] = { tag = tag, at = at } end

local losdone = false
on("stage_start", function() print(string.format("SMOKE fog: props are drawn out to %.0f units", probe.fog())) end)
on("tick", function(dt)
  t = t + dt
  if t <= 12.1 and math.floor(t) ~= (blog or -1) then   -- Bond's position each second of the first 12 s: no warps after the stage starts
    blog = math.floor(t)
    local x, y, z = bond.pos(0)
    print(string.format("SMOKE bondpos t=%d (%.0f, %.0f, %.0f)", blog, x or -1, y or -1, z or -1))
  end
  for i = #shot_at, 1, -1 do
    if t >= shot_at[i].at then
      if bench and bench.shot then bench.shot() end
      print("SMOKE shot " .. shot_at[i].tag)
      if shot_at[i].tag == "perch" then local d = test.rooms_drawn and test.rooms_drawn(0) or {}; print("SMOKE perch rooms drawn now: " .. table.concat(d, ",")) end
      table.remove(shot_at, i)
    end
  end
  if ST.phase == "round" and not losdone then   -- does the AI's line (stan) agree with the drawn geometry (probe.los) from Bond's perch? one line a perch, once
    losdone = true
    local _, _, ex, ey, ez = gadget.aim(0)
    local both_ok, both_blocked, glitch, other = 0, 0, 0, 0
    for _, p in ipairs(ST.perches) do
      local g = true
      for _, h in ipairs({ 165, 140 }) do g = g and probe.los(ex, ey, ez, p.x, p.y + h, p.z) and probe.los(p.x, p.y + h, p.z, ex, ey, ez) end
      if p.los and g then both_ok = both_ok + 1 elseif not p.los and not g then both_blocked = both_blocked + 1 elseif p.los then glitch = glitch + 1 else other = other + 1 end
      print(string.format("SMOKE LOS pad %d (%.0f m): stan %s, drawn geometry %s", p.pad, p.d / 100, tostring(p.los), tostring(g)))
    end
    print(string.format("SMOKE LOSAGREE: %d see Bond by both lines, %d blocked by both (cover), stan sees but geometry blocks %d (the glitch), geometry clear but stan blocks %d", both_ok, both_blocked, glitch, other))
  end
  if ST.phase ~= "round" then return end
  if #ST.rushers > 0 and t - rlog >= 2 then   -- how the rushers close in (the nearest one's distance from Bond, every 2 s)
    rlog = t
    local bx, _, bz = bond.pos(0)
    local near, n = 1e9, 0
    for _, c in ipairs(ST.rushers) do
      local x, z = spawn.where(c)
      if x and bx then n = n + 1; near = math.min(near, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100) end
    end
    print(string.format("SMOKE rushers: %d alive, the nearest %.0f m from Bond", n, near))
  end
  if ST.rush_done and not rush_t[ST.round] then rush_t[ST.round] = t end   -- (a round with rushers: the kills wait till they have been in for 9 s)
  for _, f in ipairs(ST.foes) do
    if f.st == "glint" and SHOTS and not f.snapped and ST.round == 1 and f.perch then
      f.snapped = true
      -- Bond looks at the perch; the picture a fraction of a second later (the glint lasts a second or more)
      local px, pz = f.perch.x, f.perch.z
      local bx, _, bz = bond.pos(0)
      if bench and bench.look then pcall(bench.look, 0, math.deg(math.atan(-(px - bx), (pz - bz))), 1) end
      snap("glint", t + 1.0)
    end
    if f.st == "scan" and not f.scan_t then f.scan_t = t end
    if f.st == "scan" and SHOTS and not f.snapped2 and ST.round == 1 and f.perch and t - (f.scan_t or t) >= 3 then   -- the marksman on his perch, Bond looking at him
      f.snapped2 = true
      local px, pz = f.perch.x, f.perch.z
      local bx, _, bz = bond.pos(0)
      local _, _, _, ey = gadget.aim(0)
      local pitch = -math.deg(math.atan(((ey or 0) - (f.perch.y + 20)) / math.sqrt((px - bx) ^ 2 + (pz - bz) ^ 2)))
      if bench and bench.look then pcall(bench.look, 0, math.deg(math.atan(-(px - bx), (pz - bz))), pitch) end
      local drawn = test.rooms_drawn and test.rooms_drawn(0) or {}
      print(string.format("SMOKE perch: foe at (%.0f, %.0f) %.0f m, pitch %.1f, his room %s, rooms drawn: %s", px, pz, math.sqrt((px - bx) ^ 2 + (pz - bz) ^ 2) / 100, pitch, tostring(spawn.room(f.chr)), table.concat(drawn, ",")))
      snap("perch", t + 0.8)
    end
    if f.st ~= "wait" and f.st ~= "run" and f.chr and spawn.alive(f.chr) then
      f.test_seen = f.test_seen or t
      local ready = not LOSE and (ST.round > 1 or ST.relocs >= 1) and t - f.test_seen >= 0.4 and rush_t[ST.round] and t - rush_t[ST.round] >= 9
      if ready then
        local part = ({ "head", "body", "body" })[math.random(3)]
        for _ = 1, 40 do if spawn.alive(f.chr) then test.shoot(f.chr, part, "sniperrifle", 0) end end
        if spawn.alive(f.chr) then spawn.kill(f.chr) end
        print(string.format("SMOKE killed foe %d of round %d (%s)", f.n, ST.round, part))
      end
    else
      f.test_seen = nil
    end
  end
end)

on("chr_spawned", function(c)
  if ST.phase ~= "round" then return end   -- (not the sight sweep's stand-ins)
  local x, z = spawn.where(c)
  local bx, _, bz = bond.pos(0)
  if x and bx and not seen[c] then
    seen[c] = true
    print(string.format("SMOKE spawn chr %d at %.0f m from Bond", c, math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) / 100))
  end
end)
