-- Job 3 fix-round harness (not shipped): a top-level override must survive stage start, and be
-- reported by mods.get as the effective value; the saved layer (ini) must not see it.
mods.set("enemy_damage", 300)
print("MODTEST top-level override enemy_damage = " .. tostring(mods.get("enemy_damage")))
