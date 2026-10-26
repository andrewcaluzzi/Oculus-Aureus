-- heist_ar.lua: the heist's "Q-Branch specs" (the AR overlay) and its diagnostics wiring (designs/bank-heist.md section 20). Shipped
-- next to the mode scripts (Data/Scripts/modes/heist_ar.lua). mode_heist.lua does include("heist_ar") at the top and
-- heist_ar.attach(H, S) at the end, so these handlers run after the mode's own (the map is loaded by then).
--
-- What it does: turns the specs on (S.ar, default true; S.ar_layers = { radar = true, ... } switches layers), puts waypoint diamonds on
-- the vault, the breaker and the exits, names every room and door rule for the diagnostics (F3), and turns the mode's moments into
-- speech bubbles over the guard who says them: "HEY!" (cover blown), the radio-in (cover report, the security room's relay), a body
-- found, "Huh?" as a guard's meter passes half way, a civilian giving up. It also draws the ai stream's guard_bark and noise events when
-- they exist (they are optional: on() refuses an unknown event, so those are hooked through pcall).
--
-- PRESENTATION ONLY: it reads the heist's state and calls ar.* / diag.*; it never changes sim state and never uses math.random (the sim
-- RNG). Its own bookkeeping (who said "Huh?") is in this file's locals, outside mode.state, and nothing in the sim reads it.
heist_ar = heist_ar or {}

local BARK = { huh = "Huh?", hey = "HEY!", investigate = "What was that?", lost = "Lost him...", found_body = "Man down!",
               radio = "Control, come in", backup = "Need backup!", surrender = "Don't shoot!" }
local BARK_KIND = { "huh", "hey", "investigate", "lost", "found_body", "radio", "backup", "surrender" }   -- numeric kinds, 1-based + 0

function heist_ar.attach(H, S)
  if not ar or not diag then return end
  if mode.env_flag("GE_AR_OFF") then return end   -- not even the handlers: the presentation proof compares the sim with and without them
  local layers = S.ar_layers or {}
  local huh = {}                  -- chr -> true once his "Huh?" played (reset when his meter drops)
  local function say(chr, text, secs, kind)
    if chr and chr >= 0 and text then ar.say(chr, text, secs or 3, kind) end
  end
  local function up(chr) return spawn.alive(chr) and not spawn.down(chr) end
  local function pos(pad) if pad then return pads.pos(pad) end end

  local points = {}               -- label -> { x, y, z, rgba }
  local function point(label, x, y, z, rgba)
    if x then points[label] = { x, y, z, rgba } end
  end

  local function labels()
    -- rooms: name (cover class), for the diagnostics panel
    for name, r in pairs(LEVEL.rooms or {}) do
      if r.id then diag.room(r.id, string.format("%s %s/%s", name, tostring(r.area or "-"), tostring((S.area_class or {})[r.area] or r.zone or "public"))) end
    end
    -- door rules, as the mode sets them (groups, then the layout's own tags win)
    local function rule_text(rule)
      if type(rule) == "table" then return "role:" .. table.concat(rule, ",") end
      return tostring(rule)
    end
    local function rule_col(t) return t == "any" and 0x60FF60FF or (t == "never" and 0xFF4040FF or 0xFFC040FF) end
    local D = H.M and H.M.DOORS or {}
    for group, rule in pairs(S.door_access or {}) do
      local list = D[group]
      if type(list) == "number" then list = { list } end
      local t = rule_text(rule)
      for _, d in ipairs(list or {}) do diag.door(d, group .. ":" .. t, rule_col(t)) end
    end
    for name, o in pairs(LEVEL.objects or {}) do
      if o.guard_access and (o.type == "door" or o.type == "safe") then
        diag.door(o.index, name .. ":" .. o.guard_access, rule_col(o.guard_access))
        if o.pair then diag.door(o.pair, name .. ":" .. o.guard_access, rule_col(o.guard_access)) end
      end
    end
  end

  local function refresh_points()
    local P = H.M and H.M.P
    if not P then return end
    points = {}
    local ph = H.phase
    local vault_job = ph == "casing" or ph == "cracking" or ph == "jammed" or ph == "recracking" or ph == "sealed" or ph == "trapped"
    if vault_job or ph == "open" or ph == "closing" then
      local x, y, z = pos(P.crack)
      point((ph == "open" or ph == "closing") and "GOLD" or "VAULT", x, y, z, 0xFFD040FF)
    end
    if P.breaker and not H.power.out and not H.hot and ph == "casing" then
      local x, y, z = pos(P.power_to)
      point("BREAKER", x or P.breaker[1], y or 0, z or P.breaker[2], 0x60E8FFFF)
    end
    local going = (H.loot or 0) > 0 or H.hot or ph == "open" or ph == "closing" or ph == "escaped"
    local ax, ay, az = pos(P.exit_a)
    local bx, by, bz = pos(P.exit_b)
    point("VAN", ax, ay, az, going and 0x50FF70FF or 0x50FF7060)
    point("DOCK", bx, by, bz, going and 0x50FF70FF or 0x50FF7060)
    ar.unpoint()
    for label, p in pairs(points) do ar.point(p[1], p[2], p[3], label, p[4]) end
    for label, p in pairs(heist_ar.warn or {}) do ar.point(p[1], p[2], p[3], label, 0xFF3838FF) end
  end

  -- The camera warning. Searching a body or picking a pocket in a live camera's view is an act the camera reports (port_ai.c), and the
  -- security room raises the alarm. A Bond within reach of an unsearched knocked-out body, or of a standing card carrier, while a
  -- working camera sees him (on its floor, not off, not on a tape loop, someone in the security room) gets a red "CAMERA SEES YOU"
  -- diamond on that camera. Presentation only: the points layer, so Shift+F3 hides it with the rest of the specs.
  local searched, bodies = {}, {}   -- (bodies: every knocked-out chr, from the event; a search needs a knocked-out one)
  on("chr_searched", function(c) searched[c] = true end)
  on("chr_knocked_out", function(c) bodies[c] = true end)
  on("chr_woken", function(c) bodies[c] = nil end)
  on("stage_start", function() searched, bodies = {}, {}; heist_ar.warn = {} end)
  heist_ar.warn = {}
  local function cam_live(cam)
    local g = cctv.get(cam)
    if not g or g.disabled then return nil end
    if tostring(cctv.name(cam) or ""):find("[LOOP]", 1, true) then return nil end   -- (the gadgets' tape loop renames the feed)
    return g
  end
  local function about_to_act(p)
    local x, y, z = bond.pos(p)
    if not x or bond.dead(p) then return false end
    local function near(c, r)
      local cx, cz = spawn.where(c)
      return cx and (cx - x) ^ 2 + (cz - z) ^ 2 <= r * r
    end
    for c in pairs(bodies) do
      if not searched[c] and spawn.alive(c) and ko.is(c) and near(c, 170) then return true end
    end
    for _, cc in pairs(H.carriers or {}) do
      if cc.chr and up(cc.chr) and near(cc.chr, 150) then return true end
    end
    return false
  end
  local function camera_warn()
    local w = {}
    if S.cover and not H.hot and not alarm.active() and (not H.security_up or H.security_up() > 0) then
      local n = bond.count()
      for p = 0, n - 1 do
        if about_to_act(p) then
          for _, cam in ipairs(cctv.list()) do
            local g = cam_live(cam)
            if g and cctv.sees(cam, bond.id(p)) then
              w[n > 1 and ("CAMERA SEES P" .. (p + 1)) or "CAMERA SEES YOU"] = { g.x, g.y, g.z }
              break
            end
          end
        end
      end
    end
    for label in pairs(heist_ar.warn) do if not w[label] then ar.unpoint(label) end end
    for label, p in pairs(w) do
      local o = heist_ar.warn[label]
      if not o or o[1] ~= p[1] or o[3] ~= p[3] then ar.point(p[1], p[2], p[3], label, 0xFF3838FF) end
    end
    heist_ar.warn = w
  end

  on("stage_start", function()
    ar.reset()
    if H.phase == "off" then ar.on(false); return end
    ar.on(S.ar ~= false)   -- (GE_AR_OFF=1 / GE_DIAG=1: tests/heist/run_ar.sh's presentation proof)
    for name, v in pairs(layers) do pcall(ar.layer, name, v and true or false) end
    if mode.env_flag("GE_DIAG") then diag.on(); ar.layer("radar", true) end
    local ct = S.cover_tune or {}
    ar.config{ range = ct.range or 1800, cone = ct.cone or 75 }
    labels()
    refresh_points()
  end)

  -- the mode's barks, as bubbles over the guard who says them
  on("cover_blown", function(g, p) say(g, "HEY!", 3, "hey") end)
  on("cover_report", function(g, p) say(g, H.power and H.power.out and "Alarm! ...it's dead" or "Control! Intruder!", 3.5, "radio") end)
  on("body_found", function(g, b, k) say(g, k == 1 and "Man down! Wake up!" or "Body! Raise the alarm!", 3.5, "hey") end)
  on("civilian_surrendered", function(c) say(c, "Don't shoot!", 2.5, "huh") end)
  on("civilian_robbed", function(c) say(c, "Take it! Take it!", 2.5, "huh") end)
  on("chr_woken", function(c) say(c, "Ugh... my head", 2.5, "huh") end)
  on("cctv_spotted", function(cam)
    if not S.cover or H.hot then return end
    for _, g in ipairs(H.guards) do   -- the security room's relay (mode_heist.lua plays its radio at the same guard)
      if g.area == "security" and up(g.chr) then say(g.chr, "Camera " .. tostring(cam) .. ": we've got someone", 3.5, "radio"); break end
    end
  end)
  on("alarm", function(a)
    if a ~= 1 then return end
    for _, g in ipairs(H.guards) do
      if up(g.chr) and g.kind == "raiser" then say(g.chr, "ALARM!", 2.5, "hey"); break end
    end
  end)
  -- the ai stream's events (optional until it merges)
  pcall(on, "guard_bark", function(chr, kind, text)
    if type(kind) == "number" then kind = BARK_KIND[kind + 1] or "huh" end
    say(chr, (type(text) == "string" and text ~= "") and text or BARK[kind] or "...", 3, kind)
  end)
  if S.noise_rings then
    pcall(on, "noise", function(x, y, z, radius, kind) if x and y and z then ar.noise(x, y, z, radius or 300, kind) end end)
  end

  local acc, wacc = 0, 0
  on("tick", function(dt)
    if H.phase == "off" or not H.ready then return end
    acc = acc + dt
    wacc = wacc + dt
    if wacc >= 0.25 then wacc = 0; camera_warn() end
    -- "Huh?" as a guard's meter passes half way (read-only: cover.state)
    if S.cover and not cover.alert() then
      for _, g in ipairs(H.guards) do
        local st = up(g.chr) and cover.state(g.chr) or nil
        if st then
          if st.susp >= 0.5 and st.witness == 0 and not huh[g.chr] then huh[g.chr] = true; say(g.chr, "Huh?", 2, "huh")
          elseif st.susp < 0.1 then huh[g.chr] = nil end
        end
      end
    end
    if acc >= 1 then
      acc = 0
      refresh_points()
      if diag.enabled() then
        diag.text(1, string.format("heist %s  hot %s  loot %d  bars %d  prog %.0f%%", H.phase, tostring(H.hot), H.loot or 0, H.bars or 0, (H.progress or 0) * 100))
        diag.text(2, string.format("guards %d  civs %d  blown %d  reports %d  power %s", #H.guards, #H.civs, H.blown or 0, H.reports or 0,
                                   H.power and (H.power.out and "OUT" or "on") or "-"))
      end
    end
  end)
end
