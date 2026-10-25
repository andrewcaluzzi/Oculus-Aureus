-- The generated level's test mode (designs/bank-heist.md P2.1, modding/levelgen.md): GE_MODE=levelgen_test GE_STAGE=levelgen.
-- Walk the rooms of the layout compiled onto the slot (tools/levelgen/*.json -> tools/gen_level.py -> port/levelgen). A guard stands
-- on post in every room but the first; the HUD names the room Bond is in. Shipped as Data/Scripts/modes/levelgen_test.lua.
include("levelgen_layout")

local last = {}
on("stage_start", function()
  for id = 2, #LEVEL.room_name do            -- by room id, not pairs(): the same order on every machine
    local r = LEVEL.rooms[LEVEL.room_name[id]]
    if #r.pads > 0 then
      local pad = r.pads[math.floor((#r.pads + 1) / 2)]
      spawn.chr(pad, { weapon = "ak47", post = true })
    end
  end
  print(string.format("ge: levelgen: %s ready, start pad %d", LEVEL.name, LEVEL.start_pad))
end)

on("tick", function(dt)
  for p = 0, bond.count() - 1 do
    local r = bond.room(p)
    if r and r ~= last[p] then
      last[p] = r
      hud.set(1, "ROOM " .. (LEVEL.room_name[r] or tostring(r)), p)
      print(string.format("ge: levelgen: Bond %d is in room %s (%d)", p, LEVEL.room_name[r] or "?", r))
    end
  end
end)
