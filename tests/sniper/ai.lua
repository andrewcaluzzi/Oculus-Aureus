-- What does GE AI do with a sniper rifle on Surface? (tests/sniper/ai.sh; throwaway measurement, the numbers go to the report and facts.md)
--   1. SIGHT: which of the stage's waypoint pads can a "snipe" marksman fire from at a still Bond at his start? A marksman is put on each pad
--      in turn (8 at a time, 4 s), combat.los asks for the stan line, test.fire_stats counts what he fired.
--   2. For a ladder of distances: a marksman on a pad that fires, alone, for SAI_SECS seconds at a still Bond (healed every tick; a hit is a
--      drop in his health): shots fired, hits, the first shot's delay.
-- Globals in front of this file (tests/sniper/ai.sh): SAI_ACC (accuracy rating, default 50), SAI_SECS (default 25), SAI_BEH (default snipe),
-- SAI_HP (health multiplier, default 4), SAI_SKIP (1: skip the sight sweep, use the ladder of the pads in SAI_PADS).
local ACC_V, SECS_V, BEH_V, HP_V = SAI_ACC or 50, SAI_SECS or 25, SAI_BEH or "snipe", SAI_HP or 4

local cand, fires, losmap = {}, {}, {}
local LADDER = { 2700, 3400, 4100, 6000, 8000, 10000, 12000, 14000 }
if SAI_LADDER then LADDER = {}; for n in SAI_LADDER:gmatch("%d+") do LADDER[#LADDER + 1] = tonumber(n) end end
local batch_i = 1
local group = nil
local tt = 0
local phase = "wait"
local bx, by, bz
local live = {}

local function dist(x, z) return math.sqrt((x - bx) ^ 2 + (z - bz) ^ 2) end
local function fired_of(c) return spawn.shots(c) or 0 end

on("stage_start", function()
  bond.warp(0, SAI_BOND or 140)
  bx, by, bz = pads.pos(SAI_BOND or 140)
  for _, id in ipairs(pads.far_from(bx, bz, 2500, 17000, true)) do cand[#cand + 1] = id end
  table.sort(cand)
  print(string.format("SAI start: Bond at (%.0f, %.0f, %.0f), %d candidate pads", bx, by, bz, #cand))
  bond.give(0, "sniperrifle", 99)
  pcall(bond.equip, 0, "sniperrifle")
  phase = "sweep"
end)

local hits, shots, first = 0, 0, nil
local hp_last
on("tick", function(dt)
  tt = tt + dt
  bond.heal(0, 1, 0)
  if phase == "sweep" and tt >= 2 then
    if group and tt - group.t0 >= 2 then
      for _, r in ipairs(group.list) do
        local n = fired_of(r.chr)
        local ok = combat.los(r.chr, 0)
        fires[r.pad] = n
        losmap[r.pad] = ok and true or false
        spawn.remove(r.chr)
      end
      group = nil
    end
    if not group then
      if batch_i > #cand then
        local ids, parts = {}, {}
        for pad, n in pairs(fires) do if n > 0 then ids[#ids + 1] = pad end end
        table.sort(ids)
        for _, pad in ipairs(ids) do
          local x, y, z = pads.pos(pad)
          parts[#parts + 1] = string.format("%d@%.0f/%.0f%s", pad, dist(x, z), y, losmap[pad] and "L" or "")
        end
        local nl = 0
        for _, v in pairs(losmap) do if v then nl = nl + 1 end end
        print(string.format("SAI SWEEP: %d of %d pads fire at Bond (combat.los says %d see him): %s", #ids, #cand, nl, table.concat(parts, " ")))
        live.ladder = {}
        for _, d in ipairs(LADDER) do
          local best, bd
          for _, pad in ipairs(ids) do
            local x, _, z = pads.pos(pad)
            local e = math.abs(dist(x, z) - d)
            if not bd or e < bd then best, bd = pad, e end
          end
          if best and bd < 1300 then live.ladder[#live.ladder + 1] = best end
        end
        live.step = 0
        phase = "duel"
        return
      end
      group = { t0 = tt, list = {} }
      for _ = 1, 8 do
        local pad = cand[batch_i]
        if not pad then break end
        batch_i = batch_i + 1
        local x, _, z = pads.pos(pad)
        local ok, c = pcall(spawn.chr, pad, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = BEH_V, accuracy = ACC_V, health = 4, sight = 20000,
                                              face = { bx, bz } })
        if ok and c then group.list[#group.list + 1] = { pad = pad, chr = c } else fires[pad] = 0 end
      end
    end
    return
  end
  if phase ~= "duel" then return end
  if not live.t0 or tt - live.t0 >= SECS_V then
    if live.chr then
      print(string.format("SAI duel %d pad %d dist %.0f: %d shots, %d hits on a still Bond (%.0f%%, damage %.2f), first shot after %s s (%s acc %d hp x%s)", live.step, live.pad,
        live.d, shots, hits, shots > 0 and 100 * hits / shots or 0, live.dmg or 0, first and string.format("%.1f", first) or "-", BEH_V, ACC_V, tostring(HP_V)))
      spawn.remove(live.chr)
      live.chr = nil
    end
    live.step = live.step + 1
    local pad = live.ladder[live.step]
    if not pad then
      print("SAI done")
      phase = "done"
      return
    end
    local x, _, z = pads.pos(pad)
    local c = spawn.chr(pad, { skin = "arctic_commando", weapon = "sniperrifle", behaviour = BEH_V, accuracy = ACC_V, health = HP_V, sight = 20000,
                               face = { bx, bz } })
    live.chr, live.pad, live.d, live.t0 = c, pad, dist(x, z), tt
    shots, hits, first, hp_last = 0, 0, nil, nil
    live.fs, live.dmg, live.hit_first = 0, 0, nil
    print(string.format("SAI duel %d: chr %s on pad %d at %.0f", live.step, tostring(c), pad, live.d))
    return
  end
  if live.chr then
    local n = fired_of(live.chr)
    if n > live.fs then
      if not first then first = tt - live.t0 end
      shots = shots + (n - live.fs)
      live.fs = n
    end
  end
end)

-- a hit on Bond: the damage event (the stock hit path), counted for the duel's marksman
on("damage", function(victim, attacker, amount)
  if live.chr and victim == bond.id(0) and attacker == live.chr then
    hits = hits + 1
    if not live.hit_first then live.hit_first = shots end
    live.dmg = (live.dmg or 0) + (amount or 0)
    pcall(bond.heal, 0, 1, 0)   -- (from inside the damage path: so the run goes on after a hit)
  end
end)
