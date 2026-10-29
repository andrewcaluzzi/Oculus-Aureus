-- Surface's sight matrix: with Bond warped onto each waypoint pad in turn, which other waypoint pads have a clear stan line to him
-- (combat.los on a spectator put on the pad, the line a "snipe" marksman's IFICouldSeeBond uses)? The offline survey behind the hand
-- picks in tools/sniper_defs.lua (tests/sniper/matrix.sh). Prints "SMAT <bond pad> n_band n_all : pad@dist ..." for the band 6000..15000.
local ids = {}
local lo, hi = SMAT_LO or 6000, SMAT_HI or 15000
local only = SMAT_ONLY   -- optional: a list of Bond pads to try
local bi, k = 0, 1
local state = "next"
local wait = 0
local group
local seen = {}
local bpos

on("stage_start", function()
  for _, id in ipairs(pads.far_from(0, 0, 0, 1e9, true)) do ids[#ids + 1] = id end
  table.sort(ids)
  print(string.format("SMAT start: %d waypoint pads", #ids))
end)

local function d2(x, z) return math.sqrt((x - bpos[1]) ^ 2 + (z - bpos[2]) ^ 2) end

on("ui_tick", function(dt)
  if #ids == 0 then return end
  if state == "next" then
    bi = bi + 1
    local B = only and only[bi] or ids[bi]
    if not B then print("SMAT done"); state = "done"; return end
    local bx, by, bz = pads.pos(B)
    bpos = { bx, bz, B, by }
    bond.warp(0, B)
    seen = {}
    k = 1
    wait = 4
    state = "settle"
  elseif state == "settle" then
    wait = wait - 1
    if wait <= 0 then state = "spawn" end
  elseif state == "spawn" then
    if k > #ids then
      local band, all, parts = 0, 0, {}
      for pad, d in pairs(seen) do
        all = all + 1
        if d >= lo and d <= hi then band = band + 1; parts[#parts + 1] = string.format("%d@%.0f", pad, d) end
      end
      table.sort(parts)
      print(string.format("SMAT %d y%.0f %d %d : %s", bpos[3], bpos[4], band, all, table.concat(parts, " ")))
      state = "next"
      return
    end
    group = {}
    for _ = 1, 12 do
      local pad = ids[k]
      if not pad then break end
      k = k + 1
      local x, y, z = pads.pos(pad)
      local d = d2(x, z)
      if pad ~= bpos[3] and d >= 1500 then
        local ok, c = pcall(spawn.chr, pad, { behaviour = "spectator", weapon = "none" })
        if ok and c then group[#group + 1] = { pad = pad, chr = c, d = d } end
      end
    end
    state = "ask"
  elseif state == "ask" then
    for _, r in ipairs(group) do
      if combat.los(r.chr, 0) then seen[r.pad] = r.d end
      spawn.remove(r.chr)
    end
    group = nil
    state = "spawn"
  end
end)
