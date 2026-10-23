-- Job 3 harness (not shipped): run with GE_LUA_TEST=<this file>. Logs MODTEST lines.
-- Turns Bond's invincibility off a few seconds in (the arena switches it on), then logs every
-- point of damage Bond takes with its attacker, and the modifier list once.
local t, done = 0, false
on("tick", function(dt)
  t = t + dt
  if not done and t > 3 then
    done = true
    print("MODTEST list:")
    for _, m in ipairs(mods.list()) do
      print(("MODTEST  %s kind=%s value=%s default=%s"):format(m.name, m.kind, tostring(m.value), tostring(m.default)))
    end
    print("MODTEST get enemy_knives = " .. tostring(mods.get("enemy_knives")))
    print("MODTEST set invincible false -> " .. tostring(mods.set("invincible", false)))
    print("MODTEST get invincible = " .. tostring(mods.get("invincible")))
    print("MODTEST bad name: " .. tostring(select(2, pcall(mods.get, "nope"))))
  end
end)
on("damage", function(victim, attacker, amount, weapon)
  if victim == CHR_BOND then
    print(("MODTEST bond damage attacker=%d amount=%.3f t=%.1f"):format(attacker, amount, t))
  end
end)
