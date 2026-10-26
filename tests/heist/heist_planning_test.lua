-- The heist PLANNING phase (designs/bank-heist.md section 27, tools/heist_planning.lua) on the bank with the real tools/mode_heist.lua.
-- Scenarios (tests/heist/run_planning.sh runs each in its own game):
--   GE_PLAN_OFF=1      planning off (an automated run without GE_PLANNING): today's start exactly: no plan, the default kit
--   (GE_PLANNING=1)    the plan: the clock waits and nobody moves while it is up; a buying spree never goes over the budget, selling never
--                      below nothing, the sally port is refused (not on today's job); then a scripted plan through the board's own
--                      input (plan.test_input: the sim's events) - LOADOUT: lockpicks, two coins; ENTRY: the roof (the piton gun comes
--                      with it); START - and the heist starts with exactly that kit on the roof. Then a quick job (the crack, three
--                      bars, the van): the payout is the loot + the quiet bonus - the planning cost, the debrief carries it.
--                      GE_PLAN_SHOTS=1: screenshots of LOADOUT and ENTRY ("HEIST SHOT plan_*")
--   GE_PLAN_KEYS=1     the real keys: GE_JOYPORT_AUTO next_weapon presses flip the plan's tabs (the INTEL board's page keys reach the sim)
--   GE_PLAN_DET=1      determinism: the scripted plan, then 20 s of the heist; run_planning.sh compares the hashes of two runs (solo, co-op)
-- Prints "HEIST PASS|FAIL ..." and "HEIST DONE n passed, m failed"; "HEIST DET ..." notes for the determinism compare.
--   GE_PLAN_HOLD=1     the hold (section 27, the planfix): with the pad stick (GE_DRIVE), the mouse (GE_FAKEMOUSE) and the Move keys
--                      (GE_FAKEDIGMOVE) all fed, Bond neither moves nor turns while the board is up; a mouse click on CONFIRM goes, and
--                      then the same feeds do move and turn him (the feeds are real)
-- The plan scenario also drives the board's MOUSE (plan.test_mouse: the real hit test, the click a session command into the sim) and
-- checks the tabs: LOADOUT / ENTRY / CREW only while planning, the job's five after the confirm.
local OFF, KEYS, DET, SHOTS = mode.env_flag("GE_PLAN_OFF"), mode.env_flag("GE_PLAN_KEYS"), mode.env_flag("GE_PLAN_DET"), mode.env_flag("GE_PLAN_SHOTS")
local HOLD = mode.env_flag("GE_PLAN_HOLD")
HEIST_SETTINGS = {
  test = true,
  cover = false,   -- (as the debrief test: the quick job warps through restricted rooms)
  crack_time = 12, jam_chance = 0, jam_every = 1000, vault_time = 14, klaxon_at = 5,
}
include("heist")
mods.set("invincible", true)
on("stage_start", function() mode.guards_wake(false); mode.body_check(false) end)

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1 else fail = fail + 1 end
  print(string.format("HEIST %s %s %s", ok and "PASS" or "FAIL", name, extra or ""))
end
local ended = {}
local real_win, real_fail = mission.win, mission.fail
mission.win = function(msg) ended.win = msg; return true end
mission.fail = function(msg) ended.fail = msg; return true end
local R
local real_end = HEIST.debrief_end
if real_end then HEIST.debrief_end = function(...) R = real_end(...); return R end end

local function wait(sec)
  local t = 0
  while t < sec do t = t + coroutine.yield(); bond.heal(0, 1, 0) end
end
local function ticks(n) for _ = 1, n do coroutine.yield() end end
local function wait_for(sec, fn)
  local t = 0
  while t < sec and not fn() do wait(0.25); t = t + 0.25 end
  return fn(), t
end
local function d2(ax, az, bx, bz) return math.sqrt((ax - bx) ^ 2 + (az - bz) ^ 2) end
local function has_item(p, name) return plan.has(p, name) end   -- (bond.equip would give it)
local function kit(p)   -- what Bond p carries of the shop's items (for the messages)
  local t = {}
  for _, n in ipairs({ "pp7", "pp7sil", "mp5ksil", "pitongun", "gaskeyring", "creditcard", "datathief", "bombdefuser", "camera", "doordecoder" }) do
    if plan.has(p, n) then t[#t + 1] = n end
  end
  return table.concat(t, ",")
end

local P = PLANNING
local EV = P and P.EV
local function press(mask, n)   -- n presses (queued in order, as n Bond ticks of presses), then one script tick
  for _ = 1, n or 1 do plan.test_input(0, mask) end
  ticks(1)
end
local function grab(maxn)
  local got = 0
  for _, it in ipairs(HEIST.loot_items) do
    if it.where == "vault" and spawn.item_alive(it.h) and got < maxn then
      bond.warp(0, it.pad, it.dx, it.dz)
      wait(0.5)
      if not spawn.item_alive(it.h) then got = got + 1 end
    end
  end
  return got
end

-- the scripted plan: lockpicks, two coins, the roof, go (from the LOADOUT tab, row 1)
local function scripted_plan()
  press(EV.DOWN, 5); press(EV.SELECT)           -- row 6: LOCKPICKS
  press(EV.DOWN); press(EV.SELECT, 2)           -- row 7: THROWING COINS x2
  plan.test_mouse(400, 78 + 7 * 25 + 14)        -- (the cursor hovers row 8, TAPE LOOPS: no click)
  if SHOTS then print("HEIST SHOT plan_loadout"); cctv.test_shot(); ticks(2) end
  press(EV.RIGHT)                               -- ENTRY
  press(EV.DOWN, 2); press(EV.SELECT)           -- row 3: THE ROOF (the piton gun comes with it)
  if SHOTS then print("HEIST SHOT plan_entry"); cctv.test_shot(); ticks(2) end
  press(EV.START)
  print(string.format("HEIST   confirmed: clock %.3f", HEIST.elapsed))
  return HEIST.elapsed
end

local function body_off()
  check("off: no plan", HEIST.plan == nil and PLANNING == nil, tostring(HEIST.plan))
  local ready = wait_for(20, function() return HEIST.ready end)
  check("off: mode ready at once", ready and HEIST.phase == "casing", HEIST.phase)
  wait(1)
  local G = GADGETS
  local me = G and G.players[0]
  check("off: the default gadgets (4 coins, 2 loops, 1 gas)", me and me.coins == 4 and me.loops == 2 and me.gas == 1,
        me and string.format("%d/%d/%d", me.coins, me.loops, me.gas) or "-")
  check("off: the PP7 and the piton gun in the kit", has_item(0, "pp7sil") and has_item(0, "pitongun") and has_item(0, "camera"))
  local x, _, z = bond.pos(0)
  local sx, _, sz = pads.pos(HEIST.M.P.start[1])
  check("off: Bond starts on the street", d2(x, z, sx, sz) < 120, string.format("%.0f units from start", d2(x, z, sx, sz)))
  check("off: the clock runs", HEIST.elapsed > 1, string.format("%.2f", HEIST.elapsed))
  check("off: nothing held", not plan.held())
end

local function body_keys()
  check("keys: the plan is up", P ~= nil and P.on)
  wait_for(4, function() return P.ready end)
  local w0 = bond.weapon(0)
  local t0 = P.tab
  wait_for(15, function() return P.tab ~= t0 end)
  wait(1.5)
  check("keys: next_weapon presses flipped the tabs (LOADOUT -> ENTRY -> CREW)", P.tab == P.base + 3, string.format("tab %d from %d", P.tab, t0))
  local open, page = intel.view()
  check("keys: the board shows the plan's tab", open and page == P.tab, string.format("open %s page %d", tostring(open), page))
  check("keys: the weapon in hand did not change (the presses went to the board)", bond.weapon(0) == w0, string.format("%s -> %s", tostring(w0), tostring(bond.weapon(0))))
end

local function body_plan()
  check("plan: the plan is up and holds", P ~= nil and P.on and plan.held())
  local x0, _, z0 = bond.pos(0)
  wait_for(20, function() return P.ready end)
  check("plan: the bank is placed (spawn queue done)", P.ready and #HEIST.guards > 0, string.format("%d guards", #HEIST.guards))
  wait(2)
  check("plan: the heist waits (not ready, phase planning)", not HEIST.ready and HEIST.phase == "planning", HEIST.phase)
  check("plan: the clock waits", math.abs(HEIST.elapsed) < 0.001, string.format("%.4f", HEIST.elapsed))
  local x1, _, z1 = bond.pos(0)
  check("plan: Bond is held", d2(x0, z0, x1, z1) < 1, string.format("%.1f units", d2(x0, z0, x1, z1)))
  local open, page = intel.view()
  check("plan: the board is up on LOADOUT", open and page == P.base + 1 and P.tab == P.base + 1, string.format("open %s page %d", tostring(open), page))
  check("plan: no kit before the plan", not has_item(0, "pp7sil") and not has_item(0, "pitongun") and has_item(0, "doordecoder"), kit(0))
  -- a spree: every row 6 presses, over and over: never over the budget
  local worst = 0
  for r = 1, #P.ITEMS do
    press(EV.SELECT, P.ITEMS[r].max == 1 and 1 or P.ITEMS[r].max + 2)   -- (a one-off item toggles: one press buys it)
    worst = math.max(worst, P.cost())
    press(EV.DOWN)
  end
  press(EV.DOWN)   -- (back to row 1: past CONFIRM)
  check("budget: a spree stops at the budget", worst <= P.budget and P.cost() <= P.budget and P.cost() > P.budget - 60000,
        string.format("spent %d of %d (worst %d)", P.cost(), P.budget, worst))
  check("budget: 'over budget' said", P.msg:find("Over budget") ~= nil or P.cost() == P.budget, P.msg)
  for r = 1, #P.ITEMS do press(EV.BACK, 7); press(EV.DOWN) end
  press(EV.DOWN)
  local neg = false
  for _, it in ipairs(P.ITEMS) do if (P.have[it.id] or 0) ~= 0 then neg = true end end
  check("budget: sold back to nothing, never below", not neg and P.cost() == 0, string.format("spent %d", P.cost()))
  -- the sally port: not on today's job
  press(EV.RIGHT); press(EV.DOWN, 4); press(EV.SELECT)
  check("entry: the sally port refused (not today's job)", P.entry == "front" and P.msg:find("today") ~= nil, P.msg)
  press(EV.DOWN, 2)   -- (row 6 CONFIRM, then row 1)
  press(EV.LEFT)      -- back to LOADOUT, row 1
  check("plan: back on LOADOUT row 1", P.tab == P.base + 1 and P.rows.loadout == 1, string.format("tab %d row %d", P.tab, P.rows.loadout))
  -- the tabs: the plan's own three only (the job's five pages come after the confirm)
  local tf, tn = plan.tabs()
  check("tabs: only LOADOUT, ENTRY, CREW while planning", tf == P.base + 1 and tn == 3, string.format("first %d, %d tabs", tf, tn))
  -- the mouse: plan.test_mouse runs the board's real hit test and sends the click as a session command (the sim applies it)
  local function ry(i) return 78 + (i - 1) * 25 + 11 end
  local pg, row = plan.test_mouse(320, ry(1))
  check("mouse: the cursor over LOADOUT row 1 (hover)", pg == P.base + 1 and row == 1, string.format("page %d row %d", pg, row))
  check("mouse: hovering buys nothing", P.cost() == 0, tostring(P.cost()))
  plan.test_mouse(320, ry(1), 1); ticks(1)
  check("mouse: a left click buys the row (the silenced PP7)", (P.have.pp7 or 0) == 1 and P.cost() == 15000, string.format("spent %d", P.cost()))
  plan.test_mouse(320, ry(7), 1); ticks(1)
  plan.test_mouse(320, ry(7), 1); ticks(1)
  check("mouse: two clicks on THROWING COINS buy two, the row follows the cursor", (P.have.coins or 0) == 2 and P.rows.loadout == 7,
        string.format("coins %d row %d", P.have.coins or 0, P.rows.loadout))
  plan.test_mouse(320, ry(7), 2); ticks(1)
  plan.test_mouse(320, ry(7), 2); ticks(1)
  plan.test_mouse(320, ry(1), 2); ticks(1)
  check("mouse: right clicks sell (back to nothing)", P.cost() == 0 and P.rows.loadout == 1, string.format("spent %d row %d", P.cost(), P.rows.loadout))
  plan.test_mouse(320, 20, 1); ticks(1)
  local _, vpage = intel.view()
  check("mouse: a click on the ENTRY tab switches to it", P.tab == P.base + 2 and vpage == P.base + 2, string.format("tab %d view %d", P.tab, vpage))
  plan.test_mouse(320, 200, 5); ticks(1)
  check("mouse: a wheel notch down moves the row", P.rows.entry == 2, "row " .. tostring(P.rows.entry))
  plan.test_mouse(320, 200, 4); ticks(1)
  plan.test_mouse(120, 20, 1); ticks(1)
  check("mouse: wheel up, then the LOADOUT tab: LOADOUT row 1 again, nothing bought", P.tab == P.base + 1 and P.rows.loadout == 1
        and P.rows.entry == 1 and P.cost() == 0 and P.entry == "front", string.format("tab %d rows %d/%d spent %d", P.tab, P.rows.loadout, P.rows.entry, P.cost()))
  local clock0 = scripted_plan()
  ticks(1)
  check("plan: confirmed, the heist starts", P.confirmed and HEIST.ready and HEIST.phase == "casing" and not plan.held(), HEIST.phase)
  check("plan: the cost (lockpicks 30k, 2 coins 10k, piton 40k, roof 25k)", P.spent == 105000 and HEIST.plan_cost == 105000, tostring(P.spent))
  local rx, _, rz = pads.pos(LEVEL.pads["roof_w"])
  local x, _, z = bond.pos(0)
  check("plan: Bond starts on the roof", d2(x, z, rx, rz) < 150 and bond.room(0) == LEVEL.rooms["roof"].id, string.format("%.0f units, room %s", d2(x, z, rx, rz), tostring(bond.room(0))))
  local me = GADGETS.players[0]
  check("plan: exactly the kit: lockpicks, piton, 2 coins", has_item(0, "gaskeyring") and has_item(0, "pitongun") and has_item(0, "creditcard")
        and me.coins == 2 and me.loops == 0 and me.gas == 0, string.format("coins %d loops %d gas %d", me.coins, me.loops, me.gas))
  check("plan: nothing else (no PP7, D5K, thermal, tape, gas)", not has_item(0, "pp7sil") and not has_item(0, "mp5ksil") and not has_item(0, "camera")
        and not has_item(0, "datathief") and not has_item(0, "bombdefuser"), kit(0))
  local open2 = intel.view()
  check("plan: the board is put away", not open2)
  local tf2, tn2 = plan.tabs()
  check("tabs: after the confirm the INTEL board has the job's five pages only", tf2 == 1 and tn2 == P.base and P.base == 5,
        string.format("first %d, %d tabs", tf2, tn2))
  intel.test_show(true, 1)
  ticks(2)
  if SHOTS then print("HEIST SHOT intel_after"); cctv.test_shot(); ticks(2) end
  intel.test_show(false)
  check("plan: the clock starts at 0 on the confirm tick", math.abs(clock0) < 0.001, string.format("%.3f, %.3f a script tick later", clock0, HEIST.elapsed))
  if bond.count() > 1 then
    local x2, _, z2 = bond.pos(1)
    check("co-op: Bond 2 on the roof too, same kit", d2(x2, z2, rx, rz) < 200 and has_item(1, "gaskeyring") and GADGETS.players[1].coins == 2)
  end
  if DET then
    print("HEIST DET confirmed " .. P.state())
    wait(20)
    print(string.format("HEIST DET after 20 s: phase %s elapsed %.2f", HEIST.phase, HEIST.elapsed))
    return
  end
  -- the quick job: the payout carries the planning cost
  local W = HEIST.M.P
  bond.warp(0, W.crack)
  wait(0.5)
  bond.test_gadget(0, "doordecoder")
  check("job: the vault opens", wait_for(30, function() return HEIST.phase == "open" end), HEIST.phase)
  check("job: bars grabbed", grab(3) >= 3, "bars " .. HEIST.bars)
  bond.warp(0, W.outside)
  wait(0.5)
  wait_for(25, function() return HEIST.phase == "sealed" end)
  wait(0.5)
  bond.warp(0, W.exit_a)
  check("job: the crew gets out (win)", wait_for(4, function() return ended.win ~= nil end), tostring(ended.win))
  local bonus = HEIST.hot and 0 or math.floor(HEIST.loot * HEIST.S.quiet_bonus)
  check("debrief: score = loot + quiet bonus - planning", HEIST.score == HEIST.loot + bonus - 105000,
        string.format("score %s, loot %d, bonus %d", tostring(HEIST.score), HEIST.loot, bonus))
  check("debrief: the planning cost in the debrief", R and R.planning == 105000 and R.payout == HEIST.score, R and string.format("planning %d payout %d", R.planning, R.payout) or "no debrief")
end

local function body_hold()
  check("hold: the plan is up and holds", P ~= nil and P.on and plan.held())
  wait_for(20, function() return P.ready end)
  wait(1)
  local x0, _, z0 = bond.pos(0)
  local yaw0, pitch0 = gadget.aim(0)
  wait(4)   -- (the pad stick, the mouse and the Move keys are all fed meanwhile)
  local x1, _, z1 = bond.pos(0)
  local yaw1, pitch1 = gadget.aim(0)
  check("hold: Bond does not move (pad stick + Move keys fed)", d2(x0, z0, x1, z1) < 0.5, string.format("%.2f units", d2(x0, z0, x1, z1)))
  check("hold: Bond does not turn or look (mouse fed)", math.abs(yaw1 - yaw0) < 0.01 and math.abs(pitch1 - pitch0) < 0.01,
        string.format("yaw %.3f -> %.3f, pitch %.3f -> %.3f", yaw0, yaw1, pitch0, pitch1))
  check("hold: still planning", P.on and not P.confirmed and HEIST.phase == "planning", HEIST.phase)
  local cy = 78 + #P.ITEMS * 25 + 2 + 12
  local pg, row = plan.test_mouse(320, cy, 1)
  check("hold: the mouse is over CONFIRM", pg == P.base + 1 and row == #P.ITEMS + 1, string.format("page %d row %d", pg, row))
  ticks(2)
  check("hold: a click on CONFIRM starts the job", P.confirmed and not plan.held() and HEIST.phase == "casing", HEIST.phase)
  wait(4)
  local x2, _, z2 = bond.pos(0)
  local yaw2 = gadget.aim(0)
  check("hold: after the confirm the same feeds move and turn Bond", d2(x1, z1, x2, z2) > 20 and math.abs(yaw2 - yaw1) > 1,
        string.format("%.0f units, yaw %.1f -> %.1f", d2(x1, z1, x2, z2), yaw1, yaw2))
end

local finished = false
local function body()
  if OFF then body_off() elseif KEYS then body_keys() elseif HOLD then body_hold() else body_plan() end
  print(string.format("HEIST DONE %d passed, %d failed", pass, fail))
  finished = true
  if ended.win then real_win(ended.win) elseif ended.fail then real_fail(ended.fail) else real_fail("planning test: done") end
end

local co = coroutine.wrap(body)
on("tick", function(dt)
  if finished then return end
  local ok, err = pcall(co, dt)
  if not ok then finished = true; print("HEIST FAIL script error: " .. tostring(err)); print(string.format("HEIST DONE %d passed, %d failed", pass, fail + 1)) end
end)
