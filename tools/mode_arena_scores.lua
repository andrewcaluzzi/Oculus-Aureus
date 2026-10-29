-- Arena best scores and medals (drop A7, modding/arena.md). Shipped to Data/Scripts/modes/_arena_scores.lua and included by
-- arena.lua. Defines the global ARENA_SCORES.
--
-- The bests live in arena_scores.ini next to midas.ini (C side, port_arenascores.c: Lua has no io), one set per pacing:
--   endless_best_wave / endless_best_kills / endless_best_time (longest run, seconds) / endless_runs / endless_best_medal (rank 0..4)
--   fixed<N>_best_wave / _best_kills / _best_time (FASTEST clear of all N waves, seconds) / _runs / _best_medal / _clears
-- Solo only (scores.enabled()). Test hooks (first_wave, debug_queue, debug_kill, debug_clear_after) do not record unless scores_force.
-- GE_NOSCORESAVE=1 keeps the file untouched (tests); GE_SCOREFILE=<path> uses another file.
-- Settings: scores (true; false = off), scores_force (tests), medals_endless (waves for bronze/silver/gold/platinum: {5, 10, 20, 30}),
--   medal_par (fixed mode: a clear within this many seconds per wave is PLATINUM; 75 = 25:00 for 20 waves).
-- Medals in fixed mode: BRONZE = a quarter of the waves, SILVER = half, GOLD = cleared, PLATINUM = cleared within par.

ARENA_SCORES = {}

local MEDALS = (type(ARENA_THEME) == "table" and ARENA_THEME.medals) or { "BRONZE", "SILVER", "GOLD", "PLATINUM" }   -- (the Bond theme: arena_theme.lua)
ARENA_SCORES.MEDALS = MEDALS

local function mmss(s) s = math.floor(s); return string.format("%d:%02d", s // 60, s % 60) end

-- (score_key: the start menu's per-preset key, tools/arena_menu_defs.lua; a plain run keys by its pacing as before)
local function prefix(S) return S.score_key or ((S.mode == "endless") and "endless" or ("fixed" .. tostring(S.waves))) end
ARENA_SCORES.prefix = prefix

-- 0 = none, 1..4 = MEDALS
function ARENA_SCORES.medal(S, r)
  if S.mode == "endless" then
    local m, t = 0, S.medals_endless or { 5, 10, 20, 30 }
    for i = 1, 4 do if r.wave >= t[i] then m = i end end
    return m
  end
  if r.win then
    return (r.time <= (S.medal_par or 75) * S.waves) and 4 or 3
  end
  local done = r.wave - 1   -- the wave he fell in was not cleared
  if done >= math.ceil(S.waves / 2) then return 2 end
  if done >= math.ceil(S.waves / 4) then return 1 end
  return 0
end

function ARENA_SCORES.recording(S)
  if S.scores == false or not scores or not scores.enabled() then return false end
  if S.scores_force then return true end
  return not (S.first_wave or S.debug_queue or S.debug_kill or S.debug_clear_after)
end

-- the stored bests for the GET READY line (nil when none)
function ARENA_SCORES.best_line(S)
  if not ARENA_SCORES.recording(S) then return nil end
  local p = prefix(S)
  local w, t = scores.get(p .. "_best_wave"), scores.get(p .. "_best_time")
  if S.mode == "endless" then
    if w then return string.format("BEST: WAVE %d", w) end
  elseif t then
    return "BEST: " .. mmss(t)
  elseif w then
    return string.format("BEST: WAVE %d", w)
  end
  return nil
end

-- The run is over: r = { win=, wave=, kills=, time= }. Records the bests and returns { medal=, news = {..}, hud1=, hud2=, debrief = {..} }.
function ARENA_SCORES.finish(S, r)
  local out = { medal = ARENA_SCORES.medal(S, r), news = {}, debrief = {} }
  local rec = ARENA_SCORES.recording(S)
  local p = prefix(S)
  local function best(key, v, higher)
    local old = rec and scores.get(p .. "_" .. key) or nil
    local better = old == nil or (higher and v > old) or (not higher and v < old)
    if rec and better then scores.set(p .. "_" .. key, v) end
    return better and rec, old
  end
  local nw, ow = best("best_wave", r.wave, true)
  local nk, ok = best("best_kills", r.kills, true)
  local nt, ot = false, nil
  if S.mode == "endless" then nt, ot = best("best_time", r.time, true)
  elseif r.win then nt, ot = best("best_time", r.time, false) end
  local nm, om = best("best_medal", out.medal, true)
  if rec then
    scores.set(p .. "_runs", (scores.get(p .. "_runs") or 0) + 1)
    if r.win then scores.set(p .. "_clears", (scores.get(p .. "_clears") or 0) + 1) end
  end
  if nw and ow then out.news[#out.news + 1] = string.format("WAVE %d", r.wave) end
  if nk and ok then out.news[#out.news + 1] = string.format("KILLS %d", r.kills) end
  if nt and ot then out.news[#out.news + 1] = "TIME " .. mmss(r.time) end
  local first = rec and ow == nil
  local mname = out.medal > 0 and MEDALS[out.medal] or nil
  out.hud1 = mname and ("MEDAL: " .. mname) or "NO MEDAL"
  if first then out.hud2 = "FIRST RUN RECORDED"
  elseif #out.news > 0 then out.hud2 = "NEW BEST: " .. table.concat(out.news, "  ")
  elseif rec then out.hud2 = string.format("BEST: WAVE %d  KILLS %d", math.floor(ow or 0), math.floor(ok or 0))
  else out.hud2 = "" end
  local d = out.debrief
  local where = S.map_name and (S.map_name .. ": ") or "Arena: "   -- (a stock map names itself, tools/arena_maps.lua)
  d[#d + 1] = (S.mode == "endless") and (where .. "endless") or string.format("%s%d waves - %s", where, S.waves, r.win and "CLEARED" or "fell")
  d[#d + 1] = string.format("Wave %d   Kills %d   Time %s", r.wave, r.kills, mmss(r.time))
  d[#d + 1] = "Medal: " .. (mname or "none")
  if #out.news > 0 then d[#d + 1] = "NEW BEST: " .. table.concat(out.news, ", ")
  elseif first then d[#d + 1] = "First run recorded"
  elseif rec then
    d[#d + 1] = string.format("Best: wave %d, kills %d%s", math.floor(ow or 0), math.floor(ok or 0),
      ot and (", time " .. mmss(ot)) or "")
  end
  if r.extra then d[#d + 1] = r.extra end
  print(string.format("ge: arena: SCORES %s: wave %d kills %d time %s medal %s; new best: wave %s kills %s time %s medal %s; recorded %s (%s)",
    p, r.wave, r.kills, mmss(r.time), mname or "none", tostring(nw), tostring(nk), tostring(nt), tostring(nm), tostring(rec),
    (scores and scores.path) and scores.path() or "-"))
  return out
end
