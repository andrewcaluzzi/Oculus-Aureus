/* port_luavm_bot.c - the `bot` Lua library: a game mode feeds the player bot (port_playerbot.c) its plan.
 *
 *   bot.active()        -> 0 off, 1 driving, 2 watching
 *   bot.push(line)      -> true, or nil + message: append one plan step (the plan language of modding/bot.md)
 *   bot.clear()         drop every step not started yet (the current one too) - e.g. when the mode's situation changes
 *   bot.step()          -> index of the step being run (1-based), number of steps
 * The steps run like a plan file's. With a mode active the bot loads Data/BotPlans/<mode>.plan if there is one, else it
 * starts with no steps and waits (fighting what it sees) for bot.push. */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"

extern int  portPlayerBotModeForLua(void);
extern int  portPlayerBotPushLine(const char *line, char *err, int errlen);
extern void portPlayerBotClearSteps(void);
extern void portPlayerBotStepInfo(int *step, int *count);

static int botActive(lua_State *L)
{
    lua_pushinteger(L, portPlayerBotModeForLua());
    return 1;
}

static int botPush(lua_State *L)
{
    const char *line = luaL_checkstring(L, 1);
    char err[160];

    if (!portPlayerBotPushLine(line, err, (int) sizeof(err)))
    {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int botClear(lua_State *L)
{
    (void) L;
    portPlayerBotClearSteps();
    return 0;
}

static int botStep(lua_State *L)
{
    int step = 0, count = 0;

    portPlayerBotStepInfo(&step, &count);
    lua_pushinteger(L, step);
    lua_pushinteger(L, count);
    return 2;
}

void portLuaBotRegister(lua_State *L)
{
    static const luaL_Reg botLib[] = { { "active", botActive }, { "push", botPush }, { "clear", botClear }, { "step", botStep },
                                        { NULL, NULL } };

    luaL_newlib(L, botLib);
    lua_setglobal(L, "bot");
}
