-- Project Midas mod script example (Lua 5.5). Every .lua file in Data/Scripts runs, in
-- alphabetical order, each time a mission starts (solo only; never in demos or multiplayer).
-- Weapon edits last until the next mission start, which restores stock first. Everything
-- below is commented out, so by default nothing changes. Console: `lua <code>`, `lua_reload`.
--
-- weapon.set(item, field, value)  -> true, or an error on a bad item/field/value
-- weapon.get(item, field)         -> the current value
-- weapon.reset()                  -> all weapons back to stock
-- weapon.items(), weapon.fields() -> lists of valid names
--
-- Items are the ITEM_ names lowercased without the prefix (ak47, wppk, goldengun,
-- throwknife, ...), plus pp7, pp7sil, dd44, klobb, kf7, zmg, ar33, rcp90.
-- Fields (integer): magsize, firerate, singlerate, penetration, sound, ammotype
-- Fields (number):  damage, inaccuracy, zoom, crosshairspeed, aimlockspeed, sway,
--                   recoilback, recoilup, boltrecoil, loudnessmin, loudnessmax,
--                   noiseshot, impact

-- Example 1: a bigger KF7 magazine.
-- weapon.set("kf7", "magsize", 99)

-- Example 2: a silent, no-recoil PP7.
-- weapon.set("pp7", "recoilup", 0)
-- weapon.set("pp7", "loudnessmax", 0)

-- Example 3: make the golden gun less deadly.
-- weapon.set("goldengun", "damage", 10.0)

-- Example 4: print a stat to the console and log.
-- print("kf7 magsize", weapon.get("kf7", "magsize"))

-- Modifiers: mods.get(name), mods.set(name, v), mods.list().
-- Toggles take true/false, the percent ones take a number (enemy_damage, enemy_health: 25..400).
-- A script's change is not written to midas.ini; cheat-backed ones apply about a second in.
-- mods.set("enemy_knives", true)
-- mods.set("enemy_damage", 150)

-- Example 5: give every gun perfect accuracy.
-- for _, name in ipairs(weapon.items()) do
--     pcall(weapon.set, name, "inaccuracy", 0)
-- end
