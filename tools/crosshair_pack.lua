-- crosshair_pack.lua: example crosshair styles (modding/lua.md "Crosshair styles").
-- Installed as Data/Scripts/mods/crosshair_pack.lua. Pick one with the console (`crosshair tactical`, `crosshair off`,
-- `crosshair list`), with GE_CROSSHAIR=<name>, or from a script: crosshair.use("tactical") (the player's choice, kept for the
-- session) or crosshair.style("tactical") (this stage only).
--
-- The file returns an array of styles; each is a crosshair.style{} table plus a `name`. Units are HUD pixels: the game's
-- 240-line frame, so 1 is 3 window pixels at 720p (before the HUD Scale setting). Colours are 0xRRGGBBAA, a name or {r, g, b, a}.
-- Edit it, add your own, and run `crosshair reload`.

return {
  -- a single dot with a dark rim: the least screen covered
  { name = "dot", shape = "dot", size = 1.4, color = 0xFFFFFFF0, outline = true, outline_thickness = 0.35, hit_marker = true },

  -- the common FPS cross: four arms that open up when you move or fire, and an X on every hit (red for a headshot)
  { name = "cross", shape = "cross", size = 3.5, gap = 1.5, thickness = 0.7, color = 0x50FF70F0, outline = true,
    dynamic = true, move_bloom = 3, fire_kick = 2, hit_marker = true },

  -- a cross with a centre dot; per weapon: a plain dot on the sniper rifle, the spread circle on the shotguns
  { name = "tactical", shape = "cross", size = 3, gap = 2, thickness = 0.6, color = "cyan", dot = 0.8, outline = true,
    dynamic = true, hit_marker = true, hit_color = "white",
    weapons = {
      sniperrifle = { shape = "dot", size = 1.2, dynamic = false },
      shotgun = { shape = "circle", size = 2, dynamic = true, spread_scale = 1, dot = 0.8 },
      autoshot = { shape = "circle", size = 2, dynamic = true, spread_scale = 1, dot = 0.8 },
    } },

  -- a ring that shows the gun's real spread (the shot lands inside it), with a centre dot
  { name = "circle", shape = "circle", size = 2, thickness = 0.5, color = 0xFFFFFFC0, dot = 0.8, outline = true,
    dynamic = true, spread_scale = 1, move_bloom = 2, fire_kick = 1.5, hit_marker = true },

  -- a chevron under the aim point (the tip is the aim point)
  { name = "chevron", shape = "chevron", size = 3, gap = 0, thickness = 0.7, color = "orange", outline = true, hit_marker = true },

  -- four corner brackets around the spread, the classic sight colour
  { name = "brackets", shape = "brackets", size = 2.5, gap = 3, thickness = 0.6, color = 0xFF3030E0, dot = 0.7,
    outline = true, dynamic = true, hit_marker = true },

  -- drawn by hand with the draw API: a range-finder with tick marks that flashes gold on a hit (solo and co-op; netplay draws nothing)
  { name = "rangefinder", shape = "none", hit_marker = false, dynamic = true,
    draw = function(x, y, ctx)
      local hit = ctx.since_hit and ctx.since_hit < 0.2
      local c = hit and "gold" or 0xE0E0E0D0
      local g = 2 + ctx.spread
      ctx.line(x - g - 8, y, x - g, y, 0.5, c)
      ctx.line(x + g, y, x + g + 8, y, 0.5, c)
      ctx.line(x, y + g, x, y + g + 6, 0.5, c)
      for i = 1, 3 do
        local ty = y + g + 1.5 * i
        ctx.line(x - 1 - i * 0.5, ty, x + 1 + i * 0.5, ty, 0.4, c)
      end
      ctx.dot(x, y, 0.8, hit and "red" or "white")
    end },
}
