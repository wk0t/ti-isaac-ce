#ifndef ISAAC_TYPES_H
#define ISAAC_TYPES_H

#include <stdint.h>

/* ---------- Geometry ---------- */
#define TILE            16
#define ROOM_W_TILES    18
#define ROOM_H_TILES    13
#define ROOM_X          16          /* bord gauche interieur (px) */
#define ROOM_Y          16
#define ROOM_W          (ROOM_W_TILES * TILE)   /* 288 */
#define ROOM_H          (ROOM_H_TILES * TILE)   /* 208 */

#define MAP_W 9
#define MAP_H 8

/* Directions */
enum { DIR_N = 0, DIR_S = 1, DIR_W = 2, DIR_E = 3 };

/* Room types */
enum { RM_NONE = 0, RM_NORMAL, RM_START, RM_TREASURE, RM_BOSS,
       RM_SHOP, RM_SECRET, RM_DEVIL, RM_SACRI, RM_MAUDITE };

/* Enemies */
enum { EN_NONE = 0, EN_GAPER, EN_FLY, EN_POOTER, EN_HORF, EN_MONSTRO,
       EN_CHARGER, EN_SPIDER, EN_GEMINI, EN_SAUTEUR, EN_CROIX, EN_MAMAN };

/* Obstacles (16x16 tiles, persistent) */
enum { OB_NONE = 0, OB_ROCK, OB_SPIKE, OB_FIRE, OB_PED, OB_STOCK,
       OB_ROCK2, OB_PIT, OB_BLOCK, OB_POOP, OB_CACTUS, OB_BONES };

/* Pickups */
enum { PK_NONE = 0, PK_HEART, PK_HALF, PK_COIN, PK_KEY, PK_BOMB, PK_SILVER,
       PK_CHEST, PK_LOCKED, PK_TRAP, PK_DEVIL, PK_SOUL, PK_NUM };

/* Items (common pool + rare pool + 2 devil-room exclusives) */
enum { IT_NONE = 0, IT_ONION, IT_EYE, IT_MUSH, IT_BREAKFAST,
       IT_SPOON, IT_BLOOD, IT_LUNCH, IT_NUM1, IT_PACTE, IT_PECHE,
       IT_BOUSSOLE, IT_AIMANT, IT_BOTTES, IT_STEAK, IT_CRANE,
       IT_AIGUILLE, IT_SPECTRE, IT_TELEGUIDE, IT_MASSE, IT_SOUFFLE,
       IT_DEMON, IT_VENIN, IT_TREFLE, IT_NUM };

/* Unlockable characters (bits of save.unlocked) */
enum { CHAR_ISAAC = 0, CHAR_MAGDA, CHAR_CAINE, CHAR_LAZARE, CHAR_NUM1 };
#define CHAR_UNLOCK_MAGDA  1
#define CHAR_UNLOCK_CAINE  2
#define CHAR_UNLOCK_LAZARE 4

/* ---------- Keys/limits ---------- */
#define MAX_ENEMIES 14
#define MAX_TEARS   28
#define MAX_PICKUPS 12
#define MAX_OBST    12
#define MAX_BOMBS   3

/* Shop prices (coins) */
#define PRICE_HEART 3
#define PRICE_KEY   5
#define PRICE_ITEM  15
#define TELE_MAX   8          /* kills to recharge the active TELE item */
#define SECRET_BIT  16   /* flags: secret room revealed */

/* Half-px = coordinates in half-pixels, relative to the room interior */
typedef struct {
    uint8_t  kind;
    uint16_t hp;          /* half-points */
    uint8_t  flash;       /* flash frames after damage */
    int16_t  x, y;        /* center, half-px */
    int8_t   dx, dy;      /* half-px per frame */
    uint8_t  t;           /* anim counter/frame */
    uint8_t  st;          /* state (boss) */
    uint8_t  shoot_cd;
    uint8_t  champion;    /* 1 = champion (red, tough, guaranteed loot) */
    uint8_t  poison;      /* remaining poison frames */
    int8_t   kx, ky;      /* accumulated knockback (half-px/frame) */
} Enemy;

typedef struct {
    uint8_t  hostile;     /* 0 = Isaac's tear, 1 = enemy shot */
    uint8_t  dmg;         /* half-points */
    int16_t  x, y;        /* center, half-px */
    int8_t   dx, dy;
    uint8_t  ttl;
    uint8_t  hitcd;       /* hit cooldown (piercing shots) */
} Tear;

typedef struct {
    uint8_t  type;        /* PK_* */
    uint8_t  price;       /* price in coins (0 = free) */
    uint8_t  tx, ty;      /* tile (top-left corner) */
    int16_t  x, y;        /* center, half-px (animated when dropped) */
} Pickup;

typedef struct {
    uint8_t  type;        /* OB_* */
    uint8_t  tx, ty;      /* inner tile 0..17 / 0..12 */
} Obst;

typedef struct {
    int16_t  x, y;        /* center, half-px */
    uint8_t  fuse;        /* remaining frames */
} Bomb;

/* ---------- Persistent room state ---------- */
typedef struct {
    uint8_t kind;          /* RM_* */
    /* bit0 visited, bit1 cleared, bit2 unlocked, bit3 item taken,
       bit4 secret revealed */
    uint8_t flags;
    uint8_t n_pick;
    Pickup  pick[MAX_PICKUPS];
    uint8_t n_obst;
    Obst    obst[MAX_OBST];
    uint8_t count;         /* counter (sacrifices in RM_SACRI) */
} RoomState;

/* ---------- Isaac ---------- */
typedef struct {
    int16_t x, y;         /* center, half-px */
    uint8_t hp;            /* half-hearts */
    uint8_t hp_max;        /* half-hearts, cap 16 */
    uint8_t coins, keys, bombs;
    uint8_t dmg;           /* half-points per tear */
    uint8_t speed;          /* half-px/frame */
    uint8_t fire_delay;     /* frames between shots */
    uint8_t range;          /* flight frames (tear ttl) */
    uint8_t triple;
    uint8_t fire_cd;        /* current cooldown */
    uint8_t iframe;         /* invincibility frames */
    uint8_t aim;            /* last direction DIR_* */
    uint8_t anim;
    int16_t maccx, maccy;   /* movement residue (quarter half-px) */
    uint8_t hp_soul;      /* soul half-hearts (blue), cap 8 */
    uint32_t has_item;      /* bits = owned items (1 << item) */
} Isaac;

/* ---------- Game ---------- */
typedef struct {
    uint8_t  floor;        /* 1 to 3 */
    uint8_t  grid[MAP_H][MAP_W];   /* 0 = empty, else room idx +1 */
    uint8_t  cur;          /* current room idx in rooms[] */
    uint8_t  n_rooms;
    RoomState rooms[MAP_W * MAP_H];
    uint8_t  item;         /* pedestal item this floor (IT_*) */
    uint8_t  item2;        /* item for sale in the shop (IT_*) */
    uint16_t kills;
    uint16_t seed_pub;     /* for debug display */
    uint8_t  dmg_floor;     /* half-hearts lost on the current floor */
    uint16_t dmg_run;       /* half-hearts lost since the start */
    uint8_t  curse;         /* bits: 1 = DARKNESS, 2 = HASTE */
} Game;

#define CURSE_DARKNESS 1
#define CURSE_HASTE    2

#endif /* ISAAC_TYPES_H */
