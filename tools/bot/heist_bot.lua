-- The Bank Heist played by the player bot. Shipped to Data/Scripts/modes/heist_bot.lua by the build.
--   GE_MODE=heist_bot GE_STAGE=bunker2 GE_PLAYERBOT=1
-- It runs the real heist (tools/mode_heist.lua, unchanged) and feeds the bot (port_playerbot.c) its steps through the Lua
-- `bot` library as the heist moves on (modding/bot.md). The bot's own door logic fetches the key cards: a card door on the
-- route sends it to the card's carrier (it does not knock carriers out yet - it shoots them, which counts as a civilian
-- killed when the carrier is staff).
include("heist")
local H = HEIST
local CRACK_PAD, EXIT_PAD = 85, 51
local VAULT_DOOR = 68

local function push(line)
  local ok, err = bot.push(line)
  if not ok then print("ge: heist_bot: bad step '" .. line .. "': " .. tostring(err)) end
end

local function idle()
  local s, n = bot.step()
  return s > n
end

local state = "approach"
local looted = {}

local function face_vault()
  local d = door.get(VAULT_DOOR)
  if d then push(string.format("face %.0f %.0f %.0f", d.x, d.y, d.z)) end
end

local function start_crack()
  push("goto pad " .. CRACK_PAD .. " 50")
  face_vault()
  push("press b 3")
  push("wait 30")
end

on("tick", function(dt)
  if bot.active() ~= 1 or not H.ready then return end
  local ph = H.phase
  if state == "approach" then
    print("ge: heist_bot: to the vault antechamber (pad " .. CRACK_PAD .. ")")
    start_crack()
    state = "crack"
  elseif state == "crack" then
    if ph == "jammed" and idle() then
      print("ge: heist_bot: the cracker jammed - restarting it")
      start_crack()
    elseif ph == "casing" and idle() then
      start_crack()                      -- the first press missed (not in the zone yet): again
    elseif ph == "cracking" and idle() then
      push("goto pad " .. CRACK_PAD .. " 120")   -- hold the zone, fight what comes
      push("kill 900")
    elseif ph == "open" then
      bot.clear()
      state = "loot"
      print("ge: heist_bot: the vault is open - looting")
    end
  elseif state == "loot" then
    if idle() then
      local n = 0
      for i, it in ipairs(H.loot_items) do
        if it.where == "vault" and not looted[i] and spawn.item_alive(it.h) and n < 6 then
          local _, y = pads.pos(it.pad)
          push(string.format("goto %.0f %.0f %.0f 15", it.x, y, it.z))
          looted[i] = true
          n = n + 1
        end
      end
      if n == 0 or ph == "closing" or ph == "sealed" then
        state = "exit"
      end
    end
    if ph == "closing" then bot.clear(); state = "exit" end
  elseif state == "exit" then
    print(string.format("ge: heist_bot: leaving with %d bar(s), %d cash", H.bars, H.cash))
    push("goto pad " .. EXIT_PAD .. " 80")
    push("exit")
    state = "out"
  end
end)
