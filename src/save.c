#include "save.h"
#include <fileioc.h>
#include <string.h>

static const char *const slot_names[3] =
    { "ISAACSV1", "ISAACSV2", "ISAACSV3" };

void save_pack(SaveSlot *s, const Game *g, const Isaac *iz, uint16_t run_time)
{
    uint8_t n = g->n_rooms;
    s->magic = SAVE_MAGIC;
    s->run_time = run_time;
    s->kills_run = g->kills;
    s->iz = *iz;
    s->floor = g->floor;
    s->cur = g->cur;
    s->item = g->item;
    s->item2 = g->item2;
    s->dmg_floor = g->dmg_floor;
    s->curse = g->curse;
    s->dmg_run = g->dmg_run;
    memcpy(s->grid, g->grid, sizeof s->grid);
    memset(s->rooms, 0, sizeof s->rooms);
    if (n > SGAMEROOMS) n = SGAMEROOMS;
    s->n_rooms = n;
    memcpy(s->rooms, g->rooms, (size_t)n * sizeof(RoomState));
}

void save_unpack(Game *g, Isaac *iz, const SaveSlot *s)
{
    uint8_t n = s->n_rooms;
    memset(g, 0, sizeof *g);
    memset(iz, 0, sizeof *iz);
    *iz = s->iz;
    g->floor = s->floor;
    g->cur = s->cur;
    g->item = s->item;
    g->item2 = s->item2;
    g->dmg_floor = s->dmg_floor;
    g->curse = (uint8_t)(s->curse & 3);
    g->dmg_run = s->dmg_run;
    g->kills = s->kills_run;
    memcpy(g->grid, s->grid, sizeof g->grid);
    memset(g->rooms, 0, sizeof g->rooms);
    if (n > SGAMEROOMS) n = SGAMEROOMS;
    g->n_rooms = n;
    memcpy(g->rooms, s->rooms, (size_t)n * sizeof(RoomState));
}

uint8_t save_read(uint8_t idx, SaveSlot *s)
{
    ti_var_t f;
    memset(s, 0, sizeof *s);
    if (idx > 2) return 0;
    f = ti_Open(slot_names[idx], "r");
    if (!f) return 0;
    if (ti_Read(s, sizeof(SaveSlot), 1, f) != 1) {
        ti_Close(f);
        return 0;
    }
    ti_Close(f);
    return s->magic == SAVE_MAGIC;
}

uint8_t save_write(uint8_t idx, const SaveSlot *s, uint8_t archive)
{
    ti_var_t f;
    if (idx > 2 || s->magic != SAVE_MAGIC) return 0;
    f = ti_Open(slot_names[idx], "w");
    if (!f) return 0;
    if (ti_Write((void *)s, sizeof(SaveSlot), 1, f) != 1) {
        ti_Close(f);
        return 0;
    }
    if (archive) ti_SetArchiveStatus(1, f);
    ti_Close(f);
    return 1;
}
