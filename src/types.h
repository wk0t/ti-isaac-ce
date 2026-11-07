#ifndef ISAAC_TYPES_H
#define ISAAC_TYPES_H

#include <stdint.h>

/* ---------- Geometrie ---------- */
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

/* Types de salles */
enum { RM_NONE = 0, RM_NORMAL, RM_START, RM_TREASURE, RM_BOSS,
       RM_SHOP, RM_SECRET, RM_DEVIL, RM_SACRI, RM_MAUDITE };

/* Ennemis */
enum { EN_NONE = 0, EN_GAPER, EN_FLY, EN_POOTER, EN_HORF, EN_MONSTRO,
       EN_CHARGER, EN_SPIDER, EN_GEMINI, EN_SAUTEUR, EN_CROIX, EN_MAMAN };

/* Obstacles (tuiles 16x16, persistants) */
enum { OB_NONE = 0, OB_ROCK, OB_SPIKE, OB_FIRE, OB_PED, OB_STOCK,
       OB_ROCK2, OB_PIT, OB_BLOCK, OB_POOP, OB_CACTUS, OB_BONES };

/* Pickups */
enum { PK_NONE = 0, PK_HEART, PK_HALF, PK_COIN, PK_KEY, PK_BOMB, PK_SILVER,
       PK_CHEST, PK_LOCKED, PK_TRAP, PK_DEVIL, PK_SOUL, PK_NUM };

/* Items (pool commun + rare + 2 exclusifs de la salle du diable) */
enum { IT_NONE = 0, IT_ONION, IT_EYE, IT_MUSH, IT_BREAKFAST,
       IT_SPOON, IT_BLOOD, IT_LUNCH, IT_NUM1, IT_PACTE, IT_PECHE,
       IT_BOUSSOLE, IT_AIMANT, IT_BOTTES, IT_STEAK, IT_CRANE,
       IT_AIGUILLE, IT_SPECTRE, IT_TELEGUIDE, IT_MASSE, IT_SOUFFLE,
       IT_DEMON, IT_VENIN, IT_TREFLE, IT_NUM };

/* Personnages deblocables (bits de save.unlocked) */
enum { CHAR_ISAAC = 0, CHAR_MAGDA, CHAR_CAINE, CHAR_LAZARE, CHAR_NUM1 };
#define CHAR_UNLOCK_MAGDA  1
#define CHAR_UNLOCK_CAINE  2
#define CHAR_UNLOCK_LAZARE 4

/* ---------- Cles/modeles ---------- */
#define MAX_ENEMIES 14
#define MAX_TEARS   28
#define MAX_PICKUPS 12
#define MAX_OBST    12
#define MAX_BOMBS   3

/* Prix boutique (pieces) */
#define PRICE_HEART 3
#define PRICE_KEY   5
#define PRICE_ITEM  15
#define TELE_MAX   8          /* kills pour recharger l'item actif TELE */
#define SECRET_BIT  16   /* flags : salle secrete revelee */

/* Half-px = coordonnees en demi-pixels, interieur relatif */
typedef struct {
    uint8_t  kind;
    uint16_t hp;          /* demi-points */
    uint8_t  flash;       /* frames de flash apres degat */
    int16_t  x, y;        /* centre, demi-pixels */
    int8_t   dx, dy;      /* demi-px par frame */
    uint8_t  t;           /* compteur d'anim/frame */
    uint8_t  st;          /* etat (boss) */
    uint8_t  shoot_cd;
    uint8_t  champ;       /* 1 = champion (rouge, robuste, butin garanti) */
    uint8_t  poison;      /* frames d'empoisonnement restantes */
    int8_t   kx, ky;      /* knockback accumule (demi-px/frame) */
} Enemy;

typedef struct {
    uint8_t  hostile;     /* 0 = larme d'Isaac, 1 = tir ennemi */
    uint8_t  dmg;         /* demi-points */
    int16_t  x, y;        /* centre, demi-px */
    int8_t   dx, dy;
    uint8_t  ttl;
    uint8_t  hitcd;       /* cooldown de toucher (tirs perceants) */
} Tear;

typedef struct {
    uint8_t  type;        /* PK_* */
    uint8_t  price;       /* cout en pieces (0 = gratuit) */
    uint8_t  tx, ty;      /* tuile (coin haut-gauche) */
    int16_t  x, y;        /* centre, demi-px (anime quand lache) */
} Pickup;

typedef struct {
    uint8_t  type;        /* OB_* */
    uint8_t  tx, ty;      /* tuile interieure 0..17 / 0..12 */
} Obst;

typedef struct {
    int16_t  x, y;        /* centre, demi-px */
    uint8_t  fuse;        /* frames restantes */
} Bomb;

/* ---------- Etat persistant d'une salle ---------- */
typedef struct {
    uint8_t kind;          /* RM_* */
    /* bit0 visitee, bit1 cleared, bit2 unlocked, bit3 item pris,
       bit4 secret revelee */
    uint8_t flags;
    uint8_t n_pick;
    Pickup  pick[MAX_PICKUPS];
    uint8_t n_obst;
    Obst    obst[MAX_OBST];
    uint8_t count;         /* compteur (sacrifices dans RM_SACRI) */
} RoomState;

/* ---------- Isaac ---------- */
typedef struct {
    int16_t x, y;         /* centre, demi-px */
    uint8_t hp;            /* demi-coeurs */
    uint8_t hp_max;        /* demi-coeurs, plafond 16 */
    uint8_t coins, keys, bombs;
    uint8_t dmg;           /* demi-points par larme */
    uint8_t speed;          /* demi-px/frame */
    uint8_t fire_delay;     /* frames entre tirs */
    uint8_t range;          /* frames de vol (ttl larme) */
    uint8_t triple;
    uint8_t fire_cd;        /* cooldown courant */
    uint8_t iframe;         /* frames d'invincibilite */
    uint8_t aim;            /* derniere direction DIR_* */
    uint8_t anim;
    int16_t maccx, maccy;   /* residu de deplacement (quarts de demi-px) */
    uint8_t hp_soul;      /* demi-coeurs d'ame (bleus), plafond 8 */
    uint32_t has_item;      /* bits = items possedes (1 << item) */
} Isaac;

/* ---------- Partie ---------- */
typedef struct {
    uint8_t  floor;        /* 1 a 3 */
    uint8_t  grid[MAP_H][MAP_W];   /* 0 = vide, sinon idx salle +1 */
    uint8_t  cur;          /* idx salle courante dans rooms[] */
    uint8_t  n_rooms;
    RoomState rooms[MAP_W * MAP_H];
    uint8_t  item;         /* item du pedestal ce floor (IT_*) */
    uint8_t  item2;        /* item en vente en boutique (IT_*) */
    uint16_t kills;
    uint16_t seed_pub;     /* pour affichage debug */
    uint8_t  dmg_floor;     /* demi-coeurs perdus sur l'etage courant */
    uint16_t dmg_run;       /* demi-coeurs perdus depuis le debut */
    uint8_t  curse;         /* bits : 1 = OBSCURITE, 2 = HATE */
} Game;

#define CURSE_DARKNESS 1
#define CURSE_HASTE    2

#endif /* ISAAC_TYPES_H */
