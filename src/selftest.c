/* selftest.c — embedded self-tests (RNG, generation, collisions). */
#include <graphx.h>
#include <keypadc.h>
#include <string.h>

#include "selftest.h"
#include "types.h"
#include "gen.h"
#include "rng.h"
#include "play.h"
#include "save.h"
#include "palette.h"

static Game tg;

static uint32_t rng_checksum(uint32_t seed)
{
    uint32_t sum = 0;
    uint16_t i;
    rng_seed(seed);
    for (i = 0; i < 256; i++) sum += rng_next();
    return sum;
}

/* 0 = OK; codes 3..7, 13..15 = invalid */
static uint8_t check_floor(const Game *g)
{
    const RoomState *start = 0, *boss = 0, *treas = 0,
                   *shop = 0, *secret = 0, *devil = 0, *sacri = 0;
    uint8_t nb = 0, i, j;

    for (i = 0; i < MAP_W * MAP_H; i++) {
        const RoomState *r = &g->rooms[i];
        if (r->kind == RM_NONE) continue;
        nb++;
        if (r->kind == RM_START)         start = r;
        else if (r->kind == RM_BOSS)     boss = r;
        else if (r->kind == RM_TREASURE) treas = r;
        else if (r->kind == RM_SHOP)     shop = r;
        else if (r->kind == RM_SECRET)   secret = r;
        else if (r->kind == RM_DEVIL)    devil = r;
        else if (r->kind == RM_SACRI)    sacri = r;
        if (r->n_obst > MAX_OBST) return 7;
        if (r->n_pick > MAX_PICKUPS) return 15;
        for (j = 0; j < r->n_obst; j++)
            if (r->obst[j].tx >= ROOM_W_TILES ||
                r->obst[j].ty >= ROOM_H_TILES) return 7;
    }

    if (nb != g->n_rooms || nb < 6 || nb > 13) return 3;
    if (!start || !boss || start == boss) return 4;
    if (!treas || treas == start || treas == boss) return 5;
    if (!shop || shop == start || shop == boss || shop == treas) return 13;
    if (!secret) return 14;
    if (devil && boss) {
        /* deal locked at start, room glued to the boss */
        uint8_t cx, cy, dx1 = 0, dy1 = 0, bx = 0, by = 0, found = 0;
        if (devil->flags & 4) return 18;
        for (cy = 0; cy < MAP_H; cy++)
            for (cx = 0; cx < MAP_W; cx++) {
                if (g->grid[cy][cx] == (uint8_t)(devil - g->rooms) + 1) {
                    dx1 = cx; dy1 = cy; found++;
                }
                if (g->grid[cy][cx] == (uint8_t)(boss - g->rooms) + 1) {
                    bx = cx; by = cy; found++;
                }
            }
        if (found != 2) return 18;
        cx = (uint8_t)((dx1 > bx) ? dx1 - bx : bx - dx1);
        cy = (uint8_t)((dy1 > by) ? dy1 - by : by - dy1);
        if ((uint8_t)(cx + cy) != 1) return 18;
    }
    if (sacri) {
        /* the sacrifice spike is in the center of the room */
        uint8_t j2, ok = 0;
        for (j2 = 0; j2 < sacri->n_obst; j2++)
            if (sacri->obst[j2].type == OB_SPIKE
                && sacri->obst[j2].tx == 8 && sacri->obst[j2].ty == 6)
                ok = 1;
        if (!ok) return 19;
    }

    {   /* connectivity: everything reachable from the start */
        uint8_t cx, cy, found = 0;
        uint8_t si = (uint8_t)(start - g->rooms);
        for (cy = 0; cy < MAP_H && !found; cy++)
            for (cx = 0; cx < MAP_W && !found; cx++)
                if (g->grid[cy][cx] == si + 1) {
                    found = 1;
                    if (gen_reachable(g, cx, cy) != nb) return 6;
                }
        if (!found) return 6;
    }
    return 0;
}

static uint8_t check_collision(void)
{
    memset(&tg, 0, sizeof tg);
    tg.rooms[0].kind = RM_NORMAL;
    tg.rooms[0].n_obst = 1;
    tg.rooms[0].obst[0].type = OB_ROCK;
    tg.rooms[0].obst[0].tx = 5;
    tg.rooms[0].obst[0].ty = 5;

    if (!play_diag_solid(&tg, 0, 5, 5)) return 8;
    if (play_diag_solid(&tg, 0, 5, 4)) return 8;
    if (play_diag_solid(&tg, 0, 4, 5)) return 8;
    tg.rooms[0].obst[0].type = OB_SPIKE;
    if (play_diag_solid(&tg, 0, 5, 5)) return 8;
    return 0;
}

/* door passage: free, refused without a key, the key opens the treasure room */
static uint8_t check_transit(void)
{
    Isaac ti;
    Input in;
    uint8_t dir = 0, f;

    memset(&in, 0, sizeof in);
    in.up = 1;

    /* case 1: north door to a normal room -> transit expected */
    memset(&tg, 0, sizeof tg);
    tg.grid[2][2] = 1;  tg.rooms[0].kind = RM_START;
    tg.grid[1][2] = 2;  tg.rooms[1].kind = RM_NORMAL;
    tg.n_rooms = 2;     tg.cur = 0; tg.floor = 1;
    play_reset_isaac(&ti);
    play_enter_room(&tg, &ti, -1);
    ti.y = 12;
    for (f = 0; f < 40; f++) {
        uint8_t ev = play_update(&tg, &ti, &in, &dir);
        if (ev == EV_TRANSIT && dir == DIR_N) break;
        if (ev != EV_NONE) return 10;
    }
    if (f >= 40) return 10;

    /* case 2: north door to the treasure room, no key -> blocked at the threshold */
    memset(&tg, 0, sizeof tg);
    tg.grid[2][2] = 1;  tg.rooms[0].kind = RM_START;
    tg.grid[1][2] = 2;  tg.rooms[1].kind = RM_TREASURE;
    tg.n_rooms = 2;     tg.cur = 0; tg.floor = 1;
    play_enter_room(&tg, &ti, -1);
    ti.y = 12;
    for (f = 0; f < 40; f++)
        if (play_update(&tg, &ti, &in, &dir) != EV_NONE) return 11;
    if (ti.y >= 8 || ti.keys) return 11;

    /* case 3: one key opens the treasure room and is consumed */
    ti.keys = 1;
    for (f = 0; f < 40; f++) {
        uint8_t ev = play_update(&tg, &ti, &in, &dir);
        if (ev == EV_TRANSIT && dir == DIR_N) break;
        if (ev != EV_NONE) return 12;
    }
    if (f >= 40 || ti.keys != 0 || !(tg.rooms[1].flags & 4)) return 12;

    return 0;
}

static void show_result(uint8_t fails, uint8_t ntests, uint8_t code, uint32_t seed)
{
    gfx_FillScreen(COL_BLACK);
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_SetTextScale(2, 2);
    gfx_PrintStringXY("SELFTEST", 112, 56);
    gfx_SetTextScale(1, 1);
    if (!fails) {
        gfx_SetTextFGColor(COL_GREEN);
        gfx_PrintStringXY("TEST OK ", 108, 112);
        gfx_PrintUInt(ntests, 2);
        gfx_PrintString(" / ");
        gfx_PrintUInt(ntests, 2);
    } else {
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("TEST KO  code=", 68, 112);
        gfx_PrintUInt(code, 2);
        gfx_PrintString(" seed=");
        gfx_PrintUInt(seed, 4);
    }
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("ENTER: CONTINUE", 100, 196);
    gfx_SwapDraw();

    for (;;) {
        kb_Scan();
        if (kb_Data[6] & kb_Enter) break;
    }
    while (kb_Data[6] & kb_Enter) kb_Scan();
}

uint8_t selftest_run(void)
{
    uint8_t ntests = 0, fails = 0, code = 0;
    uint32_t fseed = 0;
    uint32_t s;
    uint8_t c;

    /* deterministic RNG for the same seed */
    ntests++;
    if (rng_checksum(12345u) != rng_checksum(12345u)) { fails++; code = 1; }
    /* different RNG for two different seeds */
    ntests++;
    if (rng_checksum(1u) == rng_checksum(2u)) { fails++; code = 2; }

    /* floor 1 generation: 64 seeds */
    for (s = 1; s <= 64; s++) {
        rng_seed(s);
        gen_floor(&tg, 1);
        if (tg.floor != 1) { fails++; code = 9; fseed = s; }
        ntests++;
        if ((c = check_floor(&tg))) { fails++; code = c; fseed = s; }
    }
    /* floor 2 generation: 16 seeds */
    for (s = 65; s <= 80; s++) {
        rng_seed(s);
        gen_floor(&tg, 2);
        if (tg.floor != 2) { fails++; code = 9; fseed = s; }
        ntests++;
        if ((c = check_floor(&tg))) { fails++; code = c; fseed = s; }
    }
    /* diagonal collisions on a fake rock */
    ntests++;
    if ((c = check_collision())) { fails++; code = c; }

    /* door passage: free / refused without key / key opens the treasure room */
    ntests++;
    if ((c = check_transit())) { fails++; code = c; }

    /* save: pack then unpack restores the run (round-trip) */
    {
        SaveSlot ss; Game g2; Isaac i2;
        uint8_t ok;
        rng_seed(81u);
        gen_floor(&tg, 2);
        memset(&g2, 0, sizeof g2);
        memset(&i2, 0xAA, sizeof i2);
        play_reset_isaac(&i2);
        i2.coins = 21;
        tg.kills = 77;
        tg.dmg_floor = 5;
        tg.dmg_run = 9;
        tg.rooms[0].count = 7;
        /* meta managed by main: pack must not overwrite them */
        ss.unlocked = CHAR_UNLOCK_MAGDA;
        ss.wins_acc = 4;
        ss.kills_acc = 60;
        ss.in_progress = 0;
        ss.char_id = CHAR_ISAAC;
        save_pack(&ss, &tg, &i2, 999);
        ok = (uint8_t)((ss.magic == SAVE_MAGIC) && !ss.in_progress
             && ss.run_time == 999 && ss.n_rooms == tg.n_rooms
             && ss.unlocked == CHAR_UNLOCK_MAGDA
             && ss.wins_acc == 4 && ss.kills_acc == 60
             && ss.kills_run == 77);
        save_unpack(&g2, &i2, &ss);
        ok &= (uint8_t)((g2.n_rooms == tg.n_rooms) && (g2.floor == tg.floor)
             && (g2.cur == tg.cur) && (g2.kills == 77) && (i2.coins == 21)
             && (g2.dmg_floor == 5) && (g2.dmg_run == 9)
             && (g2.rooms[0].count == 7)
             && (g2.item == tg.item) && (g2.item2 == tg.item2)
             && memcmp(g2.grid, tg.grid, sizeof g2.grid) == 0
             && memcmp(g2.rooms, tg.rooms,
                       (size_t)g2.n_rooms * sizeof(RoomState)) == 0);
        ntests++;
        if (!ok) { fails++; code = 16; }
        ntests++;
        if (sizeof(SaveSlot) > 2048) { fails++; code = 17; }
    }

    show_result(fails, ntests, code, fseed);
    return fails;
}
