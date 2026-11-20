#ifndef ISAAC_PLAY_H
#define ISAAC_PLAY_H

#include "types.h"

typedef struct {
    uint8_t up, down, left, right;          /* movement: 4 / 8 / 6 / 2 */
    uint8_t f_up, f_down, f_left, f_right;   /* directional fire: arrows */
    uint8_t f_center;                        /* [5]: fire in the aimed direction */
    uint8_t bomb;       /* alpha or [+] (edge) */
    uint8_t map;        /* mode (edge) */
    uint8_t quit;       /* clear/del (edge) */
    uint8_t active;     /* [stats]: active item (edge) */
} Input;

enum {
    EV_NONE = 0,
    EV_TRANSIT,        /* *dir = DIR_* */
    EV_NEXT_FLOOR,
    EV_WIN,
    EV_DEAD,
    EV_TELE            /* active item: random teleport */
};

void    play_reset_isaac(Isaac *iz);
void    play_start_floor(Game *g, Isaac *iz);
void    play_enter_room(Game *g, Isaac *iz, int8_t from_dir);
uint8_t play_update(Game *g, Isaac *iz, const Input *in, uint8_t *dir);
void    play_apply_item(Isaac *iz, uint8_t item);
void    play_draw(const Game *g, const Isaac *iz);
void    play_draw_hud(const Game *g, const Isaac *iz);
uint8_t play_diag_solid(const Game *g, uint8_t room, uint8_t tx, uint8_t ty);
const char *play_item_name(uint8_t item);
void    play_notify(const char *txt, const char *desc);
uint8_t play_tele_charge(void);

#endif
