-- Smoke of the sniper duel on Surface (tests/sniper/run.sh): the real ladder, Bond invulnerable, fast-forward. The test "kills" a marksman with a
-- real shot (test.shoot, the stock hit path) once he has taken his shot: in round 1 only after he has fired and moved once, so the log shows the
-- whole glint -> shot -> relocate -> glint cycle; in the later rounds as soon as he is aiming. The run ends in the debrief on the hub's stage.
-- Env: GE_SNIPER_SHOTS=1 takes pictures (a glint, the round callout).
if mode.env_flag("GE_SNIPER_SHOTS") then SNIPER_SETTINGS = { band_hi = 8500 } end   -- (the pictures: the nearest perches, so a figure shows at all)
if mode.env_flag("GE_SNIPER_LOSE") then   -- the loss path quickly: a marksman who nearly always hits and moves fast (round 1 five times over: "ROUND 1/5" still)
  local hard = { foe = 1, snipers = 1, rushers = 0, hit = 0.9, glint = 1.0, hide = { 2, 3 } }
  SNIPER_SETTINGS = { rounds_table = { hard, hard, hard, hard, hard }, first_delay = 1 }
end
include("sniper")
local ST = SNIPER
local t = 0
local seen = {}
local SHOTS = mode.env_flag("GE_SNIPER_SHOTS")
local LOSE = mode.env_flag("GE_SNIPER_LOSE")   -- nobody shoots back: Bond sits (no invuln cheat) until the marksmen kill him: the loss path
local rush_t = {}
local rlog = 0
local shot_at = {}
local function snap(tag, at) shot_at[#shot_at + 1] = { tag = tag, at = at } end

on("tick", function(dt)
  t = t + dt
  for i = #shot_at, 1, -1 do
    if t >= shot_at[i].at then
      if bench and bench.shot then bench.shot() end
      print("SMOKE shot " .. shot_at[i].tag)
      table.remove(shot_at, i)
    end
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
      snap("glint", t + 0.5)
    end
    if f.st == "fire" and f.chr and spawn.alive(f.chr) then
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
