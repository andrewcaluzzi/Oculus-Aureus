-- heist_lib.lua: helpers for heist-style modes (designs/bank-heist.md). Use from a mode script with include("heist_lib").
-- Install: copy next to the mode scripts (Data/Scripts/modes/heist_lib.lua).
--
--   heist.reinforce{ pads = {25, 50}, interval = 25, per_wave = 3, max_alive = 16, opts = { weapon = "ak47", behaviour = "chase" }, first = 0 }
--     While alarm.active(), every `interval` seconds spawn `per_wave` guards at the entrance `pads` (round robin), never more than
--     `max_alive` of them alive at once (and never if spawn.free() is below `reserve`, default 12). `first` = delay before the first wave
--     after the alarm starts (default 0). Returns the controller: r.alive() -> count, r.spawned -> total, r.stop(), r.waves.
--     The wave guards are tagged: r.is_wave(chr). One reinforcement controller per call; the alarm event restarts the first-wave timer.
heist = heist or {}

function heist.reinforce(cfg)
  local pads = cfg.pads or {}
  local interval = cfg.interval or 25
  local per_wave = cfg.per_wave or 3
  local max_alive = cfg.max_alive or 16
  local reserve = cfg.reserve or 12
  local opts = cfg.opts or { weapon = "ak47", behaviour = "chase" }
  local r = { spawned = 0, waves = 0, mine = {}, running = true }
  local clock, nextwave, ring = 0, nil, 0
  local was_active = false

  function r.alive()
    local n = 0
    for chr in pairs(r.mine) do
      if spawn.alive(chr) and not spawn.down(chr) then n = n + 1 else r.mine[chr] = nil end
    end
    return n
  end
  function r.is_wave(chr) return r.mine[chr] ~= nil end
  function r.stop() r.running = false end

  local function wave()
    local alive = r.alive()
    local made = 0
    for _ = 1, per_wave do
      if alive + made >= max_alive or spawn.free() < reserve or #pads == 0 then break end
      local chr
      for _ = 1, #pads do   -- a pad whose spot is still occupied by the last arrival fails: try the next one
        ring = ring % #pads + 1
        local o = {}
        for k, v in pairs(opts) do o[k] = v end
        chr = spawn.chr(pads[ring], o)
        if chr then break end
      end
      if chr then r.mine[chr] = true; made = made + 1; r.spawned = r.spawned + 1 end
    end
    if made > 0 then r.waves = r.waves + 1 end
    return made
  end

  on("tick", function(dt)
    if not r.running then return end
    clock = clock + dt
    local active = alarm.active()
    if active and not was_active then nextwave = clock + (cfg.first or 0) end
    was_active = active
    if active and nextwave and clock >= nextwave then
      wave()
      nextwave = clock + interval
    end
  end)
  return r
end
