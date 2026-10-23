-- Hostage rescue on Flatland, the co-op / netplay headline mode (GE_MODE=hostage-flatland).
-- 10 hostages, 20 guards (14 holding, 6 "patrol" guards that hold: Flatland has no patrol paths), win at 6 rescued,
-- lose at 5 dead. It is the stock hostage script with these settings; both machines of a session run the same text
-- (Data/Scripts/modes is part of the netplay handshake's script hash). Shipped to Data/Scripts/modes/hostage-flatland.lua.
HOSTAGE_SETTINGS = {
  hostages         = 10,
  guards_hold      = 14,
  guards_patrol    = 6,
  need             = 6,
  lose_if_dead     = 5,
  holding_min_dist = 2300,
  extract_radius   = 250,
  guard_weapon     = "ak47",
  cluster_radius   = 900,
  patrol_paths     = { 0, 1, 2, 3, 0, 1 },
}
include("hostage")
