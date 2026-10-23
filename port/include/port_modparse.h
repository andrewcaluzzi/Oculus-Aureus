/* port_modparse.h - pure text helpers for the modifier framework (no game types, so the
 * logic tests build it alone). See port_modifier.h. */
#ifndef PORT_MODPARSE_H
#define PORT_MODPARSE_H

/* "on"/"true"/"yes" -> 1, "off"/"false"/"no" -> 0, else a decimal integer (optional sign).
 * Returns 1 and sets *out, or 0 when the text is none of those. */
int portModParseValue(const char *text, int *out);

/* Reads the next item of "name,name=value,..." at *pp and advances past its comma. Names are
 * lowercased, blanks around names and values are dropped, empty items are skipped. val is ""
 * when the item has no '='. Returns 1 for an item, 0 at the end of the text. */
int portModListNext(const char **pp, char *name, int nameCap, char *val, int valCap);

/* Clamps v into [min, max] and snaps it to the nearest min + k*step (step >= 1). */
int portModClamp(int v, int min, int max, int step);

/* Two layers: the saved value (ini, console, menu) and a per-stage override (Lua, GE_MODS).
 * The effective value is the override when there is one, else the saved value. */
int portModEffective(int saved, int hasOverride, int override_);

#endif /* PORT_MODPARSE_H */
