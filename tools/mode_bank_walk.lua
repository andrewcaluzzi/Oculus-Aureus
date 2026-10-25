-- Walk the generated level (designs/bank-heist.md P2.3, modding/levelgen.md): GE_MODE=bank-walk GE_STAGE=levelgen. Shipped as
-- Data/Scripts/modes/bank-walk.lua; "Walk Janus Bank.bat" starts it on the Janus bank (tools/levelgen/janus_bank.json on the slot).
-- No guards, no heist rules: Bond is invincible, every keyed door opens for him (the level reset's unlock cheat; the padlock on the
-- dock shutter still has to be shot off), and the HUD names the room and area he is in.
include("levelgen_layout")
mods.set("invincible", true)

local last = {}
on("stage_start", function()
  print(string.format("ge: bank-walk: %s ready, %d rooms, start pad %d", LEVEL.name, #LEVEL.room_name, LEVEL.start_pad))
end)

on("tick", function(dt)
  for p = 0, bond.count() - 1 do
    local r = bond.room(p)
    if r and r ~= last[p] then
      last[p] = r
      local name = LEVEL.room_name[r] or tostring(r)
      local R = LEVEL.rooms[name]
      local area = R and R.area and R.area ~= name and ("  (" .. R.area .. ")") or ""
      hud.set(1, string.upper(name) .. area, p)
      hud.set(2, "B opens doors. Shoot the padlock on the dock shutter.", p)
      print(string.format("ge: bank-walk: Bond %d is in %s (%d)", p, name, r))
    end
  end
end)
