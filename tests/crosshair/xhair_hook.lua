-- tests/crosshair/run.sh proof: a draw_crosshair hook over the player's style (GE_LUA_TEST). It reads the frame info and draws a
-- spread ring and a tick under the aim point; it must leave the sim untouched (the run compares per-tick state hashes).
local said = false
on("draw_crosshair", function(x, y, ctx)
  ctx.circle(x, y, 6 + ctx.spread, 0.4, 0xFFFFFF60)
  ctx:line(x, y + 8, x, y + 10, 0.5, "yellow")
  if ctx.since_hit and ctx.since_hit < 0.3 then ctx.rect(x - 1, y - 12, x + 1, y - 10, "red") end
  if not said then
    said = true
    print(string.format("XHAIR HOOK drew at %.1f,%.1f spread %.2f weapon %d", x, y, ctx.spread, ctx.weapon))
  end
end)
