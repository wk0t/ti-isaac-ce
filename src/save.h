#ifndef ISAAC_SAVE_H
#define ISAAC_SAVE_H

#include "types.h"

#define SAVE_MAGIC  166   /* 0xA6 */
#define SAVE_FVER   2     /* format version; 0 = before v1.2 */
#define SGAMEROOMS  13    /* n_rooms <= 13 (guaranteed by the self-tests) */

typedef struct {
    uint8_t  magic;
    uint8_t  unlocked;      /* bits CHAR_UNLOCK_* */
    uint8_t  char_id;      /* last character played */
    uint8_t  in_progress;  /* 1 = saved run, can be resumed */
    uint8_t  wins_acc;     /* accumulated wins */
    uint16_t kills_acc;    /* accumulated kills (all runs) */
    uint16_t run_time;     /* timer of the current run (seconds) */
    uint16_t kills_run;    /* kills of the current run */
    Isaac    iz;           /* Isaac stats at save time */
    uint8_t  floor, cur, n_rooms, item, item2;
    uint8_t  dmg_floor;
    uint8_t  curse;        /* floor curses (bits CURSE_*) */
    uint16_t dmg_run;
    uint8_t  grid[MAP_H][MAP_W];
    RoomState rooms[SGAMEROOMS];
    uint8_t  save_fver;    /* SAVE_FVER; at the end of the struct: any
                              format evolution is APPENDED here */
} SaveSlot;

/* Cross-version compatibility: save_read reads the real size of the
   AppVar (older version shorter, or future version longer) and accepts
   the prefix. Never reorder or resize the existing SaveSlot fields. */

/* Fills the "run" part of the slot (meta unlocked/char_id/in_progress/
   wins_acc/kills_acc managed by the caller, not touched by pack). */
void save_pack(SaveSlot *s, const Game *g, const Isaac *iz, uint16_t run_time);
void save_unpack(Game *g, Isaac *iz, const SaveSlot *s);

/* AppVar ISAACSV1..3; returns 1 = ok */
uint8_t save_read(uint8_t idx, SaveSlot *s);
uint8_t save_write(uint8_t idx, const SaveSlot *s, uint8_t archive);

#endif /* ISAAC_SAVE_H */
