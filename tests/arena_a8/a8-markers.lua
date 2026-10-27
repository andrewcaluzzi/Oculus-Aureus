-- A8 markers v2: a messy knife wave for before/after screenshots. The Knife Sisters (wave 2, 8 of them, knives thrown from range) at an
-- invulnerable Bond standing in the pit looking at gate 2, each sister killed 9 s after she spawns (her PP7 drops), centre ammo every 6 s,
-- an armour pickup and a crowd gift placed near Bond. A8_MARKER_STYLE = "a7" (the old beacons over everything) or "v2" (default).
-- Run with GE_SHOT=<frame> (one BMP) and GE_MARKERLOG=1 (published / drawn counts once a second).
ARENA_SETTINGS = { mode = "endless", boss_every = 0, breather = 30, start_delay = 1, first_wave = 2, ammo_every = 6, armour_chance = 0,
  debug_kill = 9, max_alive = 8, max_alive_cap = 8,
  marker_style = A8_MARKER_STYLE or "v2",
  crowd = { count = 30, favour = true, roar_every = 9999, sponsor_cooldown = 9999 } }
include("arena")

local t, placed = 0, false
on("tick", function(dt)
  t = t + dt
  if not placed and t > 2 then
    placed = true
    local function pad_at(px, pz)
      local best, bd = nil, 1e18
      for i = 0, pads.count() - 1 do
        local x, y, z = pads.pos(i)
        if x and y <= 1 then local d = (x - px) ^ 2 + (z - pz) ^ 2; if d < bd then best, bd = i, d end end
      end
      return best
    end
    bond.warp(0, pad_at(0, -1500), 20, 20)
    local h = spawn.item(pad_at(-1000, 500), "armour", { amount = 1.0 })
    print("ge: arena: A8TEST markers: armour placed", h, "style", ARENA_SETTINGS.marker_style)
    ARENA_CROWD.live.throw(0)
  end
end)
