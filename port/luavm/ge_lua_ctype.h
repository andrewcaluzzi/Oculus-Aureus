/* Force-included into every ge_lua source (see CMakeLists.txt): src/str.c defines global
 * isalpha/isspace/toupper that clash with the C runtime's at link time, so Lua uses private
 * ASCII copies (port_luavm.c) through these macros instead. */
#ifndef GE_LUA_CTYPE_H
#define GE_LUA_CTYPE_H

#include <ctype.h>

int ge_lua_isalpha(int c);
int ge_lua_isspace(int c);
int ge_lua_toupper(int c);

#undef isalpha
#undef isspace
#undef toupper
#define isalpha(c) ge_lua_isalpha(c)
#define isspace(c) ge_lua_isspace(c)
#define toupper(c) ge_lua_toupper(c)

#endif
