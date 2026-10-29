-- Arena plus screenshots (run.sh with TEST=plus-shots): the credits panel with two timer chips, a power-up on the sand, the Q-Branch
-- crate prompt, and the Requisition cards. bench.shot() writes a BMP of the next presented frame into the working directory.
ARENA_SETTINGS = { mode = "fixed", waves = 3, boss_every = 0, breather = 30, start_delay = 1, first_wave = 1, ammo_every = 0, armour_chance = 0,
  max_alive = 24, max_alive_cap = 24, test_keys = true, corpse_keep = 4,
  crowd = { count = 60, favour = true, drift = 0, roar_every = 9999, sponsor_cooldown = 9999, thumb = false, points = { camp_secs = 9999 } },
  plus = { pu_chance = 0, pu_chance_boss = 0 },
  debug_queue = function(w)
    local q = {}
    for i = 1, 7 do
      q[#q + 1] = { n = i, gate = 1 + (i % 4), spec = { skin = "russian_soldier", weapon = "pp7", behaviour = "hold", health = 0.05, accuracy = 0, _tier = "normal" } }
    end
    return q
  end }
include("arena")

local PL = ARENA_PLUS and ARENA_PLUS.live
local t, si, st = 0, 0, { t = 0 }
local function live() return ARENA_HOOKS.live() end
local function near(c, dx)
  local x, z = spawn.where(c)
  local best, bd = nil, 1e18
  for i = 0, pads.count() - 1 do
    local px, py, pz = pads.pos(i)
    if px and py <= 1 then
      local d = (px - x) ^ 2 + (pz - z) ^ 2
      if d > 300 * 300 and d < bd then best, bd = i, d end
    end
  end
  bond.warp(0, best, dx or 20, 20)
end
local function kill(c) for _ = 1, 80 do if not spawn.alive(c) then break end test.shoot(c, "body", "pp7", 0) end end
local function collect(kind)
  local bx, _, bz = bond.pos(0)
  PL.drop(kind, bx, bz)
  local f = PL.field()
  if f then bond.warp(0, f.pad, f.dx, f.dz) end
end
local function shot(name) print("PLUSSHOT " .. name); pcall(bench.shot) end

local steps = {
  function() return #live() >= 7 end,
  function() for k, c in ipairs(live()) do if k <= 3 then near(c); kill(c) end end ARENA_TEST_KEY("credits"); return true end,
  function() collect("qdamage"); return true end,
  function() collect("double"); return true end,
  function()   -- shot 1: the panel with the credits row and two chips, looking at a live enemy
    local c = live()[1]
    near(c, 900)
    local x, z = spawn.where(c)
    test.face(0, x, z)
    return true
  end,
  function() shot("hud"); return true end,
  function()   -- shot 2: a nuke on the sand ahead
    local bx, _, bz = bond.pos(0)
    PL.drop("nuke", bx + 700, bz)
    local f = PL.field()
    if f then bond.warp(0, f.pad, f.dx - 650, f.dz); test.face(0, f.x, f.z) end
    return true
  end,
  function() shot("powerup"); return true end,
  function()   -- shot 3: Q and the crate prompt
    local q = PL.crate()
    PL.drop("maxammo", q.x, q.z)   -- (replaces the nuke on the field)
    bond.warp(0, q.pad, 260, 120)
    test.face(0, q.x, q.z)
    return true
  end,
  function() shot("crate"); return true end,
  function() for _, c in ipairs(live()) do near(c); kill(c) end return true end,   -- shot 4: the cards
  function() return PL.held() end,
  function() return st.t > 2.0 end,
  function() shot("cards"); return true end,
  function() return st.t > 1.0 end,
  function() print("PLUSSHOT DONE"); if bench and bench.exit then pcall(bench.exit) end return true end,
}
-- (a step returns true when done; a step that waits on time uses st.t, reset on each step)
on("tick", function(dt)
  t = t + dt
  if t < 3 then return end
  if si == 0 then si = 1 end
  local s = steps[si]
  if not s then return end
  st.t = st.t + dt
  local ok, done = pcall(s)
  if not ok then print("PLUSSHOT error in step " .. si .. ": " .. tostring(done)); done = true end
  if done then si = si + 1; st.t = 0 end
end)
