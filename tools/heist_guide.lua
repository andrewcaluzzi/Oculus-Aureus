-- heist_guide.lua: the first-play guide for the bank heist (designs/bank-heist.md section 23d). Presentation only.
-- Included by tools/mode_heist.lua; heist_guide(ctx) returns G with
--   G.objective(p) -> text or nil   HUD line 2 while casing: always the next concrete step (nil = the mode's own objective line)
--   G.line3(p)     -> text or nil   HUD line 3 when nothing else is on it: a prompt near something you can use, else the opening key
--                                   strip (the first ~40 s), else for 8 s after the objective changes, how to do it
--   G.tag(text)    -> text          HUD line 1 with "TAB: INTEL" on the end when it fits (and the board exists)
-- ctx = { S, H, PW, M (heist_bank_map()), keys = { GREEN, BLUE, RED, GOLD, FLOORKEY } }
-- Everything here only reads the mode's state and the world and writes HUD text (hud.set, by the caller): no sim state, no
-- math.random, nothing in mode.state. Its own memory (the last objective per Bond, when it changed) only picks which text to show.
-- S.guide = false turns it off (the mode's old lines come back).

local LINE = 63                                    -- a HUD line holds 63 characters

function heist_guide(ctx)
  local S, H, M, K = ctx.S, ctx.H, ctx.M, ctx.keys
  local JG = (ctx.job or heist_job()).guide          -- the job's steps and words (tools/jobs/<job>.lua)
  local L = LEVEL
  local G = {}
  local P = M.P or {}

  local function fit(t) return #t <= LINE and t or t:sub(1, LINE) end
  local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
  local function room_label(r)
    local n = r and L.room_name and L.room_name[r]
    return n and n:gsub("_", " "):upper() or nil
  end

  -- where a carrier is now (the room's name), or nil
  local function carrier_room(role)
    local c = H.carriers[role]
    if not c or not c.chr or not spawn.alive(c.chr) then return nil end
    return room_label(spawn.room(c.chr))
  end
  local function inside(p)
    local r = bond.room(p)
    local n = r and L.room_name[r]
    return n and L.rooms[n] and L.rooms[n].area ~= "street"
  end

  -- the step and its two lines: what (line 2) and how (line 3, for a while after it changes)
  local got_in = {}
  local function step(p)
    if inside(p) then got_in[p] = true end
    if not got_in[p] then
      return "in", JG.in_what, S.cover and JG.in_how_cover or JG.in_how
    end
    -- the job's card ladder (JOB.guide.steps; the bank: GREEN, BLUE, RED)
    for _, st in ipairs(JG.steps) do
      if not bond.has_key(p, K[st.key]) then
        if not st.role then return st.id, st.what, st.how end
        local r = carrier_room(st.role)
        return st.id, fit(st.what .. (r and (" (" .. r .. ")") or st.none)), st.how
      end
    end
    return "vault", JG.vault_what, JG.vault_how
  end

  function G.objective(p)
    if H.phase ~= "casing" then return nil end
    local id, what = step(p)
    return what
  end

  -- a prompt near something usable, from the world as it is now
  local function near_prompt(p)
    local x, _, z = bond.pos(p)
    if not x then return nil end
    -- a knocked-out carrier who still has his card: search him
    for role, c in pairs(H.carriers) do
      if c.chr and c.key and ko.is(c.chr) then
        local alive, held = spawn.key_alive(c.key)
        local cx, cz = spawn.where(c.chr)
        if alive and held and cx and d2(x, z, cx, cz) < 200 then
          return "Out cold with a card: stand over the body, press USE to search"
        end
      end
    end
    -- a dropped card on the floor nearby: walk onto it
    for role, c in pairs(H.carriers) do
      if c.key then
        local alive, held, kx, kz = spawn.key_alive(c.key)
        if alive and not held and kx and d2(x, z, kx, kz) < 400 then
          return "A key card is on the floor here: walk over it to pick it up"
        end
      end
    end
    -- a vent grille or a piton point
    for _, ab in pairs(M.VENTS or {}) do
      for _, pd in ipairs(ab) do
        local vx, _, vz = pads.pos(pd)
        if vx and d2(x, z, vx, vz) < 150 then return "Vent: press USE to crawl through" end
      end
    end
    for _, lh in pairs(M.GRAPPLES or {}) do
      local gx, _, gz = pads.pos(lh[1])
      if gx and d2(x, z, gx, gz) < 220 then return "Piton point: pick the PITON GUN (next weapon), face up, fire" end
    end
    if H.hostage_prompt and H.hostage_prompt(p) then return H.hostage_prompt(p) end   -- holding one / a civilian just surrendered (section 31)
    if H.interlock_prompt then return H.interlock_prompt(p, x, z) end   -- a mantrap / the sally port (tools/heist_interlock.lua)
    return nil
  end

  -- the opening key strip: one tip at a time after the opening line (8 s), each 8 s
  local TIPS = {
    "TAB (pad: BACK) opens the INTEL board: the plan, maps, who has keys",
    "Next/prev weapon: pick a gadget (piton, lockpicks, coins, gas)",
    "SHIFT+F3: AR specs on/off (brackets show who carries a card)",
    "USE: search bodies, crawl vents, watch the camera screens",
  }
  local TIP0, TIPLEN = 8, 8

  local last_id, changed_at = {}, {}
  function G.line3(p)
    if S.guide == false then return nil end
    local pr = near_prompt(p)
    if pr then return fit(pr) end
    local t = H.elapsed
    local k = math.floor((t - TIP0) / TIPLEN) + 1
    if t >= TIP0 and k >= 1 and k <= #TIPS then return fit(TIPS[k]) end
    if H.phase == "casing" then
      local id, _, how = step(p)
      if id ~= last_id[p] then last_id[p], changed_at[p] = id, t end
      local from = math.max(changed_at[p], TIP0 + #TIPS * TIPLEN)   -- after the strip, then for 10 s after each new step
      if how and t >= from and t - from < 10 then return fit(how) end
    end
    return nil
  end

  function G.tag(text)
    if S.intel == false then return text end
    local t = text .. "  TAB: INTEL"
    return #t <= LINE and t or text
  end

  return G
end
