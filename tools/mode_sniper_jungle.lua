-- The sniper duel on the SNIPER JUNGLE prototype from a launcher (GE_MODE=sniper-jungle GE_STAGE=levelgen GE_LEVELGEN=jungle;
-- Play Sniper Jungle.bat, a build with -DGE_LEVELGEN_EXTRA=port/levelgen_jungle -DGE_LEVELGEN_EXTRA_NAME=jungle). Every perch is 250 to
-- 355 m off, beyond the default marksman vision range (250 m): sight = 40000 so the "snipe" marksman sees Bond from the far ridge.
SNIPER_SETTINGS = { stage = "jungle", sight = 40000 }
include("sniper")
