/* port_traits.h - guard personalities for the heist (designs/bank-heist.md section 32; port/src/port_traits.c). Each tracked guard may
 * carry one trait (dealt by tools/heist_traits.lua from the sim RNG). The ai and combat layers read their per-guard multipliers here;
 * with the traits off (or a guard with none) every value is the neutral one (1 for a multiplier, 0 for an addition), so the stock layers
 * run bit for bit as before. Other streams may call these through weak declarations. */
#ifndef PORT_TRAITS_H
#define PORT_TRAITS_H

enum
{
    TRAIT_NONE, TRAIT_ROOKIE, TRAIT_VETERAN, TRAIT_BOOK, TRAIT_BRIBABLE, TRAIT_N
};

/* the per-guard values (portTraitVal) */
enum
{
    TV_HEAR,       /* x a heard noise's awareness gain (ai) */
    TV_SUSP,       /* x the cover meter's fill rate (port_heist.c coverScan); 0 = he looks away (bribed) */
    TV_TURN,       /* x the stop-and-turn time before he walks over (ai) */
    TV_CHECKIN,    /* x the time before his own missed radio check-in is noticed when he is down (ai) */
    TV_RETREAT,    /* x combat's retreat_hp (combat): a rookie falls back sooner, a veteran later */
    TV_SURRENDER,  /* x how easily he gives up at gunpoint (for the hostages stream; > 1 easier) */
    TV_PADS,       /* + search pads he checks round a noise (ai; by-the-book) */
    TV_BACKUP,     /* + guards he calls when he goes into combat (ai; by-the-book) */
    TV_FULL,       /* 1: every noise he notices gets a full search, never a look from where he stands (by-the-book) */
    TV_N
};

int portTraitOn(void);
int portTraitOf(int chrnum);                          /* TRAIT_* (TRAIT_NONE when off or untraited) */
float portTraitVal(int chrnum, int field);            /* TV_*: neutral when off */
const char *portTraitText(int chrnum, int barkKind, const char *text);   /* a bark's line in his own voice (or text) */
void portTraitBarkSfx(int chrnum, int barkKind, float vol, float pan);   /* presentation: his own extra ROM sound */
int portTraitDeaf(int chrnum, int noiseKind);         /* 1: he ignores this noise (bribed: he looks away) */
void portTraitSuspicious(int chrnum, float x, float y, float z);        /* ai: he just turned to a noise ("Huh?") */
int portTraitSkipPad(int chrnum, int pad);            /* ai patrol: 1 = he skips this watchclock station this lap */

#endif
