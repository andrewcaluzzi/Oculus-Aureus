-- Probe mode: logs what a mode sees on a stock stage. Run:
--   GE_MODE=probe GE_STAGE=bunker2 GE_SKIPINTRO=1 ./ProjectMidas      (Linux build under Xvfb for the headless runs)
-- Output is "ge: probe: ..." lines (C side: port/src/port_probe.c) and "ge: lua: PROBE ..." lines (this file).
-- Nothing here changes the game. Results for Bunker 2: designs/bank-heist-bunker2.md.

local GUNS = { "fist", "knife", "throwknife", "wppk", "wppksil", "tt33", "skorpion", "ak47", "uzi", "mp5k", "mp5ksil",
               "spectre", "m16", "fnp90", "shotgun", "autoshot", "sniperrifle", "ruger", "goldengun", "grenadelaunch", "rocketlaunch" }

-- GE_PROBE_KEEPLOCKS=1: ask the level reset to leave the keyed doors locked (mode.keep_locks, top level: it must run before the 1 s mark)
if mode.env_flag("GE_PROBE_KEEPLOCKS") then mode.keep_locks() end
local doorsAge, doorsLogged = 0, false
on("tick", function(dt)
  doorsAge = doorsAge + dt
  if not doorsLogged and doorsAge >= 3 then
    doorsLogged = true
    print("PROBE doors_unlocked (UNLOCK DOORS cheat on for Bond) = " .. tostring(probe.doors_unlocked()))
  end
end)

local function fmt(v) if v == nil then return "nil" end return string.format("%.3f", v) end

local function loudness()
  print("PROBE hearing: radius = hearingscale * noise * 100 (chr.c chrCheckGuardsHeardSound); stock hearingscale = 1.0 (chr.c:1742)")
  print("PROBE hearing: noise is the gun's own value, bumped by noiseshot per shot, capped at loudnessmax, decaying to loudnessmin")
  print("PROBE hearing: gun | loudnessmin loudnessmax noiseshot | radius at min / after 1 shot / at max (scale 1.0)")
  for _, g in ipairs(GUNS) do
    local ok, mn = pcall(weapon.get, g, "loudnessmin")
    if ok then
      local mx = weapon.get(g, "loudnessmax")
      local ns = weapon.get(g, "noiseshot")
      local one = math.min(mx, mn + ns)
      print(string.format("PROBE hearing: %-14s | %s %s %s | %.0f / %.0f / %.0f", g, fmt(mn), fmt(mx), fmt(ns), mn * 100, one * 100, mx * 100))
    else
      print("PROBE hearing: " .. g .. " | no stats")
    end
  end
end

on("stage_start", function(stage)
  print("PROBE begin stage=" .. tostring(stage) .. " mode=" .. mode.name())
  local x, y, z = bond.pos()
  print(string.format("PROBE bond start pos=%.0f,%.0f,%.0f pads=%d", x or 0, y or 0, z or 0, pads.count()))
  probe.level()
  probe.pads()
  loudness()
  print("PROBE end")
end)

-- GE_PROBE_CCTV=1: after 3 s warp Bond to pad 57 (the look target of the Bunker 2 camera at idx 95, with a clear line of sight) and log the
-- alarm timer once a second. Shows that the stock camera tick still runs in a mode (it raises the alarm after 300 ticks of unbroken sight).
-- GE_PROBE_SCAN=1: warp Bond to every pad in turn (one per tick) and log each camera's live state with the stock line-of-sight result.
-- GE_PROBE_TOUR=1: warp Bond to a fixed list of Bunker 2 pads in turn, 1.25 s apart (with GE_SHOT=15 this gives a few screenshots per pad).
-- Scripts cannot read the environment, so the list is in the file.
local tour, tourN = { 51, 85, 54, 82, 12, 9, 49, 105, 2, 26 }, 0

local t, warped, scan = 0, false, 0
on("tick", function(dt)
  if mode.env_flag("GE_PROBE_TOUR") then
    tourN = tourN + 1
    if tourN >= 8 and (tourN - 8) % 5 == 0 and tour[(tourN - 8) // 5 + 1] then
      local pad = tour[(tourN - 8) // 5 + 1]
      bond.warp(0, pad)
      print("PROBE tour warp pad " .. pad)
    end
    return
  end
  if mode.env_flag("GE_PROBE_SCAN") then
    t = t + dt
    if t >= 2 and scan < 2 * pads.count() then
      if scan % 2 == 0 then
        bond.warp(0, scan // 2)
      else
        print("PROBE scan pad " .. (scan // 2))
        probe.cctv()
      end
      scan = scan + 1
    end
    return
  end
  if not mode.env_flag("GE_PROBE_CCTV") then return end
  t = t + dt
  if not warped and t >= 3 then
    local pad = 57
    bond.warp(0, pad)
    warped = true
    t = 0
    print("PROBE cctv test: warped Bond to pad " .. pad)
  end
  local a = probe.alarm()
  if warped and math.floor(t * 4) % 4 == 0 then
    print(string.format("PROBE cctv t=%.2f alarm=%d", t, a))
    probe.cctv()
  end
end)
