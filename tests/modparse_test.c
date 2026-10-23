/* Unit test for the modifier text helpers (port/src/port_modparse.c). */
#include <stdio.h>
#include <string.h>
#include "port_modparse.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    int v = -99;
    char name[24], val[16];
    const char *p;

    CHECK(portModParseValue("on", &v) && v == 1);
    CHECK(portModParseValue("OFF", &v) && v == 0);
    CHECK(portModParseValue("True", &v) && v == 1);
    CHECK(portModParseValue("no", &v) && v == 0);
    CHECK(portModParseValue("150", &v) && v == 150);
    CHECK(portModParseValue("-25", &v) && v == -25);
    CHECK(portModParseValue("+7", &v) && v == 7);
    CHECK(!portModParseValue("", &v));
    CHECK(!portModParseValue("onn", &v));
    CHECK(!portModParseValue("12x", &v));
    CHECK(!portModParseValue("-", &v));
    CHECK(!portModParseValue(NULL, &v));
    CHECK(portModParseValue("99999999999", &v) && v == 1000000);

    p = " Enemy_Knives , enemy_damage = 150,,=3, one_hit_kills=off ,x=";
    CHECK(portModListNext(&p, name, sizeof(name), val, sizeof(val)) && strcmp(name, "enemy_knives") == 0 && val[0] == '\0');
    CHECK(portModListNext(&p, name, sizeof(name), val, sizeof(val)) && strcmp(name, "enemy_damage") == 0 && strcmp(val, "150") == 0);
    CHECK(portModListNext(&p, name, sizeof(name), val, sizeof(val)) && strcmp(name, "one_hit_kills") == 0 && strcmp(val, "off") == 0);
    CHECK(portModListNext(&p, name, sizeof(name), val, sizeof(val)) && strcmp(name, "x") == 0 && val[0] == '\0');
    CHECK(!portModListNext(&p, name, sizeof(name), val, sizeof(val)));
    CHECK(!portModListNext(&p, name, sizeof(name), val, sizeof(val)));   /* stays at the end */

    p = "";
    CHECK(!portModListNext(&p, name, sizeof(name), val, sizeof(val)));
    p = " , ,";
    CHECK(!portModListNext(&p, name, sizeof(name), val, sizeof(val)));
    p = "averyveryverylongmodifiername=1";
    CHECK(portModListNext(&p, name, 8, val, sizeof(val)) && strcmp(name, "averyve") == 0 && strcmp(val, "1") == 0);

    CHECK(portModClamp(100, 25, 400, 25) == 100);
    CHECK(portModClamp(110, 25, 400, 25) == 100);   /* 85 above 25 snaps down to 75 */
    CHECK(portModClamp(113, 25, 400, 25) == 125);   /* 88 above 25 snaps up to 100 */
    CHECK(portModClamp(1, 25, 400, 25) == 25);
    CHECK(portModClamp(9999, 25, 400, 25) == 400);
    CHECK(portModClamp(399, 25, 400, 25) == 400);
    CHECK(portModClamp(5, 0, 1, 1) == 1);
    CHECK(portModClamp(-3, 0, 1, 1) == 0);
    CHECK(portModClamp(7, 0, 10, 0) == 7);

    CHECK(portModEffective(100, 0, 300) == 100);   /* no override: the saved value stands */
    CHECK(portModEffective(100, 1, 300) == 300);   /* an override wins */
    CHECK(portModEffective(150, 1, 100) == 100);   /* even when it is the default */
    CHECK(portModEffective(0, 1, 0) == 0);

    printf("modparse_test: %s (%d failed)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
