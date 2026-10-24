#include <ultra64.h>

#ifndef TARGET_N64
extern int portAllPlayerCount(void);   /* port_coop.c (as game/player.h, which this file does not include) */
#else
#define portAllPlayerCount() getPlayerCount()
#endif

void set_favorite_weapon_for_every_player(void) {
    int right_hand;
    int left_hand;
    int player_count;
    int current_player;
    int player;

    player_count = portAllPlayerCount();
    current_player = get_cur_playernum();

    for (player = 0; player < player_count; player++) {
        set_cur_player(player);
        bondinvGetWeaponOfChoice(&right_hand, &left_hand);
        store_favorite_weapon_current_player(right_hand, left_hand);
    }

    set_cur_player(current_player);
}

