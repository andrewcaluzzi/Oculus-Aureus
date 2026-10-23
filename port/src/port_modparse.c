/* port_modparse.c - pure text helpers for the modifier framework (see port_modparse.h). */
#include <stddef.h>
#include "port_modparse.h"

static char lowerc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int isblank_(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int wordEq(const char *s, const char *w)
{
    while (*w != '\0')
    {
        if (lowerc(*s) != *w)
        {
            return 0;
        }
        s++;
        w++;
    }
    return *s == '\0';
}

int portModParseValue(const char *text, int *out)
{
    long n = 0;
    int neg = 0;

    if (text == NULL || text[0] == '\0')
    {
        return 0;
    }
    if (wordEq(text, "on") || wordEq(text, "true") || wordEq(text, "yes"))
    {
        *out = 1;
        return 1;
    }
    if (wordEq(text, "off") || wordEq(text, "false") || wordEq(text, "no"))
    {
        *out = 0;
        return 1;
    }
    if (*text == '-' || *text == '+')
    {
        neg = (*text == '-');
        text++;
    }
    if (*text < '0' || *text > '9')
    {
        return 0;
    }
    while (*text >= '0' && *text <= '9')
    {
        n = n * 10 + (*text - '0');
        if (n > 1000000)
        {
            n = 1000000;   /* far past any modifier range; keeps the int safe */
        }
        text++;
    }
    if (*text != '\0')
    {
        return 0;
    }
    *out = (int)(neg ? -n : n);
    return 1;
}

int portModListNext(const char **pp, char *name, int nameCap, char *val, int valCap)
{
    const char *p;

    if (pp == NULL || *pp == NULL || nameCap < 1 || valCap < 1)
    {
        return 0;
    }
    p = *pp;
    for (;;)
    {
        const char *itemEnd, *eq, *ns, *ne, *vs, *ve;
        int n, v;

        while (isblank_(*p) || *p == ',')
        {
            p++;
        }
        if (*p == '\0')
        {
            *pp = p;
            return 0;
        }
        itemEnd = p;
        eq = NULL;
        while (*itemEnd != '\0' && *itemEnd != ',')
        {
            if (*itemEnd == '=' && eq == NULL)
            {
                eq = itemEnd;
            }
            itemEnd++;
        }
        ns = p;
        ne = (eq != NULL) ? eq : itemEnd;
        while (ne > ns && isblank_(ne[-1]))
        {
            ne--;
        }
        n = 0;
        while (ns < ne && n < nameCap - 1)
        {
            name[n++] = lowerc(*ns++);
        }
        name[n] = '\0';
        v = 0;
        if (eq != NULL)
        {
            vs = eq + 1;
            ve = itemEnd;
            while (vs < ve && isblank_(*vs))
            {
                vs++;
            }
            while (ve > vs && isblank_(ve[-1]))
            {
                ve--;
            }
            while (vs < ve && v < valCap - 1)
            {
                val[v++] = *vs++;
            }
        }
        val[v] = '\0';
        *pp = (*itemEnd == ',') ? itemEnd + 1 : itemEnd;
        if (name[0] != '\0')
        {
            return 1;
        }
        p = *pp;   /* an item with no name ("=3"): skip it */
    }
}

int portModClamp(int v, int min, int max, int step)
{
    if (step < 1)
    {
        step = 1;
    }
    if (v < min)
    {
        return min;
    }
    if (v > max)
    {
        return max;
    }
    v = min + ((v - min + step / 2) / step) * step;
    return (v > max) ? max : v;
}

int portModEffective(int saved, int hasOverride, int override_)
{
    return hasOverride ? override_ : saved;
}
