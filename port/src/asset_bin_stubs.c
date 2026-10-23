/**
 * Zero stubs for the 12 binary-only assets with no generated C source.
 * STUBBED-WALK VERDICT (2026-08-14) - these are NOT pending work:
 *
 *  - The eight bg interiors (imp/wax/sho/rit/pam/lip/lee/ear) are
 *    AUTHENTICALLY EMPTY: the N64 repo's byte-matching ROM build emits
 *    ZERO-LENGTH .seg files for all of them (build/u/assets/obseg/bg/) -
 *    the retail US cart shipped these cut stages with no bg data. Zeros
 *    here are the faithful representation; no bin2c wave will ever bring
 *    data. (pete/jun were the exception: their generated .c lived in
 *    assets/obseg/bg/u/ - the US-region straggler subdir - and are wired
 *    in the manifest since f38bb2fc; Streets/Jungle render.)
 *    Selecting a cut stage still misbehaves (header of zeros, zero
 *    rooms) exactly as it would on hardware - do not soak them.
 *  - The four JP text banks return empty; langGet's bounds/fallback
 *    handles a zero bank on the US path, which never selects them.
 *    REAL DATA EXISTS if ever wanted: ../goldeneye_src/assets/obseg/
 *    text/LaztJ.bin + LmiscJ/LsevbJ/LstatJ.bin (no .c was generated).
 * bg stubs sized 64KB each: comfortably covers every header-window read
 * the bg loader performs before it would trust interior offsets.
 */
#include <ultra64.h>

u8 bg_imp_all_p_seg[0x10000];
u8 bg_wax_all_p_seg[0x10000];
/* bg_sho_all_p_seg is Flatland's bg now (port/flatland, CMakeLists.txt) */
u8 bg_rit_all_p_seg[0x10000];
u8 bg_pam_all_p_seg[0x10000];
u8 bg_lip_all_p_seg[0x10000];
u8 bg_lee_all_p_seg[0x10000];
/* bg_ear_all_p_seg is the Colosseum's bg now (port/colosseum, CMakeLists.txt) */

u8 LstatJ[0x1000];
u8 LsevbJ[0x1000];
u8 LmiscJ[0x1000];
u8 LaztJ[0x1000];
