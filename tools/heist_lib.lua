-- heist_lib.lua: helpers for heist-style modes (designs/bank-heist.md). Use from a mode script with include("heist_lib").
-- Install: copy next to the mode scripts (Data/Scripts/modes/heist_lib.lua).
--
--   heist.reinforce{ pads = {25, 50}, interval = 25, per_wave = 3, max_alive = 16, opts = { weapon = "ak47", behaviour = "chase" }, first = 0 }
--     While alarm.active(), every `interval` seconds spawn `per_wave` guards at the entrance `pads` (round robin), never more than
--     `max_alive` of them alive at once (and never if spawn.free() is below `reserve`, default 12). `first` = delay before the first wave
--     after the alarm starts (default 0). Returns the controller: r.alive() -> count, r.spawned -> total, r.stop(), r.waves.
--     The wave guards are tagged: r.is_wave(chr). One reinforcement controller per call; the alarm event restarts the first-wave timer.
--     Optional: `active = function() return hot end` replaces alarm.active() as the "waves run" test (a mode's own latch: the stock siren
--     times out after ~30 s); `opts` may be a function returning a fresh spawn.chr table per guard (variety); r.wave_now() spawns a wave at once.
--     A pad whose spot is taken (a guard posted on it, the last arrival) is tried a step aside (`offsets`, default 0 and 110 units each
--     way) before the next pad: the heist's hall pads all have posted guards, and its waves never came in before.
heist = heist or {}

function heist.reinforce(cfg)
  local pads = cfg.pads or {}
  local interval = cfg.interval or 25
  local per_wave = cfg.per_wave or 3
  local max_alive = cfg.max_alive or 16
  local reserve = cfg.reserve or 12
  local opts = cfg.opts or { weapon = "ak47", behaviour = "chase" }
  local offsets = cfg.offsets or { { 0, 0 }, { 110, 0 }, { -110, 0 }, { 0, 110 }, { 0, -110 } }
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
      for _ = 1, #pads do   -- a pad whose spot is still occupied (the last arrival, a guard posted there) fails: a step aside, then the next pad
        ring = ring % #pads + 1
        local o = {}
        if type(opts) == "function" then o = opts(r.spawned + made + 1) else for k, v in pairs(opts) do o[k] = v end end
        for _, off in ipairs(offsets) do
          o.offset = (off[1] ~= 0 or off[2] ~= 0) and { off[1], off[2] } or nil
          chr = spawn.chr(pads[ring], o)
          if chr then break end
        end
        if chr then break end
      end
      if chr then r.mine[chr] = true; made = made + 1; r.spawned = r.spawned + 1 end
    end
    if made > 0 then r.waves = r.waves + 1 end
    return made
  end

  function r.wave_now() return wave() end

  on("tick", function(dt)
    if not r.running then return end
    clock = clock + dt
    local active
    if cfg.active then active = cfg.active() and true or false else active = alarm.active() end
    if active and not was_active then nextwave = clock + (cfg.first or 0) end
    was_active = active
    if active and nextwave and clock >= nextwave then
      wave()
      nextwave = clock + interval
    end
  end)
  return r
end
