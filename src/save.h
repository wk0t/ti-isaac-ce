#ifndef ISAAC_SAVE_H
#define ISAAC_SAVE_H

#include "types.h"

#define SAVE_MAGIC  166   /* 0xA6 */
#define SGAMEROOMS  13    /* n_rooms <= 13 (garanti par les self-tests) */

typedef struct {
    uint8_t  magic;
    uint8_t  unlocked;      /* bits CHAR_UNLOCK_* */
    uint8_t  char_id;      /* dernier personnage joue */
    uint8_t  in_progress;  /* 1 = run sauvegardee, reprise possible */
    uint8_t  wins_acc;     /* victoires cumulees */
    uint16_t kills_acc;    /* ennemis tues cumules (toutes runs) */
    uint16_t run_time;     /* chrono de la run en cours (secondes) */
    uint16_t kills_run;    /* kills de la run en cours */
    Isaac    iz;           /* stats Isaac au moment de la sauvegarde */
    uint8_t  floor, cur, n_rooms, item, item2;
    uint8_t  dmg_floor;
    uint8_t  curse;        /* malédictions de l'étage (bits CURSE_*) */
    uint16_t dmg_run;
    uint8_t  grid[MAP_H][MAP_W];
    RoomState rooms[SGAMEROOMS];
} SaveSlot;

/* Remplit la partie "run" du slot (meta unlocked/char_id/in_progress/
   wins_acc/kills_acc geres par l'appelant, non touches par pack). */
void save_pack(SaveSlot *s, const Game *g, const Isaac *iz, uint16_t run_time);
void save_unpack(Game *g, Isaac *iz, const SaveSlot *s);

/* AppVar ISAACSV1..3 ; retour 1 = ok */
uint8_t save_read(uint8_t idx, SaveSlot *s);
uint8_t save_write(uint8_t idx, const SaveSlot *s, uint8_t archive);

#endif /* ISAAC_SAVE_H */
