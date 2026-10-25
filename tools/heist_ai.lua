-- heist_ai.lua - the guards' hearing, awareness and barks for the heist (designs/bank-heist.md section 18). Included by mode_heist.lua;
-- the work is in C (port/src/port_noise.c, port_ai.c; Lua noise.* and ai.*, port/luavm/port_luavm_ai.c). This file sets it up from the
-- mode's settings table S and the difficulty, registers the bank's guards, and hands the layout's patrol routes to them.
--
-- Settings (S, or HEIST_SETTINGS in a test): every one optional, nil = the built-in value.
--   ai = true                      the whole layer (false: stock guards: they see and hear shots, nothing else)
--   hearing_slap 700, hearing_thud 650, hearing_step 60, hearing_run 330, hearing_door 420, hearing_glass 900, hearing_coin 450,
--   hearing_alarm 3500            how far each noise carries in the open at hearing scale 1 (units). Each guard's own hearing scale
--                                  (S.hearing x the difficulty's `hearing`) multiplies it. Gunshots use the gun's own noise (stock).
--   hearing_portal 0.6            the radius left through each open doorway or opening on the way
--   hearing_door_factor 0.25      ... through each shut door
--   hearing_wall 0.1              ... through walls (the least a sound keeps between any two rooms)
--   ai_tune = {}                  ai.tune{} overrides: turn, alert_turn, look, search_time, search_radius, search_pads, decay,
--                                  susp_level, search_level, checkin, backup, backup_range, cover_noise, step_speed, run_speed,
--                                  attribute, near, weight_<kind>
--   patrols = true                some posted guards walk patrol routes (the layout's patrol_<route>_<n> pads, else the named
--                                  routes below: patrols_named = false turns those off)
--   ai_difficulty                 per difficulty (0 Agent .. 3 007) ai.tune values on top (below: harder = quicker to react, a shorter
--                                  check-in, more backup)

HEIST_AI = HEIST_AI or {}
local A = HEIST_AI

local KIND_CODE = { post = 1, raiser = 2, carrier = 3, investigator = 4, squad = 5, vault = 6, wave = 7, response = 8 }
local DIFF_AI = {
  [0] = { turn = 1.4, alert_turn = 0.6, search_time = 25, checkin = 35, backup = 1 },   -- Agent
  [1] = { turn = 1.2, alert_turn = 0.5, search_time = 30, checkin = 25, backup = 2 },   -- Secret Agent
  [2] = { turn = 1.0, alert_turn = 0.4, search_time = 35, checkin = 20, backup = 2 },   -- 00 Agent
  [3] = { turn = 0.8, alert_turn = 0.3, search_time = 40, checkin = 15, backup = 3 },   -- 007
}
local HEAR = { "slap", "thud", "step", "run", "door", "glass", "coin", "alarm" }

local function log(fmt, ...) print(string.format("ge: heist: ai: " .. fmt, ...)) end

-- at stage start (after the cover): turn the layer on and tune it
function A.setup(S, H)
  A.S, A.H = S, H
  A.on = (S.ai ~= false) and ai ~= nil and noise ~= nil
  if not A.on then
    if ai then ai.enable(false) end
    return
  end
  ai.enable(true)
  local nt = {}
  for _, k in ipairs(HEAR) do if S["hearing_" .. k] then nt[k] = S["hearing_" .. k] end end
  if S.hearing_portal then nt.portal = S.hearing_portal end
  if S.hearing_door_factor then nt.door_factor = S.hearing_door_factor end
  if S.hearing_wall then nt.wall = S.hearing_wall end
  noise.tune(nt)
  local d = mission.difficulty()
  local dt = (S.ai_difficulty or DIFF_AI)[d] or DIFF_AI[1]
  ai.tune(dt)
  ai.tune(S.ai_tune or {})
  A.routes_done = false
  log("on: difficulty %d, slap heard to %.0f, through a shut door x%.2f", d, noise.radius("slap"), S.hearing_door_factor or 0.25)
end

-- mode_heist.lua note_guard: every guard of the bank (posted, raisers, carriers, investigators, squads, waves) is tracked
function A.guard(c, kind)
  if A.on and c then ai.guard(c, KIND_CODE[kind] or 0) end
end

-- the layout's patrol routes: pads named patrol_<route>_<n> (n = 1, 2, ...). The posted guard nearest each route's first pad walks it
-- in a loop while he is unaware (bank v2: tools/levelgen/build_janus_bank.py; the bank3 layout uses the same names).
-- without patrol_* pads in the layout: these routes by pad name (bank v2), each a loop on one floor with no keyed door on the way
local NAMED_ROUTES = {
  { name = "hall", pads = { "hall_post_sw", "hall_post_nw", "hall_post_ne", "hall_post_se" } },
  { name = "offices", pads = { "ocorr_s", "ocorr_mid", "ocorr_n", "ocorr_mid" } },
  { name = "rear", pads = { "rear_w", "rear_mid", "rear_e", "rear_mid" } },
  { name = "security", pads = { "sec_corr_s", "sec_corr_mid", "sec_corr_n", "sec_corr_mid" } },
  { name = "vault", pads = { "vcorr_w", "vcorr_mid", "vcorr_e", "vcorr_mid" } },
  { name = "gallery", pads = { "gal_w_s", "gal_w_mid", "gal_w_n", "gal_w_mid" } },
}

local function routes()
  local R = {}
  for name, id in pairs((LEVEL and LEVEL.pads) or {}) do
    local r, n = tostring(name):match("^patrol_([%w]+)_(%d+)$")
    if r then R[r] = R[r] or {}; R[r][tonumber(n)] = id end
  end
  local names = {}
  for r in pairs(R) do names[#names + 1] = r end
  table.sort(names)
  local out = {}
  for _, r in ipairs(names) do
    local list = {}
    for i = 1, 16 do if R[r][i] then list[#list + 1] = R[r][i] else break end end
    if #list >= 2 then out[#out + 1] = { name = r, pads = list } end
  end
  if #out == 0 and A.S.patrols_named ~= false then
    for _, r in ipairs(NAMED_ROUTES) do
      local list = {}
      for _, n in ipairs(r.pads) do if LEVEL.pads[n] then list[#list + 1] = LEVEL.pads[n] end end
      if #list == #r.pads then out[#out + 1] = { name = r.name, pads = list } end
    end
  end
  return out
end

function A.assign_routes()
  if not A.on or A.routes_done then return end
  A.routes_done = true
  if A.S.patrols == false then return end
  local H = A.H
  local taken, n = {}, 0
  for _, route in ipairs(routes()) do
    local x, _, z = pads.pos(route.pads[1])
    local best, bd = nil, 1e18
    for _, g in ipairs(H.guards) do
      if g.kind == "post" and not taken[g.chr] and spawn.alive(g.chr) then
        local gx, gz = spawn.where(g.chr)
        local d = gx and ((gx - x) ^ 2 + (gz - z) ^ 2) or 1e18
        if d < bd then best, bd = g, d end
      end
    end
    if best and bd < 2500 * 2500 and ai.patrol(best.chr, route.pads) then
      taken[best.chr] = true
      best.route = route.name
      n = n + 1
      log("guard %d patrols route %s (%d pads)", best.chr, route.name, #route.pads)
    end
  end
  A.patrols = n
end

-- the mode's tick (H.ready)
function A.tick()
  if A.on and not A.routes_done then A.assign_routes() end
end

-- a camera's report of a body (port_ai.c: cctv_spotted with player -1): say so on every HUD
on("cctv_spotted", function(cam, p)
  if A.on and p == -1 and A.H and not A.H.hot and not A.H.report then
    local up = A.H.security_up and A.H.security_up() or 1
    A.H.flash = up > 0 and "A camera saw a body - the security room is calling it in" or "A camera saw a body - nobody is watching the screens"
    A.H.flash_until = A.H.elapsed + 4
  end
end)
