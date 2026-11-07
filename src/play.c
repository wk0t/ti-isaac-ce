#include "play.h"
#include "rng.h"
#include "gen.h"
#include "palette.h"
#include <graphx.h>
#include <string.h>
#include "gfx/gfx.h"

/* personnage courant (ISAAC / MAGDA / CAINE / LAZARE), choisi dans main.c */
uint8_t play_char_id = CHAR_ISAAC;

/* ---------- runtime pools ---------- */
static Enemy  en[MAX_ENEMIES];
static uint8_t n_en;
static Tear   tr[MAX_TEARS];
static uint8_t n_tr;
static Bomb   bm[MAX_BOMBS];
static uint8_t n_bm;

/* ---------- particules (juice) ---------- */
#define MAX_PARTS 24
typedef struct {
    int16_t x, y;
    int8_t  dx, dy;
    uint8_t ttl, col;
} Part;
static Part    pt[MAX_PARTS];
static uint8_t n_pt;
static uint8_t hurtflash;   /* frames de bordure rouge apres degat */
static uint8_t shake;       /* frames de tremblement d'ecran restantes */
static uint8_t shx, shy;     /* offsets de rendu du tremblement (>= 0) */

/* le rendu de la salle secoue via ROOM_X/ROOM_Y en transparence : offsets
   uniquement positifs, les sprites NoClip ne tolerent pas de sous-depassement */
#undef ROOM_X
#undef ROOM_Y
#define ROOM_X (16u + shx)
#define ROOM_Y (16u + shy)

/* ---------- juice : hit-stop, textes flottants, decals, banniere ---------- */
static uint8_t hitstop;      /* frames de gel au moment des impacts */
static uint8_t boss_intro;   /* frames d'ecartitre avant le boss */
static const char *boss_name;
static uint8_t tele_charge;  /* kills accumules vers la recharge TELE */

#define MAX_POPS 3
typedef struct { int16_t x, y; uint8_t ttl, col; char txt[10]; } Pop;
static Pop pops[MAX_POPS];

#define MAX_DECALS 12
typedef struct { int16_t x, y; uint8_t ttl; } Decal;
static Decal decals[MAX_DECALS];

static uint8_t banner_ttl;
static const char *banner_txt;
static const char *banner_desc;

static void burst(int16_t x, int16_t y, uint8_t n, uint8_t col)
{
    while (n--) {
        if (n_pt >= MAX_PARTS) return;
        pt[n_pt].x = x; pt[n_pt].y = y;
        pt[n_pt].dx = (int8_t)(rng_range(9) - 4);
        pt[n_pt].dy = (int8_t)(rng_range(9) - 4);
        pt[n_pt].ttl = (uint8_t)(10 + rng_range(12));
        pt[n_pt].col = col;
        n_pt++;
    }
}

static void spawn_pop(int16_t x, int16_t y, const char *txt, uint8_t col)
{
    uint8_t s = 0;
    while (s < MAX_POPS && pops[s].ttl) s++;
    if (s >= MAX_POPS) s = 0;
    pops[s].x = x; pops[s].y = y;
    pops[s].ttl = 38; pops[s].col = col;
    strncpy(pops[s].txt, txt, sizeof pops[s].txt - 1);
    pops[s].txt[sizeof pops[s].txt - 1] = '\0';
}

static void spawn_banner(const char *txt, const char *desc)
{
    banner_txt = txt;
    banner_desc = desc;
    banner_ttl = 90;
}

static void spawn_decal(int16_t x, int16_t y)
{
    uint8_t s = 0;
    while (s < MAX_DECALS && decals[s].ttl) s++;
    if (s >= MAX_DECALS) s = 0;
    decals[s].x = x; decals[s].y = y;
    decals[s].ttl = (uint8_t)(150 + rng_range(240));
}

/* reset des effets a l'entree d'une salle */
static void clear_fx(void)
{
    uint8_t s;
    for (s = 0; s < MAX_POPS; s++) pops[s].ttl = 0;
    for (s = 0; s < MAX_DECALS; s++) decals[s].ttl = 0;
    n_pt = 0;
    hurtflash = 0;
    hitstop = 0;
}

/* definie plus bas (utilisee par play_enter_room pour la salle maudite) */
static void hurt_isaac(Game *g, Isaac *iz, uint8_t dmg);
static const char *item_name(uint8_t item)
{
    switch (item) {
    case IT_ONION:     return "OIGNON";
    case IT_EYE:       return "OEIL LARGE";
    case IT_MUSH:      return "CHAMPI";
    case IT_BREAKFAST: return "PETIT-DEJ";
    case IT_SPOON:     return "CUILLERE";
    case IT_BLOOD:     return "SANG";
    case IT_LUNCH:     return "BENTHO";
    case IT_NUM1:      return "MYSTERE";
    case IT_PACTE:     return "PACTE";
    case IT_PECHE:     return "PECHE";
    case IT_BOUSSOLE:  return "BOUSSOLE";
    case IT_AIMANT:    return "AIMANT";
    case IT_BOTTES:    return "BOTTES";
    case IT_STEAK:     return "STEAK";
    case IT_CRANE:     return "CRANE";
    case IT_AIGUILLE:  return "AIGUILLE";
    case IT_SPECTRE:   return "SPECTRE";
    case IT_TELEGUIDE: return "TELEGUIDE";
    case IT_MASSE:     return "MASSE";
    case IT_SOUFFLE:   return "SOUFFLE";
    case IT_DEMON:     return "COEUR DEMO";
    case IT_VENIN:     return "VENIN";
    case IT_TREFLE:    return "TREFLE";
    default:           return "???";
    }
}

/* nom public (ecran de fin) */
const char *play_item_name(uint8_t item)
{
    return item_name(item);
}

/* banniere public (main.c : resurrection de Lazare, etc.) */
void play_notify(const char *txt, const char *desc)
{
    spawn_banner(txt, desc);
}

/* charge de l'item actif TELE (0..TELE_MAX kills) */
uint8_t play_tele_charge(void)
{
    return tele_charge;
}

/* effet de l'item, en anglais, affiche au ramassage */
static const char *item_desc(uint8_t item)
{
    switch (item) {
    case IT_ONION:     return "TEARS UP";
    case IT_EYE:       return "TRIPLE SHOT, TEARS DOWN";
    case IT_MUSH:      return "ALL STATS UP + HP";
    case IT_BREAKFAST: return "HP UP";
    case IT_SPOON:     return "SPEED UP";
    case IT_BLOOD:     return "DMG UP";
    case IT_LUNCH:     return "HP UP";
    case IT_NUM1:      return "TEARS UP, RANGE DOWN";
    case IT_PACTE:     return "DMG WAY UP";
    case IT_PECHE:     return "TRIPLE SHOT + DMG UP";
    case IT_BOUSSOLE:  return "SEE THE MAP";
    case IT_AIMANT:    return "ATTRACTS PICKUPS";
    case IT_BOTTES:    return "SPEED WAY UP";
    case IT_STEAK:     return "HP UP +2";
    case IT_CRANE:     return "DMG UP";
    case IT_AIGUILLE:  return "PIERCING SHOTS";
    case IT_SPECTRE:   return "SPECTRAL TEARS";
    case IT_TELEGUIDE: return "HOMING TEARS";
    case IT_MASSE:     return "DMG UP + BIG TEARS";
    case IT_SOUFFLE:   return "KNOCKBACK SHOTS";
    case IT_DEMON:     return "+2 SOUL HEARTS";
    case IT_VENIN:     return "POISON TEARS";
    case IT_TREFLE:    return "LUCK UP";
    default:           return "";
    }
}

/* ---------- petits helpers ---------- */

static RoomState *cur_room(Game *g) { return &g->rooms[g->cur]; }
static const RoomState *cur_room_c(const Game *g) { return &g->rooms[g->cur]; }

static const Obst *obst_at(const Game *g, uint8_t tx, uint8_t ty)
{
    const RoomState *r = cur_room_c(g);
    uint8_t i;
    for (i = 0; i < r->n_obst; i++)
        if (r->obst[i].tx == tx && r->obst[i].ty == ty) return &r->obst[i];
    return 0;
}

/* variante modifiable : destruction poop/bones par les larmes */
static Obst *obst_at_rw(Game *g, uint8_t tx, uint8_t ty)
{
    RoomState *r = cur_room(g);
    uint8_t i;
    for (i = 0; i < r->n_obst; i++)
        if (r->obst[i].tx == tx && r->obst[i].ty == ty) return &r->obst[i];
    return 0;
}

/* fosses et pics : la larme passe au-dessus sans dommage */
static uint8_t tear_passes(const Game *g, const Tear *t)
{
    const Obst *o = obst_at(g, (uint8_t)(t->x >> 5), (uint8_t)(t->y >> 5));
    return o && (o->type == OB_PIT || o->type == OB_SPIKE);
}

/* Solide pour une salle quelconque (expose aussi pour les self-tests). */
uint8_t play_diag_solid(const Game *g, uint8_t room, uint8_t tx, uint8_t ty)
{
    const RoomState *r = &g->rooms[room];
    uint8_t i;
    for (i = 0; i < r->n_obst; i++)
        if (r->obst[i].tx == tx && r->obst[i].ty == ty) {
            uint8_t t = r->obst[i].type;
            return t == OB_ROCK || t == OB_ROCK2 || t == OB_FIRE
                || t == OB_STOCK || t == OB_PIT || t == OB_BLOCK
                || t == OB_POOP || t == OB_CACTUS || t == OB_BONES;
        }
    return 0;
}

static uint8_t solid_at(const Game *g, uint8_t tx, uint8_t ty)
{
    return play_diag_solid(g, g->cur, tx, ty);
}

/* Boite en demi-px (centre + demi-taille) : collision obstacle solide ? */
static uint8_t box_blocked(const Game *g, int16_t x, int16_t y, int16_t half)
{
    int16_t xmin = x - half, xmax = x + half;
    int16_t ymin = y - half, ymax = y + half;
    int16_t tx0 = xmin >> 5, tx1 = xmax >> 5;
    int16_t ty0 = ymin >> 5, ty1 = ymax >> 5;
    int16_t tx, ty;
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > ROOM_W_TILES - 1) tx1 = ROOM_W_TILES - 1;
    if (ty1 > ROOM_H_TILES - 1) ty1 = ROOM_H_TILES - 1;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            if (solid_at(g, (uint8_t)tx, (uint8_t)ty)) return 1;
    return 0;
}

static int16_t move_axis(const Game *g, int16_t pos, int16_t other,
                         int16_t half, int16_t delta)
{
    int16_t np = (int16_t)(pos + delta);
    if (np == pos) return pos;
    if (box_blocked(g, np, other, half)) return pos;
    return np;
}

/* case voisine occupee (grid) pour savoir si une porte existe ;
   la porte vers une salle secrete est cachee tant qu'elle n'est pas
   revelee par une bombe. */
static uint8_t has_door(const Game *g, uint8_t dir)
{
    uint8_t cx, cy;
    for (cy = 0; cy < MAP_H; cy++)
        for (cx = 0; cx < MAP_W; cx++)
            if (g->grid[cy][cx] == g->cur + 1) {
                uint8_t nb = gen_neighbor(g, cx, cy, dir);
                if (!nb) return 0;
                if (g->rooms[nb - 1].kind == RM_SECRET
                    && !(g->rooms[nb - 1].flags & SECRET_BIT))
                    return 0;
                if (g->rooms[nb - 1].kind == RM_DEVIL
                    && !(g->rooms[nb - 1].flags & 4))
                    return 0;
                return 1;
            }
    return 0;
}

/* porte verrouillee (trone) : consomme une cle, sinon bloque */
static uint8_t open_locked(Game *g, Isaac *iz, uint8_t dir)
{
    uint8_t cx, cy;
    for (cy = 0; cy < MAP_H; cy++)
        for (cx = 0; cx < MAP_W; cx++)
            if (g->grid[cy][cx] == g->cur + 1) {
                uint8_t nb = gen_neighbor(g, cx, cy, dir);
                RoomState *rn;
                if (!nb) return 0;
                rn = &g->rooms[nb - 1];
                if (rn->kind == RM_TREASURE && !(rn->flags & 4)) {
                    if (!iz->keys) return 0;
                    iz->keys--;
                    rn->flags |= 4;
                }
                return 1;
            }
    return 0;
}

/* ---------- entree / sortie de salle ---------- */

void play_reset_isaac(Isaac *iz)
{
    memset(iz, 0, sizeof *iz);
    iz->hp = 6;
    iz->hp_max = 6;
    iz->dmg = 7;
    iz->speed = 12;
    iz->fire_delay = 14;
    iz->range = 30;
    iz->bombs = 3;
    iz->keys = 1;
    iz->aim = DIR_S;
}

void play_start_floor(Game *g, Isaac *iz)
{
    g->dmg_floor = 0;
    tele_charge = 0;
    play_enter_room(g, iz, -1);
    if (g->curse & CURSE_DARKNESS)
        spawn_banner("MALEDICTION", "OBSCURITE...");
    else if (g->curse & CURSE_HASTE)
        spawn_banner("MALEDICTION", "HATE !");
}

void play_enter_room(Game *g, Isaac *iz, int8_t from_dir)
{
    RoomState *r = cur_room(g);
    uint8_t i;

    n_en = 0; n_tr = 0; n_bm = 0; clear_fx(); shake = 0;
    r->flags |= 1; /* visitee */

    switch (from_dir) {
    case DIR_N: /* entre par la porte du haut */
        iz->x = 304; iz->y = 24; iz->aim = DIR_S; break;
    case DIR_S:
        iz->x = 304; iz->y = (int16_t)(ROOM_H * 2 - 24); iz->aim = DIR_N; break;
    case DIR_W:
        iz->x = 24; iz->y = 240; iz->aim = DIR_E; break;
    case DIR_E:
        iz->x = (int16_t)(ROOM_W * 2 - 24); iz->y = 240; iz->aim = DIR_W; break;
    default:
        iz->x = (int16_t)(ROOM_W); iz->y = (int16_t)(ROOM_H);
        iz->aim = DIR_S;
        break;
    }

    if (r->kind == RM_BOSS && !(r->flags & 2)) {
        if (g->floor == 3) {
            /* boss final : MAMAN, au fond de L'ANTRE */
            en[0].kind = EN_MAMAN;
            en[0].hp = 1000;
            en[0].x = (int16_t)(ROOM_W);
            en[0].y = (int16_t)(ROOM_H - 20);
            en[0].flash = 0; en[0].t = 0; en[0].st = 0;
            en[0].dx = 0; en[0].dy = 0; en[0].shoot_cd = 70;
            en[0].champ = 0; en[0].poison = 0;
            en[0].kx = 0; en[0].ky = 0;
            n_en = 1;
            boss_name = "MAMAN";
        } else if (g->floor == 2 && rng_range(2) == 0) {
            /* boss alternatif : les jumeaux */
            en[0].kind = EN_GEMINI;
            en[0].hp = 320;
            en[0].x = (int16_t)(ROOM_W - 60);
            en[0].y = (int16_t)(ROOM_H - 40);
            en[0].flash = 0; en[0].t = 0; en[0].st = 0;
            en[0].dx = 4; en[0].dy = 4; en[0].shoot_cd = 40;
            en[0].champ = 0; en[0].poison = 0;
            en[0].kx = 0; en[0].ky = 0;
            en[1].kind = EN_GEMINI;
            en[1].hp = 320;
            en[1].x = (int16_t)(ROOM_W + 60);
            en[1].y = (int16_t)(ROOM_H - 40);
            en[1].flash = 0; en[1].t = 16; en[1].st = 0;
            en[1].dx = 4; en[1].dy = 4; en[1].shoot_cd = 80;
            en[1].champ = 0; en[1].poison = 0;
            en[1].kx = 0; en[1].ky = 0;
            n_en = 2;
            boss_name = "LES JUMEAUX";
        } else {
            en[0].kind = EN_MONSTRO;
            en[0].hp = (uint16_t)((g->floor == 2) ? 800 : 500);
            en[0].x = (int16_t)(ROOM_W);
            en[0].y = (int16_t)(ROOM_H - 40);
            en[0].flash = 0; en[0].t = 0; en[0].st = 0;
            en[0].dx = 0; en[0].dy = 0; en[0].shoot_cd = 60;
            en[0].champ = 0; en[0].poison = 0;
            en[0].kx = 0; en[0].ky = 0;
            n_en = 1;
            boss_name = "MONSTRO";
        }
        boss_intro = 66;
        return;
    }

    if (r->kind == RM_NORMAL && !(r->flags & 2)) {
        uint8_t count = (uint8_t)((g->floor == 1) ? 3 + rng_range(3)
                                 : (g->floor == 2) ? 4 + rng_range(3)
                                 : 5 + rng_range(4));
        for (i = 0; i < count && n_en < MAX_ENEMIES; i++) {
            uint8_t kind, tries;
            int16_t sx = 0, sy = 0;
            uint8_t roll = rng_range(100);
            if (g->floor == 1) {
                if      (roll < 40) kind = EN_GAPER;
                else if (roll < 80) kind = EN_FLY;
                else                kind = EN_POOTER;
            } else if (g->floor == 3) {
                if      (roll < 20) kind = EN_GAPER;
                else if (roll < 35) kind = EN_FLY;
                else if (roll < 47) kind = EN_POOTER;
                else if (roll < 60) kind = EN_HORF;
                else if (roll < 73) kind = EN_CHARGER;
                else if (roll < 85) kind = EN_SPIDER;
                else if (roll < 93) kind = EN_SAUTEUR;
                else                kind = EN_CROIX;
            } else {
                if      (roll < 30) kind = EN_GAPER;
                else if (roll < 50) kind = EN_FLY;
                else if (roll < 65) kind = EN_POOTER;
                else if (roll < 80) kind = EN_HORF;
                else if (roll < 90) kind = EN_CHARGER;
                else                kind = EN_SPIDER;
            }
            for (tries = 0; tries < 20; tries++) {
                sx = (int16_t)((2 + rng_range(14)) * 32 + 16);
                sy = (int16_t)((2 + rng_range(9)) * 32 + 16);
                if (solid_at(g, (uint8_t)(sx >> 5), (uint8_t)(sy >> 5))) continue;
                {
                    int16_t ddx = (int16_t)(sx - iz->x), ddy = (int16_t)(sy - iz->y);
                    if (ddx < 0) ddx = (int16_t)-ddx;
                    if (ddy < 0) ddy = (int16_t)-ddy;
                    if (ddx > 176 || ddy > 176) break;
                }
            }
            en[n_en].kind = kind;
            en[n_en].x = sx; en[n_en].y = sy;
            switch (kind) {
            case EN_GAPER:
                en[n_en].hp = (uint16_t)(28 + 8 * (g->floor - 1));
                en[n_en].dx = 3; en[n_en].dy = 3;
                en[n_en].shoot_cd = 0; break;
            case EN_FLY:
                en[n_en].hp = (uint16_t)(12 + 2 * (g->floor - 1));
                en[n_en].dx = 4; en[n_en].dy = 0;
                en[n_en].shoot_cd = 0; break;
            case EN_POOTER:
                en[n_en].hp = (uint16_t)(20 + 6 * (g->floor - 1));
                en[n_en].dx = 0; en[n_en].dy = 2;
                en[n_en].shoot_cd = (uint8_t)(60 + rng_range(60)); break;
            case EN_CHARGER:
                en[n_en].hp = (uint16_t)(26 + 6 * (g->floor - 1));
                en[n_en].dx = 0; en[n_en].dy = 0;
                en[n_en].shoot_cd = 0; break;
            case EN_SPIDER:
                en[n_en].hp = (uint16_t)(10 + 2 * (g->floor - 1));
                en[n_en].dx = 2; en[n_en].dy = 2;
                en[n_en].shoot_cd = 0; break;
            case EN_SAUTEUR:
                en[n_en].hp = (uint16_t)(26 + 6 * g->floor);
                en[n_en].dx = 0; en[n_en].dy = 0;
                en[n_en].shoot_cd = (uint8_t)(20 + rng_range(40)); break;
            case EN_CROIX:
                en[n_en].hp = (uint16_t)(22 + 8 * g->floor);
                en[n_en].dx = 0; en[n_en].dy = 0;
                en[n_en].shoot_cd = (uint8_t)(50 + rng_range(40)); break;
            default:
                en[n_en].hp = (uint16_t)(48 + 12 * (g->floor - 1));
                en[n_en].dx = 0; en[n_en].dy = 0;
                en[n_en].shoot_cd = (uint8_t)(50 + rng_range(40)); break;
            }
            en[n_en].flash = 0; en[n_en].t = (uint8_t)rng_range(32);
            en[n_en].st = 0;
            en[n_en].poison = 0;
            en[n_en].kx = 0; en[n_en].ky = 0;
            /* champion : robuste, rapide, butin garanti */
            en[n_en].champ = 0;
            if (rng_range(100) < ((g->floor == 1) ? 5
                                  : (g->floor == 2) ? 10 : 18)) {
                en[n_en].champ = 1;
                en[n_en].hp = (uint16_t)(en[n_en].hp * 3 / 2);
                if (en[n_en].dx > 0) en[n_en].dx++;
                else if (en[n_en].dx < 0) en[n_en].dx--;
                if (en[n_en].dy > 0) en[n_en].dy++;
                else if (en[n_en].dy < 0) en[n_en].dy--;
            }
            n_en++;
        }
    }

    if (r->kind == RM_MAUDITE && !(r->flags & 2)) {
        /* garde maudite : la porte mord en entrant */
        uint8_t count = (uint8_t)(3 + rng_range(2));
        for (i = 0; i < count && n_en < MAX_ENEMIES; i++) {
            uint8_t kind = (rng_range(2)) ? EN_SPIDER : EN_FLY;
            en[n_en].kind = kind;
            en[n_en].x = (int16_t)((3 + rng_range(12)) * 32 + 16);
            en[n_en].y = (int16_t)((2 + rng_range(8)) * 32 + 16);
            if (kind == EN_SPIDER) {
                en[n_en].hp = (uint16_t)(10 + 2 * g->floor);
                en[n_en].dx = 2; en[n_en].dy = 2;
            } else {
                en[n_en].hp = (uint16_t)(12 + 2 * g->floor);
                en[n_en].dx = 4; en[n_en].dy = 0;
            }
            en[n_en].flash = 0; en[n_en].t = (uint8_t)rng_range(32);
            en[n_en].st = 0; en[n_en].shoot_cd = 0;
            en[n_en].champ = 0; en[n_en].poison = 0;
            en[n_en].kx = 0; en[n_en].ky = 0;
            n_en++;
        }
        if (from_dir >= 0) hurt_isaac(g, iz, 1);
    }
}

/* ---------- items ---------- */

void play_apply_item(Isaac *iz, uint8_t item)
{
    switch (item) {
    case IT_ONION:
        if (iz->fire_delay > 6) iz->fire_delay = (uint8_t)(iz->fire_delay * 3 / 4);
        break;
    case IT_EYE:
        iz->triple = 1;
        if (iz->fire_delay < 26)
            iz->fire_delay = (uint8_t)(iz->fire_delay * 5 / 4);
        break;
    case IT_MUSH:
        if (iz->hp_max < 16) iz->hp_max += 2;
        if (iz->hp < iz->hp_max) iz->hp = (uint8_t)(iz->hp + 2);
        if (iz->hp > iz->hp_max) iz->hp = iz->hp_max;
        iz->dmg = (uint8_t)(iz->dmg + 2);
        if (iz->speed < 22) iz->speed = (uint8_t)(iz->speed + 2);
        break;
    case IT_BREAKFAST:
    case IT_LUNCH:
        if (iz->hp_max < 16) iz->hp_max += 2;
        if (iz->hp < iz->hp_max) iz->hp = (uint8_t)(iz->hp + 2);
        break;
    case IT_SPOON:
        if (iz->speed < 22) iz->speed = (uint8_t)(iz->speed + 2);
        break;
    case IT_BLOOD:
        iz->dmg = (uint8_t)(iz->dmg + 2);
        break;
    case IT_NUM1:
        if (iz->fire_delay > 5) iz->fire_delay = (uint8_t)(iz->fire_delay * 3 / 5);
        if (iz->range > 10) iz->range = (uint8_t)(iz->range - 8);
        break;
    case IT_PACTE:
        iz->dmg = (uint8_t)(iz->dmg + 6);
        break;
    case IT_PECHE:
        iz->triple = 1;
        iz->dmg = (uint8_t)(iz->dmg + 2);
        break;
    case IT_BOTTES:
        if (iz->speed < 20) iz->speed = (uint8_t)(iz->speed + 4);
        break;
    case IT_STEAK:
        if (iz->hp_max < 16) iz->hp_max = (uint8_t)(iz->hp_max + 4);
        if (iz->hp_max - iz->hp > 4) iz->hp = (uint8_t)(iz->hp + 4);
        else iz->hp = iz->hp_max;
        break;
    case IT_CRANE:
        iz->dmg = (uint8_t)(iz->dmg + 3);
        break;
    case IT_MASSE:
        iz->dmg = (uint8_t)(iz->dmg + 2);
        break;
    case IT_DEMON:
        iz->hp_soul = (uint8_t)(iz->hp_soul + 4);
        if (iz->hp_soul > 8) iz->hp_soul = 8;
        break;
    default:
        break;
    }
    if (iz->hp > iz->hp_max) iz->hp = iz->hp_max;
}

/* ---------- butin / coffres ---------- */

/* place un pickup dans un slot libre de la salle (silencieux si plein) */
static void room_give_pick(RoomState *r, uint8_t type, uint8_t tx, uint8_t ty)
{
    uint8_t k, slot = 0xFF;
    for (k = 0; k < MAX_PICKUPS; k++)
        if (r->pick[k].type == PK_NONE || k >= r->n_pick) { slot = k; break; }
    if (slot == 0xFF) return;
    if (slot >= r->n_pick) r->n_pick = (uint8_t)(slot + 1);
    r->pick[slot].type = type;
    r->pick[slot].price = 0;
    r->pick[slot].tx = tx;
    r->pick[slot].ty = ty;
    r->pick[slot].x = (int16_t)(tx * 2 * TILE + TILE);
    r->pick[slot].y = (int16_t)(ty * 2 * TILE + TILE);
}

/* ouvre un coffre : disperse le butin autour de la tuile */
static void drop_loot(Game *g, RoomState *r, uint8_t tx, uint8_t ty,
                      uint8_t locked)
{
    static const int8_t OFF[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
    uint8_t n = locked ? (uint8_t)(3 + rng_range(2))
                       : (uint8_t)(2 + rng_range(2));
    uint8_t i;
    for (i = 0; i < n; i++) {
        uint8_t type, ox = tx, oy = ty;
        uint8_t roll = rng_range(100);
        int nx = tx + OFF[i & 3][0], ny = ty + OFF[i & 3][1];
        if (nx >= 1 && nx < ROOM_W_TILES - 1
            && !play_diag_solid(g, g->cur, (uint8_t)nx, (uint8_t)ny))
            ox = (uint8_t)nx;
        if (ny >= 1 && ny < ROOM_H_TILES - 1
            && !play_diag_solid(g, g->cur, (uint8_t)nx, (uint8_t)ny))
            oy = (uint8_t)ny;
        if      (roll < 30) type = PK_COIN;
        else if (roll < 45) type = PK_HALF;
        else if (roll < 60) type = PK_BOMB;
        else if (roll < 72) type = PK_KEY;
        else if (roll < 82) type = PK_SILVER;
        else                type = PK_HEART;
        if (locked && i == 0) type = PK_SILVER;
        /* coffre verrouille : petit espoir d'un coeur d'ame */
        if (locked && rng_range(5) == 0) type = PK_SOUL;
        room_give_pick(r, type, ox, oy);
    }
}

/* ---------- larmes ---------- */

static void spawn_tear(Game *g, Isaac *iz, int8_t dx, int8_t dy)
{
    (void)g;
    if (n_tr >= MAX_TEARS) return;
    tr[n_tr].hostile = 0;
    tr[n_tr].dmg = iz->dmg;
    tr[n_tr].x = iz->x; tr[n_tr].y = iz->y;
    tr[n_tr].dx = (int8_t)(dx * 10); tr[n_tr].dy = (int8_t)(dy * 10);
    tr[n_tr].ttl = iz->range;
    tr[n_tr].hitcd = 0;
    n_tr++;
}

static void spawn_eshot(int16_t x, int16_t y, int8_t dx, int8_t dy,
                        uint8_t dmg, uint8_t speed)
{
    if (n_tr >= MAX_TEARS) return;
    {
        Tear *t = &tr[n_tr];
        t->hostile = 1;
        t->dmg = dmg;
        t->x = x; t->y = y;
        t->dx = (int8_t)(dx * speed); t->dy = (int8_t)(dy * speed);
        t->ttl = 90;
        t->hitcd = 0;
        n_tr++;
    }
}

/* vecteur normalise +/-1 vers une cible (8 dirs) */
static void aim8(int16_t fx, int16_t fy, int16_t tx, int16_t ty,
                 int8_t *dx, int8_t *dy)
{
    int adx = tx - fx, ady = ty - fy;
    int ax = adx < 0 ? -adx : adx, ay = ady < 0 ? -ady : ady;
    if (ax > 2 * ay) { *dx = adx > 0 ? 1 : -1; *dy = 0; }
    else if (ay > 2 * ax) { *dx = 0; *dy = ady > 0 ? 1 : -1; }
    else { *dx = adx > 0 ? 1 : -1; *dy = ady > 0 ? 1 : -1; }
}

/* ---------- degats issac ---------- */

/* degats + knockback (kdx/kdy : poussée en demi-px ; 0 = pas de poussée) */
static void hurt_isaac_k(Game *g, Isaac *iz, uint8_t dmg, int8_t kdx, int8_t kdy)
{
    uint8_t total = dmg;
    if (iz->iframe) return;
    /* les coeurs d'ame encaissent avant les coeurs rouges */
    if (iz->hp_soul) {
        uint8_t take = (dmg < iz->hp_soul) ? dmg : iz->hp_soul;
        iz->hp_soul = (uint8_t)(iz->hp_soul - take);
        dmg = (uint8_t)(dmg - take);
    }
    if (dmg && iz->hp <= dmg) {
        iz->hp = 0;
        hurtflash = 6; shake = 8;
        g->dmg_floor += total; g->dmg_run += total;
        return;
    }
    if (dmg) iz->hp = (uint8_t)(iz->hp - dmg);
    iz->iframe = 45;
    hurtflash = 6;
    shake = 8;
    g->dmg_floor += total;
    g->dmg_run += total;
    if (kdx || kdy) {
        iz->x = move_axis(g, iz->x, iz->y, 12, kdx);
        iz->y = move_axis(g, iz->y, iz->x, 12, kdy);
    }
}

static void hurt_isaac(Game *g, Isaac *iz, uint8_t dmg)
{
    hurt_isaac_k(g, iz, dmg, 0, 0);
}

/* ---------- explosion ---------- */

static void explode(Game *g, Isaac *iz, int16_t cx, int16_t cy)
{
    RoomState *r = cur_room(g);
    uint8_t i;
    burst(cx, cy, 12, COL_GREY);
    shake = 10;
    for (i = 0; i < n_en; i++) {
        int16_t ddx = (int16_t)(en[i].x - cx), ddy = (int16_t)(en[i].y - cy);
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        if (ddx < 72 && ddy < 72 && en[i].hp <= 120)
            en[i].hp = 0;
        else if (ddx < 72 && ddy < 72)
            en[i].hp = (uint16_t)(en[i].hp - 120);
    }
    for (i = 0; i < r->n_obst; i++) {
        Obst *o = &r->obst[i];
        int16_t ox = (int16_t)(o->tx * 32 + 16), oy = (int16_t)(o->ty * 32 + 16);
        int16_t ddx = (int16_t)(ox - cx), ddy = (int16_t)(oy - cy);
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        if (ddx < 60 && ddy < 60
            && (o->type == OB_ROCK || o->type == OB_ROCK2
                || o->type == OB_POOP || o->type == OB_FIRE
                || o->type == OB_CACTUS || o->type == OB_BONES)) {
            if (o->type == OB_ROCK2 && rng_range(4) == 0)
                room_give_pick(r, PK_COIN, o->tx, o->ty);
            else if (o->type == OB_POOP && rng_range(8) == 0)
                room_give_pick(r, PK_COIN, o->tx, o->ty);
            o->type = OB_NONE;
        }
    }
    {
        int16_t ddx = (int16_t)(iz->x - cx), ddy = (int16_t)(iz->y - cy);
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        if (ddx < 44 && ddy < 44) {
            int8_t bkx = (iz->x >= cx) ? 10 : -10;
            int8_t bky = (iz->y >= cy) ? 10 : -10;
            hurt_isaac_k(g, iz, 2, bkx, bky);
        }
    }

    /* bombe proche d'un mur adjacent a une salle secrete : revelation */
    {
        uint8_t gx, gy;
        for (gy = 0; gy < MAP_H; gy++)
            for (gx = 0; gx < MAP_W; gx++)
                if (g->grid[gy][gx] == g->cur + 1) {
                    uint8_t d;
                    for (d = 0; d < 4; d++) {
                        uint8_t nb = gen_neighbor(g, gx, gy, d);
                        RoomState *rn;
                        if (!nb) continue;
                        rn = &g->rooms[nb - 1];
                        if (rn->kind != RM_SECRET
                            || (rn->flags & SECRET_BIT)) continue;
                        if ((d == DIR_N && cy < 90) ||
                            (d == DIR_S && cy > ROOM_H * 2 - 90) ||
                            (d == DIR_W && cx < 90) ||
                            (d == DIR_E && cx > ROOM_W * 2 - 90))
                            rn->flags |= SECRET_BIT;
                    }
                    gy = MAP_H; /* sortir des boucles */
                    break;
                }
    }
}

/* ---------- update principal ---------- */

uint8_t play_update(Game *g, Isaac *iz, const Input *in, uint8_t *dir)
{
    RoomState *r = cur_room(g);
    uint8_t i, j;
    uint8_t combat = (n_en > 0);
    int16_t sp = iz->speed;

    *dir = 0;
    if (hitstop) { hitstop--; return EV_NONE; }
    if (boss_intro) { boss_intro--; return EV_NONE; }
    if (shake) {
        shake--;
        shx = (uint8_t)(shake & 1);
        shy = (uint8_t)((shake >> 1) & 1);
    } else {
        shx = shy = 0;
    }
    if (iz->iframe) iz->iframe--;
    if (iz->fire_cd) iz->fire_cd--;

    /* --- direction visee --- */
    if (in->up)    iz->aim = DIR_N;
    if (in->down)  iz->aim = DIR_S;
    if (in->left)  iz->aim = DIR_W;
    if (in->right) iz->aim = DIR_E;

    /* --- deplacement --- */
    {
        int16_t mx = 0, my = 0, d;

        if (in->up)    my = -1;
        if (in->down)  my = 1;
        if (in->left)  mx = -1;
        if (in->right) mx = 1;

        if (mx || my) iz->anim++;
        else iz->anim = 0;
        {
            /* accumulateurs en 1/8 de demi-px : mouvement fin,
               diagonale ralentie (x0.75) au lieu d'un frame saute sur 4 */
            int16_t rate = (mx && my) ? (int16_t)(sp * 3 / 2)
                                      : (int16_t)(sp * 2);
            if (mx) {
                iz->maccx = (int16_t)(iz->maccx + (mx > 0 ? rate : -rate));
                d = (int16_t)(iz->maccx / 8);
                iz->maccx = (int16_t)(iz->maccx - d * 8);
                if (d) iz->x = move_axis(g, iz->x, iz->y, 12, d);
            } else {
                iz->maccx = 0;
            }
            if (my) {
                iz->maccy = (int16_t)(iz->maccy + (my > 0 ? rate : -rate));
                d = (int16_t)(iz->maccy / 8);
                iz->maccy = (int16_t)(iz->maccy - d * 8);
                if (d) iz->y = move_axis(g, iz->y, iz->x, 12, d);
            } else {
                iz->maccy = 0;
            }
        }

        /* bords : combat = bloque, sinon sortie par la porte */
        if (combat) {
            if (iz->x < 10) iz->x = 10;
            if (iz->x > ROOM_W * 2 - 10) iz->x = (int16_t)(ROOM_W * 2 - 10);
            if (iz->y < 10) iz->y = 10;
            if (iz->y > ROOM_H * 2 - 10) iz->y = (int16_t)(ROOM_H * 2 - 10);
        } else {
            uint8_t in_ns = (uint8_t)(iz->x > 264 && iz->x < 344);
            uint8_t in_we = (uint8_t)(iz->y > 200 && iz->y < 280);

            /* nord */
            if (iz->y < 10 && in_ns && has_door(g, DIR_N)) {
                if (iz->y < 4) {
                    if (open_locked(g, iz, DIR_N)) { *dir = DIR_N; return EV_TRANSIT; }
                    iz->y = 4;
                }
            } else if (iz->y < 10) {
                iz->y = 10;
            }
            /* sud */
            if (iz->y > ROOM_H * 2 - 10 && in_ns && has_door(g, DIR_S)) {
                if (iz->y > ROOM_H * 2 - 4) {
                    if (open_locked(g, iz, DIR_S)) { *dir = DIR_S; return EV_TRANSIT; }
                    iz->y = (int16_t)(ROOM_H * 2 - 4);
                }
            } else if (iz->y > ROOM_H * 2 - 10) {
                iz->y = (int16_t)(ROOM_H * 2 - 10);
            }
            /* ouest */
            if (iz->x < 10 && in_we && has_door(g, DIR_W)) {
                if (iz->x < 4) {
                    if (open_locked(g, iz, DIR_W)) { *dir = DIR_W; return EV_TRANSIT; }
                    iz->x = 4;
                }
            } else if (iz->x < 10) {
                iz->x = 10;
            }
            /* est */
            if (iz->x > ROOM_W * 2 - 10 && in_we && has_door(g, DIR_E)) {
                if (iz->x > ROOM_W * 2 - 4) {
                    if (open_locked(g, iz, DIR_E)) { *dir = DIR_E; return EV_TRANSIT; }
                    iz->x = (int16_t)(ROOM_W * 2 - 4);
                }
            } else if (iz->x > ROOM_W * 2 - 10) {
                iz->x = (int16_t)(ROOM_W * 2 - 10);
            }
        }
    }

    /* --- tir : vise et tire avec 8 / 2 / 4 / 6 --- */
    {
        int8_t fdx = 0, fdy = 0;

        if (in->f_up)         { fdy = -1; iz->aim = DIR_N; }
        else if (in->f_down)  { fdy = 1;  iz->aim = DIR_S; }
        else if (in->f_left)  { fdx = -1; iz->aim = DIR_W; }
        else if (in->f_right) { fdx = 1;  iz->aim = DIR_E; }

        /* [5] : tir dans la direction du dernier deplacement */
        if (!fdx && !fdy && in->f_center) {
            switch (iz->aim) {
            case DIR_S: fdy = 1;  break;
            case DIR_W: fdx = -1; break;
            case DIR_E: fdx = 1;  break;
            default:    fdy = -1; break;
            }
        }

        if ((fdx || fdy) && !iz->fire_cd) {
            spawn_tear(g, iz, fdx, fdy);
            if (iz->triple) {
                if (fdx) {
                    spawn_tear(g, iz, fdx, -1);
                    spawn_tear(g, iz, fdx, 1);
                } else {
                    spawn_tear(g, iz, -1, fdy);
                    spawn_tear(g, iz, 1, fdy);
                }
            }
            iz->fire_cd = iz->fire_delay;
        }
    }

    /* --- objet actif : TELE (touche STAT) --- */
    if (in->active && tele_charge >= TELE_MAX) {
        tele_charge = 0;
        return EV_TELE;
    }

    /* --- bombe posee --- */
    if (in->bomb && iz->bombs > 0 && n_bm < MAX_BOMBS) {
        bm[n_bm].x = iz->x; bm[n_bm].y = iz->y; bm[n_bm].fuse = 45;
        n_bm++;
        iz->bombs--;
    }

    for (i = 0; i < n_bm; ) {
        bm[i].fuse--;
        if (bm[i].fuse == 0) {
            explode(g, iz, bm[i].x, bm[i].y);
            bm[i] = bm[n_bm - 1];
            n_bm--;
            continue;
        }
        i++;
    }

    /* --- larmes --- */
    for (i = 0; i < n_tr; ) {
        Tear *t = &tr[i];
        t->x = (int16_t)(t->x + t->dx);
        t->y = (int16_t)(t->y + t->dy);
        if (t->ttl) t->ttl--;
        if (t->x < 4 || t->x > ROOM_W * 2 - 4 ||
            t->y < 4 || t->y > ROOM_H * 2 - 4 || !t->ttl) {
            t->ttl = 0; /* morte */
        } else if (tear_passes(g, t)) {
            /* au-dessus d'une fosse ou de pics : la larme traverse */
        } else if (solid_at(g, (uint8_t)(t->x >> 5), (uint8_t)(t->y >> 5))) {
            Obst *vo = obst_at_rw(g, (uint8_t)(t->x >> 5),
                                 (uint8_t)(t->y >> 5));
            if (vo && vo->type == OB_POOP) {
                vo->type = OB_NONE;
                burst(t->x, t->y, 8, COL_FLOOR2_DK);
                if (rng_range(8) == 0)
                    room_give_pick(cur_room(g), PK_COIN, vo->tx, vo->ty);
            } else if (vo && vo->type == OB_BONES) {
                vo->type = OB_NONE;
                burst(t->x, t->y, 8, COL_OFFWHITE);
            }
            t->ttl = 0;
        } else if (t->hostile) {
            int16_t ddx = (int16_t)(t->x - iz->x), ddy = (int16_t)(t->y - iz->y);
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (ddx < 14 && ddy < 16) {
                int8_t kdx = t->dx ? (int8_t)((t->dx > 0) ? -10 : 10) : 0;
                int8_t kdy = t->dy ? (int8_t)((t->dy > 0) ? -10 : 10) : 0;
                hurt_isaac_k(g, iz, t->dmg, kdx, kdy);
                t->ttl = 0;
            }
        } else {
            for (j = 0; j < n_en; j++) {
                int16_t lim = (en[j].kind == EN_MONSTRO
                               || en[j].kind == EN_GEMINI) ? 30
                             : (en[j].kind == EN_MAMAN) ? 34 : 15;
                int16_t ddx = (int16_t)(t->x - en[j].x);
                int16_t ddy = (int16_t)(t->y - en[j].y);
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (t->dmg >= 10 || (iz->has_item & (1UL << IT_MASSE)))
                    lim = (int16_t)(lim + 4);
                if (ddx < lim && ddy < lim && !t->hitcd) {
                    if (en[j].hp <= t->dmg) en[j].hp = 0;
                    else en[j].hp = (uint16_t)(en[j].hp - t->dmg);
                    en[j].flash = 6;
                    if (iz->has_item & (1UL << IT_VENIN))
                        en[j].poison = 120;
                    if (en[j].kind != EN_MONSTRO && en[j].kind != EN_GEMINI
                        && en[j].kind != EN_MAMAN
                        && (en[j].x > 40 || en[j].y > 40)) {
                        /* knockback : sauf sur les bords de salle { */
                        uint8_t kb = (iz->has_item & (1UL << IT_SOUFFLE))
                                   ? 12 : 6;
                        if (t->dx > 0)      en[j].kx = (int8_t)(en[j].kx + kb);
                        else if (t->dx < 0) en[j].kx = (int8_t)(en[j].kx - kb);
                        if (t->dy > 0)      en[j].ky = (int8_t)(en[j].ky + kb);
                        else if (t->dy < 0) en[j].ky = (int8_t)(en[j].ky - kb);
                        if (en[j].kx > 14)  en[j].kx = 14;
                        if (en[j].kx < -14) en[j].kx = -14;
                        if (en[j].ky > 14)  en[j].ky = 14;
                        if (en[j].ky < -14) en[j].ky = -14;
                    }
                    if (iz->has_item & (1UL << IT_AIGUILLE))
                        t->hitcd = 8;   /* tir perceant : la larme survit */
                    else
                        t->ttl = 0;
                    break;
                }
            }
        }
        if (!t->ttl) {
            if (!t->hostile) burst(t->x, t->y, 2, COL_TEAR);
            *t = tr[n_tr - 1];
            n_tr--;
            continue;
        }
        i++;
    }

    /* --- ennemis --- */
    for (i = 0; i < n_en; ) {
        Enemy *e = &en[i];
        if (e->flash) e->flash--;
        e->t++;

        /* poison (VENIN) : degats reguliers */
        if (e->poison) {
            e->poison--;
            if ((e->poison % 30) == 0 && e->hp > 1)
                e->hp = (uint16_t)(e->hp - 1);
        }
        /* knockback recu : glisse puis s'arrete */
        if (e->kx || e->ky) {
            e->x = move_axis(g, e->x, e->y, 10, e->kx);
            e->y = move_axis(g, e->y, e->x, 10, e->ky);
            e->kx = (int8_t)(e->kx / 2);
            e->ky = (int8_t)(e->ky / 2);
        }
        switch (e->kind) {
        case EN_GAPER: {
            int16_t ddx = (int16_t)(iz->x - e->x), ddy = (int16_t)(iz->y - e->y);
            int ax = ddx < 0 ? -ddx : ddx, ay = ddy < 0 ? -ddy : ddy;
            int16_t half = 10;
            if (ax > ay) {
                int8_t d = ddx > 0 ? 1 : -1;
                e->x = move_axis(g, e->x, e->y, half, (int16_t)(d * 2));
                e->y = move_axis(g, e->y, e->x, half, ddy > 0 ? 1 : -1);
            } else {
                int8_t d = ddy > 0 ? 1 : -1;
                e->y = move_axis(g, e->y, e->x, half, (int16_t)(d * 2));
                e->x = move_axis(g, e->x, e->y, half, ddx > 0 ? 1 : -1);
            }
            break;
        }
        case EN_FLY:
            if ((e->t & 15) == 0) {
                e->dx = (int8_t)rng_range(4);
                if (rng_range(2)) e->dx = (int8_t)(-e->dx);
                e->dy = (int8_t)rng_range(4);
                if (rng_range(2)) e->dy = (int8_t)(-e->dy);
            }
            e->x = (int16_t)(e->x + e->dx);
            e->y = (int16_t)(e->y + e->dy);
            if (e->x < 8)   { e->x = 8;   e->dx = 4; }
            if (e->x > ROOM_W * 2 - 8) { e->x = (int16_t)(ROOM_W * 2 - 8); e->dx = -4; }
            if (e->y < 8)   { e->y = 8;   e->dy = 4; }
            if (e->y > ROOM_H * 2 - 8) { e->y = (int16_t)(ROOM_H * 2 - 8); e->dy = -4; }
            break;
        case EN_POOTER: {
            int8_t dx, dy;
            aim8(e->x, e->y, iz->x, iz->y, &dx, &dy);
            e->x = (int16_t)(e->x + dx);
            e->y = (int16_t)(e->y + dy);
            if (e->shoot_cd) e->shoot_cd--;
            else {
                aim8(e->x, e->y, iz->x, iz->y, &dx, &dy);
                spawn_eshot(e->x, e->y, dx, dy, 1, 8);
                e->shoot_cd = (uint8_t)(90 + rng_range(60));
            }
            break;
        }
        case EN_HORF:
            if (e->shoot_cd) e->shoot_cd--;
            else {
                int8_t dx, dy;
                aim8(e->x, e->y, iz->x, iz->y, &dx, &dy);
                spawn_eshot(e->x, e->y, dx, dy, 1, 8);
                e->shoot_cd = (uint8_t)(75 + rng_range(30));
            }
            break;
        case EN_MONSTRO:
            switch (e->st) {
            case 0: /* idle */
                if (e->shoot_cd) e->shoot_cd--;
                else {
                    uint8_t roll = rng_range(100);
                    if (roll < 55) { e->st = 1; e->t = 0; } /* telegraph */
                    else { e->st = 3; e->t = 0; }           /* spit */
                    /*NOP*/
                }
                break;
            case 1: /* telegraph squash */
                if (e->t >= 20) {
                    e->st = 2; e->t = 0;
                    e->dx = (int8_t)((iz->x - e->x) / 20);
                    e->dy = (int8_t)((iz->y - e->y) / 20);
                }
                break;
            case 2: /* saut */
                e->x = (int16_t)(e->x + e->dx);
                e->y = (int16_t)(e->y + e->dy);
                if (e->x < 40)  { e->x = 40;  e->dx = 0; }
                if (e->x > ROOM_W * 2 - 40) { e->x = (int16_t)(ROOM_W * 2 - 40); e->dx = 0; }
                if (e->y < 40)  { e->y = 40;  e->dy = 0; }
                if (e->y > ROOM_H * 2 - 40) { e->y = (int16_t)(ROOM_H * 2 - 40); e->dy = 0; }
                if (e->t >= 24) { e->st = 0; e->t = 0; e->shoot_cd = (uint8_t)(30 + rng_range(40)); }
                break;
            default: /* spit */
                if (e->t == 6 || e->t == 12 || e->t == 18) {
                    int8_t dx, dy;
                    aim8(e->x, e->y, iz->x, iz->y, &dx, &dy);
                    spawn_eshot(e->x, e->y, dx, dy, 1, 9);
                }
                if (e->t >= 26) {
                    if (e->hp < 250 && g->floor == 1) {
                        /* salve radiale sous 50% (floor 1: hp<250) */
                        uint8_t k;
                        int8_t vx[8] = {1,1,1,0,-1,-1,-1,0};
                        int8_t vy[8] = {-1,0,1,1,1,0,-1,-1};
                        for (k = 0; k < 8; k++)
                            spawn_eshot(e->x, e->y, vx[k], vy[k], 1, 7);
                        e->shoot_cd = (uint8_t)(50 + rng_range(40));
                    } else if (g->floor == 2 && e->hp < 400) {
                        uint8_t k;
                        int8_t vx[8] = {1,1,1,0,-1,-1,-1,0};
                        int8_t vy[8] = {-1,0,1,1,1,0,-1,-1};
                        for (k = 0; k < 8; k++)
                            spawn_eshot(e->x, e->y, vx[k], vy[k], 1, 7);
                        e->shoot_cd = (uint8_t)(50 + rng_range(40));
                    } else {
                        e->shoot_cd = (uint8_t)(30 + rng_range(40));
                    }
                    e->st = 0; e->t = 0;
                }
                break;
            }
            break;
        case EN_CHARGER:
            /* charge dans la direction dominante puis se recale */
            if (e->st == 0) {
                if (e->t >= 40) {
                    int16_t ddx = (int16_t)(iz->x - e->x);
                    int16_t ddy = (int16_t)(iz->y - e->y);
                    int ax = ddx < 0 ? -ddx : ddx, ay = ddy < 0 ? -ddy : ddy;
                    if (ax > ay) { e->dx = ddx > 0 ? 4 : -4; e->dy = 0; }
                    else        { e->dy = ddy > 0 ? 4 : -4; e->dx = 0; }
                    e->st = 1;
                }
            } else {
                int16_t ox = e->x, oy = e->y;
                e->x = move_axis(g, e->x, e->y, 10, e->dx);
                e->y = move_axis(g, e->y, e->x, 10, e->dy);
                if (e->x == ox && e->y == oy) { e->st = 0; e->t = 0; }
            }
            break;
        case EN_SPIDER:
            /* avances saccadees dans une direction aleatoire */
            if ((e->t & 7) == 0) {
                e->dx = (int8_t)(rng_range(5) - 2);
                e->dy = (int8_t)(rng_range(5) - 2);
            }
            e->x = move_axis(g, e->x, e->y, 9, e->dx);
            e->y = move_axis(g, e->y, e->x, 9, e->dy);
            if (e->x < 12)  e->x = 12;
            if (e->x > ROOM_W * 2 - 12) e->x = (int16_t)(ROOM_W * 2 - 12);
            if (e->y < 12)  e->y = 12;
            if (e->y > ROOM_H * 2 - 12) e->y = (int16_t)(ROOM_H * 2 - 12);
            break;
        case EN_GEMINI: {
            const Enemy *partner = NULL;
            uint8_t k2;
            for (k2 = 0; k2 < n_en; k2++) {
                if (k2 != i && en[k2].kind == EN_GEMINI) {
                    partner = &en[k2];
                    break;
                }
            }
            if (e->st == 0) {
                if (e->t >= (partner ? 30 : 18)) {
                    int16_t ddx = (int16_t)(iz->x - e->x);
                    int16_t ddy = (int16_t)(iz->y - e->y);
                    int ax = ddx < 0 ? -ddx : ddx, ay = ddy < 0 ? -ddy : ddy;
                    if (ax > ay) { e->dx = ddx > 0 ? 5 : -5; e->dy = 0; }
                    else        { e->dy = ddy > 0 ? 5 : -5; e->dx = 0; }
                    e->st = 1;
                }
            } else {
                int16_t ox = e->x, oy = e->y;
                e->x = move_axis(g, e->x, e->y, 14, e->dx);
                e->y = move_axis(g, e->y, e->x, 14, e->dy);
                if (e->x == ox && e->y == oy) { e->st = 0; e->t = 0; }
            }
            if (e->x < 40)  { e->x = 40;  e->st = 0; e->t = 0; }
            if (e->x > ROOM_W * 2 - 40) { e->x = (int16_t)(ROOM_W * 2 - 40); e->st = 0; e->t = 0; }
            if (e->y < 40)  { e->y = 40;  e->st = 0; e->t = 0; }
            if (e->y > ROOM_H * 2 - 40) { e->y = (int16_t)(ROOM_H * 2 - 40); e->st = 0; e->t = 0; }
            if (e->shoot_cd) e->shoot_cd--;
            else {
                int8_t dx, dy;
                aim8(e->x, e->y, iz->x, iz->y, &dx, &dy);
                spawn_eshot(e->x, e->y, dx, dy, 1, 8);
                e->shoot_cd = (uint8_t)(80 + rng_range(40));
            }
            break;
        }
        case EN_SAUTEUR:
            /* attend, vibre, puis bondit sur Isaac en passant au-dessus
               des obstacles */
            if (e->st == 0) {
                if (e->shoot_cd) e->shoot_cd--;
                else { e->st = 1; e->t = 0; }
            } else if (e->st == 1) {
                if (e->t >= 18) {
                    int16_t ddx = (int16_t)(iz->x - e->x);
                    int16_t ddy = (int16_t)(iz->y - e->y);
                    e->dx = (int8_t)(ddx / 18);
                    e->dy = (int8_t)(ddy / 18);
                    if (e->dx > 6)  e->dx = 6;
                    if (e->dx < -6) e->dx = -6;
                    if (e->dy > 6)  e->dy = 6;
                    if (e->dy < -6) e->dy = -6;
                    if (!e->dx && !e->dy) e->dy = -6;
                    e->st = 2; e->t = 0;
                }
            } else {
                e->x = (int16_t)(e->x + e->dx);
                e->y = (int16_t)(e->y + e->dy);
                if (e->x < 14) { e->x = 14; e->dx = 0; }
                if (e->x > ROOM_W * 2 - 14) { e->x = (int16_t)(ROOM_W * 2 - 14); e->dx = 0; }
                if (e->y < 14) { e->y = 14; e->dy = 0; }
                if (e->y > ROOM_H * 2 - 14) { e->y = (int16_t)(ROOM_H * 2 - 14); e->dy = 0; }
                if (e->t >= 14) {
                    e->st = 0; e->t = 0;
                    e->shoot_cd = (uint8_t)(25 + rng_range(35));
                }
            }
            break;
        case EN_CROIX:
            /* statue fixe : salves alternant croix droite et diagonales */
            if (e->shoot_cd) e->shoot_cd--;
            else {
                if (e->st & 1) {
                    spawn_eshot(e->x, e->y,  1,  1, 1, 6);
                    spawn_eshot(e->x, e->y, -1,  1, 1, 6);
                    spawn_eshot(e->x, e->y,  1, -1, 1, 6);
                    spawn_eshot(e->x, e->y, -1, -1, 1, 6);
                } else {
                    spawn_eshot(e->x, e->y,  1,  0, 1, 7);
                    spawn_eshot(e->x, e->y, -1,  0, 1, 7);
                    spawn_eshot(e->x, e->y,  0,  1, 1, 7);
                    spawn_eshot(e->x, e->y,  0, -1, 1, 7);
                }
                e->st = (uint8_t)((e->st + 1) & 1);
                e->shoot_cd = (uint8_t)(85 + rng_range(40));
            }
            break;
        case EN_MAMAN:
            /* boss final : charge, double salve radiale, invocation */
            switch (e->st) {
            case 0:
                if (e->shoot_cd) e->shoot_cd--;
                else {
                    uint8_t roll = rng_range(100);
                    if      (roll < 40) { e->st = 1; e->t = 0; }
                    else if (roll < 70) { e->st = 3; e->t = 0; }
                    else               { e->st = 4; e->t = 0; }
                }
                break;
            case 1:
                if (e->t >= 24) {
                    e->st = 2; e->t = 0;
                    e->dx = (int8_t)((iz->x - e->x) / 22);
                    e->dy = (int8_t)((iz->y - e->y) / 22);
                    if (e->dx > 7)  e->dx = 7;
                    if (e->dx < -7) e->dx = -7;
                    if (e->dy > 7)  e->dy = 7;
                    if (e->dy < -7) e->dy = -7;
                }
                break;
            case 2:
                e->x = (int16_t)(e->x + e->dx);
                e->y = (int16_t)(e->y + e->dy);
                if (e->x < 56) { e->x = 56; e->dx = 0; }
                if (e->x > ROOM_W * 2 - 56) { e->x = (int16_t)(ROOM_W * 2 - 56); e->dx = 0; }
                if (e->y < 56) { e->y = 56; e->dy = 0; }
                if (e->y > ROOM_H * 2 - 56) { e->y = (int16_t)(ROOM_H * 2 - 56); e->dy = 0; }
                if (e->t >= 24) {
                    e->st = 0; e->t = 0;
                    e->shoot_cd = (uint8_t)(20 + rng_range(30));
                }
                break;
            case 3:
                if (e->t == 10) {
                    spawn_eshot(e->x, e->y,  1,  0, 1, 8);
                    spawn_eshot(e->x, e->y, -1,  0, 1, 8);
                    spawn_eshot(e->x, e->y,  0,  1, 1, 8);
                    spawn_eshot(e->x, e->y,  0, -1, 1, 8);
                }
                if (e->t == 21) {
                    spawn_eshot(e->x, e->y,  1,  1, 1, 6);
                    spawn_eshot(e->x, e->y, -1,  1, 1, 6);
                    spawn_eshot(e->x, e->y,  1, -1, 1, 6);
                    spawn_eshot(e->x, e->y, -1, -1, 1, 6);
                }
                if (e->t >= 32) {
                    e->st = 0; e->t = 0;
                    e->shoot_cd = (uint8_t)(30 + rng_range(30));
                }
                break;
            default:
                if (e->t == 14) {
                    uint8_t k;
                    for (k = 0; k < 2 && n_en < MAX_ENEMIES; k++) {
                        en[n_en].kind = EN_FLY;
                        en[n_en].hp = 16;
                        en[n_en].x = (int16_t)(e->x + (k ? 40 : -40));
                        en[n_en].y = (int16_t)(e->y - 24);
                        en[n_en].flash = 0; en[n_en].t = 0; en[n_en].st = 0;
                        en[n_en].dx = 4; en[n_en].dy = 0;
                        en[n_en].shoot_cd = 0;
                        en[n_en].champ = 0; en[n_en].poison = 0;
                        en[n_en].kx = 0; en[n_en].ky = 0;
                        n_en++;
                    }
                }
                if (e->t >= 26) {
                    e->st = 0; e->t = 0;
                    e->shoot_cd = (uint8_t)(40 + rng_range(40));
                }
                break;
            }
            break;
        default:
            break;
        }

        /* MALEDICTION HATE : les ennemis pressent le pas */
        if (g->curse & CURSE_HASTE) {
            if (e->shoot_cd) e->shoot_cd--;
            if (e->kind == EN_GAPER && (e->t & 3) == 3) {
                int16_t ddx = (int16_t)(iz->x - e->x);
                int16_t ddy = (int16_t)(iz->y - e->y);
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (ddx >= ddy)
                    e->x = move_axis(g, e->x, e->y, 10,
                                     (int16_t)(ddx > 0 ? 2 : -2));
                else
                    e->y = move_axis(g, e->y, e->x, 10,
                                     (int16_t)(ddy > 0 ? 2 : -2));
            }
        }

        /* contact */
        {
            int16_t lim = (e->kind == EN_MONSTRO || e->kind == EN_GEMINI) ? 40
                        : (e->kind == EN_MAMAN) ? 46 : 14;
            int16_t ddx = (int16_t)(iz->x - e->x), ddy = (int16_t)(iz->y - e->y);
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (ddx < lim && ddy < lim) {
                uint8_t dmg = (uint8_t)((e->kind == EN_MONSTRO
                                         || e->kind == EN_GEMINI
                                         || e->kind == EN_MAMAN) ? 2 : 1);
                int8_t kdx = 0, kdy = 0;
                if (iz->x > e->x) kdx = 10;
                else if (iz->x < e->x) kdx = -10;
                if (iz->y > e->y) kdy = 8;
                else if (iz->y < e->y) kdy = -8;
                hurt_isaac_k(g, iz, dmg, kdx, kdy);
            }
        }

        /* mort */
        if (e->hp == 0) {
            uint8_t bigboss = (uint8_t)((e->kind == EN_MONSTRO
                                         || e->kind == EN_GEMINI
                                         || e->kind == EN_MAMAN) ? 4 : 2);
            g->kills++;
            if (tele_charge < TELE_MAX) tele_charge++;
            hitstop = bigboss;
            shake = (uint8_t)((bigboss > 2) ? 14 : 4);
            spawn_decal(e->x, e->y);
            if (bigboss > 2)
                burst(e->x, e->y, 16, COL_BLOOD);
            else
                burst(e->x, e->y, 6, COL_BLOOD);
            if (e->champ) burst(e->x, e->y, 8, COL_GOLD);
            /* drop */
            {
                RoomState *rr = cur_room(g);
                uint8_t roll = rng_range(100);
                uint8_t type = PK_NONE;
                /* TREFLE : la chance decale la table de butin */
                if ((iz->has_item & (1UL << IT_TREFLE)) && roll >= 12)
                    roll = (uint8_t)(roll - 12);
                if (roll < 20) type = PK_COIN;
                else if (roll < 28) type = PK_BOMB;
                else if (roll < 36) type = PK_HALF;
                else if (roll < 44) type = PK_KEY;
                else if (roll < 46) type = PK_HEART;
                else if (roll < 50) type = PK_SILVER;
                else if (roll < 53) type = PK_SOUL;
                if (e->champ && type == PK_NONE) type = PK_COIN;
                if (type != PK_NONE)
                    room_give_pick(rr, type, (uint8_t)(e->x >> 5),
                                   (uint8_t)(e->y >> 5));
                if (e->kind == EN_MONSTRO || e->kind == EN_GEMINI) {
                    /* butin de boss garanti */
                    room_give_pick(rr, PK_HEART, 8, 7);
                    room_give_pick(rr, PK_COIN, 7, 6);
                    room_give_pick(rr, PK_COIN, 9, 6);
                    if (rng_range(100) < 30)
                        room_give_pick(rr, PK_SILVER, 8, 5);
                    /* pacte du diable debloque si l'etage est sans degats */
                    {
                        uint8_t k2;
                        for (k2 = 0; k2 < g->n_rooms; k2++) {
                            if (g->rooms[k2].kind == RM_DEVIL
                                && !(g->rooms[k2].flags & 4)) {
                                if ((g->dmg_floor == 0
                                     && rng_range(100) < 60)
                                    || rng_range(100) < 15) {
                                    g->rooms[k2].flags |= 4;
                                    spawn_pop(iz->x, iz->y - 24, "DIABLE!",
                                              COL_BLOOD);
                                    hitstop = 8;
                                }
                                break;
                            }
                        }
                    }
                }
            }
            *e = en[n_en - 1];
            n_en--;
            continue;
        }
        i++;
    }    /* --- spikes / feux --- */
    {
        const Obst *o = obst_at(g, (uint8_t)(iz->x >> 5), (uint8_t)(iz->y >> 5));
        if (o && o->type == OB_SPIKE) {
            /* salle de sacrifice : le pic central donne un reward */
            if (r->kind == RM_SACRI && o->tx == 8 && o->ty == 6
                && !iz->iframe) {
                uint8_t rew;
                r->count++;
                if      (r->count == 1) rew = PK_COIN;
                else if (r->count == 2) rew = PK_KEY;
                else if (r->count == 3) rew = PK_CHEST;
                else if (r->count == 4) rew = PK_HEART;
                else                    rew = PK_SILVER;
                room_give_pick(r, rew, 8, 7);
                spawn_pop(o->tx * 32 + 16, o->ty * 32 - 8,
                          (rew == PK_CHEST) ? "COFFRE!" : "OFFRANDE",
                          COL_GOLD);
                spawn_decal(o->tx * 32 + 16, o->ty * 32 + 16);
            }
            hurt_isaac(g, iz, 1);
        }

        /* cactus : piquent au contact rapproche */
        for (j = 0; j < r->n_obst; j++) {
            const Obst *oc = &r->obst[j];
            int16_t ocx = (int16_t)(oc->tx * 32 + 16);
            int16_t ocy = (int16_t)(oc->ty * 32 + 16);
            int16_t ddx = (int16_t)(iz->x - ocx), ddy = (int16_t)(iz->y - ocy);
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (oc->type == OB_CACTUS && ddx < 34 && ddy < 34)
                hurt_isaac(g, iz, 1);
        }
    }

    /* --- particules --- */
    for (i = 0; i < n_pt; ) {
        pt[i].x = (int16_t)(pt[i].x + pt[i].dx);
        pt[i].y = (int16_t)(pt[i].y + pt[i].dy);
        if (pt[i].ttl) pt[i].ttl--;
        if (!pt[i].ttl) {
            pt[i] = pt[n_pt - 1];
            n_pt--;
            continue;
        }
        i++;
    }

    /* --- machine a dons (boutique) --- */
    if (r->kind == RM_SHOP) {
        static uint8_t don_cd;
        if (don_cd) don_cd--;
        else {
            for (j = 0; j < r->n_obst; j++) {
                if (r->obst[j].type != OB_STOCK) continue;
                {
                    int16_t mx = (int16_t)(r->obst[j].tx * 32 + 16);
                    int16_t my = (int16_t)(r->obst[j].ty * 32 + 16);
                    int16_t ddx = (int16_t)(iz->x - mx);
                    int16_t ddy = (int16_t)(iz->y - my);
                    if (ddx < 0) ddx = -ddx;
                    if (ddy < 0) ddy = -ddy;
                    if (ddx < 62 && ddy < 62 && iz->coins) {
                        iz->coins--;
                        r->count++;
                        spawn_pop(mx, my - 18, "DON", COL_GOLD);
                        don_cd = 22;
                        if (rng_range(100) < 22) {
                            uint8_t roll = rng_range(100);
                            uint8_t type = (roll < 45) ? PK_HALF
                                         : (roll < 75) ? PK_KEY
                                         : (roll < 93) ? PK_BOMB
                                         : PK_SILVER;
                            room_give_pick(r, type, r->obst[j].tx,
                                           (uint8_t)(r->obst[j].ty - 1));
                            spawn_pop(mx, my - 32, "MERCI !", COL_OFFWHITE);
                        }
                        if (r->count == 10) {
                            room_give_pick(r, PK_CHEST, r->obst[j].tx,
                                           (uint8_t)(r->obst[j].ty - 1));
                            spawn_pop(mx, my - 32, "JACKPOT", COL_GOLD);
                        }
                    }
                }
                break;
            }
        }
    }

    /* --- pickups (gratuits ou en vente en boutique) --- */
    if (r->n_pick) {
        for (i = 0; i < r->n_pick; i++) {
            Pickup *p = &r->pick[i];
            int16_t ddx, ddy;
            uint8_t ok;
            if (!p->type || p->type == PK_TRAP) continue;
            /* aimant : le butin glisse vers Isaac */
            if (!p->price && p->type != PK_CHEST && p->type != PK_LOCKED
                && (iz->has_item & (1UL << IT_AIMANT))) {
                int16_t mx2 = (int16_t)(iz->x - p->x);
                int16_t my2 = (int16_t)(iz->y - p->y);
                if (mx2 > -192 && mx2 < 192 && my2 > -192 && my2 < 192) {
                    if (mx2 > 2) p->x = (int16_t)(p->x + 2);
                    else if (mx2 < -2) p->x = (int16_t)(p->x - 2);
                    if (my2 > 2) p->y = (int16_t)(p->y + 2);
                    else if (my2 < -2) p->y = (int16_t)(p->y - 2);
                }
            }
            ddx = (int16_t)(iz->x - p->x); ddy = (int16_t)(iz->y - p->y);
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (ddx >= 20 || ddy >= 20) continue;
            /* coffres : ouverture au contact (verrouille = cle requise) */
            if (p->type == PK_CHEST || p->type == PK_LOCKED) {
                if (p->type == PK_LOCKED) {
                    if (!iz->keys) continue;
                    iz->keys--;
                    drop_loot(g, r, p->tx, p->ty, 1);
                } else if (rng_range(100) < 20 && n_en + 2 <= MAX_ENEMIES) {
                    /* coffre piege : deux mouches ! */
                    uint8_t k;
                    for (k = 0; k < 2; k++) {
                        en[n_en].kind = EN_FLY;
                        en[n_en].hp = 12;
                        en[n_en].x = (int16_t)(p->x + (k ? 24 : -24));
                        en[n_en].y = (int16_t)(p->y - 16);
                        en[n_en].flash = 0; en[n_en].t = 0; en[n_en].st = 0;
                        en[n_en].dx = 3; en[n_en].dy = 3;
                        en[n_en].shoot_cd = 0;
                        n_en++;
                    }
                    spawn_pop(p->x, p->y - 24, "PIEGE !", COL_BLOOD);
                } else {
                    drop_loot(g, r, p->tx, p->ty, 0);
                }
                burst(p->x, p->y, 8, COL_GOLD);
                p->type = PK_NONE;
                continue;
            }
            /* pacte du diable : paye en demi-coeurs */
            if (p->type == PK_DEVIL) {
                if (iz->hp > p->price) {
                    uint8_t it = (p->tx <= 8) ? IT_PACTE : IT_PECHE;
                    uint8_t k;
                    iz->hp = (uint8_t)(iz->hp - p->price);
                    play_apply_item(iz, it);
                    iz->has_item |= (uint32_t)1UL << it;
                    spawn_banner(item_name(it), item_desc(it));
                    for (k = 0; k < 3; k++) spawn_decal(p->x, p->y);
                    burst(p->x, p->y, 10, COL_BLOOD);
                    p->type = PK_NONE;
                    hitstop = 4;
                }
                continue;
            }
            ok = 1;
            if (p->price && iz->coins < p->price) ok = 0;
            if (ok && (p->type == PK_HEART || p->type == PK_HALF)
                && iz->hp >= iz->hp_max) ok = 0;
            if (ok && p->type == PK_SOUL && iz->hp_soul >= 8) ok = 0;
            if (ok) {
                if (p->price)
                    iz->coins = (uint8_t)(iz->coins - p->price);
                switch (p->type) {
                case PK_HEART:
                    if (iz->hp + 2 <= iz->hp_max) iz->hp += 2; else iz->hp = iz->hp_max;
                    spawn_pop(p->x, p->y - 16, "+PV", COL_BLOOD);
                    break;
                case PK_HALF:
                    iz->hp++;
                    if (iz->hp > iz->hp_max) iz->hp = iz->hp_max;
                    spawn_pop(p->x, p->y - 16, "+1/2", COL_BLOOD);
                    break;
                case PK_COIN:
                    if (iz->coins < 99) iz->coins++;
                    spawn_pop(p->x, p->y - 16, "+1", COL_GOLD);
                    break;
                case PK_KEY:
                    if (iz->keys < 9) iz->keys++;
                    spawn_pop(p->x, p->y - 16, "+CLE", COL_OFFWHITE);
                    break;
                case PK_BOMB:
                    if (iz->bombs < 9) iz->bombs++;
                    spawn_pop(p->x, p->y - 16, "+BOMBE", COL_OFFWHITE);
                    break;
                case PK_SILVER:
                    iz->coins = (iz->coins > 94) ? 99 :
                                (uint8_t)(iz->coins + 5);
                    spawn_pop(p->x, p->y - 16, "+5", COL_GOLD);
                    break;
                case PK_SOUL:
                    iz->hp_soul = (uint8_t)(iz->hp_soul + 2);
                    if (iz->hp_soul > 8) iz->hp_soul = 8;
                    spawn_pop(p->x, p->y - 16, "+AME", COL_TEAR);
                    break;
                default: break;
                }
                p->type = PK_NONE;
            }
        }
    }

    /* --- item sur pedestal (trone gratuit / boutique payante) --- */
    if ((r->kind == RM_TREASURE || r->kind == RM_SHOP) && !(r->flags & 8)) {
        int16_t dx = (int16_t)(iz->x - (8 * 32 + 16));
        int16_t dy = (int16_t)(iz->y - (6 * 32 + 16));
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 24 && dy < 24
            && !(r->kind == RM_SHOP && iz->coins < PRICE_ITEM)) {
            uint8_t it = (r->kind == RM_SHOP) ? g->item2 : g->item;
            if (r->kind == RM_SHOP)
                iz->coins = (uint8_t)(iz->coins - PRICE_ITEM);
            play_apply_item(iz, it);
            iz->has_item |= (uint32_t)1UL << it;
            spawn_banner(item_name(it), item_desc(it));
            r->flags |= 8;
        }
    }

    /* --- trappe vers etage suivant --- */
    {
        for (i = 0; i < r->n_pick; i++) {
            if (r->pick[i].type == PK_TRAP) {
                int16_t ddx = (int16_t)(iz->x - r->pick[i].x);
                int16_t ddy = (int16_t)(iz->y - r->pick[i].y);
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (ddx < 20 && ddy < 20) return EV_NEXT_FLOOR;
            }
        }
    }

    /* --- salle nettoyee --- */
    if (combat && n_en == 0) {
        r->flags |= 2;
        /* celebration : eclat dore + LIBRE ! */
        burst(iz->x, iz->y - 20, 10, COL_GOLD);
        spawn_pop(iz->x, iz->y - 28, "LIBRE !", COL_GOLD);
        shake = 3;
        /* trone du boss vaincu : trappe vers l'etage suivant */
        if (r->kind == RM_BOSS && g->floor < 3)
            room_give_pick(r, PK_TRAP, 8, 6);
    }

    /* --- MAMAN vaincue : victoire --- */
    if (r->kind == RM_BOSS && n_en == 0 && g->floor == 3) return EV_WIN;

    /* --- mort --- */
    if (iz->hp == 0) return EV_DEAD;

    return EV_NONE;
}

/* ---------- rendu ---------- */

static void draw_floor_walls(const Game *g)
{
    uint8_t tx, ty;
    uint8_t base = (g->floor == 1) ? COL_FLOOR
                  : (g->floor == 2) ? COL_FLOOR2 : COL_FLOOR2_MID;
    uint8_t mid  = (g->floor == 1) ? COL_FLOOR_MID
                  : (g->floor == 2) ? COL_FLOOR2_MID : COL_FLOOR2_DK;
    uint8_t dark = (g->floor == 1) ? COL_FLOOR_DK
                  : (g->floor == 2) ? COL_FLOOR2_DK : COL_WALL_DD;

    /* murs */
    gfx_SetColor(COL_WALL);
    gfx_FillRectangle(0, 0, 320, 16);
    gfx_FillRectangle(0, 224, 320, 16);
    gfx_FillRectangle(0, 16, 16, 208);
    gfx_FillRectangle(304, 16, 16, 208);
    gfx_SetColor(COL_WALL_DK);
    gfx_FillRectangle(0, 12, 320, 4);
    gfx_FillRectangle(0, 224, 320, 4);
    gfx_FillRectangle(12, 16, 4, 208);
    gfx_FillRectangle(304, 16, 4, 208);

    /* sol */
    gfx_SetColor(base);
    gfx_FillRectangle(ROOM_X, ROOM_Y, ROOM_W, ROOM_H);

    /* taches deterministes par tuile */
    for (ty = 0; ty < ROOM_H_TILES; ty++)
        for (tx = 0; tx < ROOM_W_TILES; tx++) {
            uint8_t h = (uint8_t)(tx * 7 + ty * 13 + g->cur * 29);
            if ((h & 15) == 3) {
                gfx_SetColor(mid);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 3),
                                  (uint8_t)(ROOM_Y + ty * 16 + 4), 3, 2);
            } else if ((h & 31) == 9) {
                gfx_SetColor(dark);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 10),
                                  (uint8_t)(ROOM_Y + ty * 16 + 11), 2, 2);
            } else if ((h & 15) == 12 && g->floor == 2) {
                /* planches de bois trainantes (etage 2) */
                gfx_SetColor(COL_WOOD_DK);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 3),
                                  (uint8_t)(ROOM_Y + ty * 16 + 7), 10, 2);
                gfx_SetColor(COL_WOOD);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 4),
                                  (uint8_t)(ROOM_Y + ty * 16 + 7), 8, 1);
            } else if ((h & 15) == 11 && g->floor == 3) {
                /* cranes de L'ANTRE */
                gfx_SetColor(COL_OFFWHITE);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 5),
                                  (uint8_t)(ROOM_Y + ty * 16 + 6), 6, 5);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 6),
                                  (uint8_t)(ROOM_Y + ty * 16 + 11), 4, 1);
                gfx_SetColor(COL_BLACK);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 6),
                                  (uint8_t)(ROOM_Y + ty * 16 + 7), 2, 2);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 9),
                                  (uint8_t)(ROOM_Y + ty * 16 + 7), 2, 2);
            } else if ((h & 15) == 6 && g->floor == 3) {
                /* petits trous noirs du sol */
                gfx_SetColor(dark);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 5),
                                  (uint8_t)(ROOM_Y + ty * 16 + 7), 8, 3);
                gfx_SetColor(COL_BLACK);
                gfx_FillRectangle((uint24_t)(ROOM_X + tx * 16 + 6),
                                  (uint8_t)(ROOM_Y + ty * 16 + 8), 6, 2);
            }
        }

    /* accentes par type de salle */
    if (g->rooms[g->cur].kind == RM_DEVIL) {
        /* sol noir + grande croix de sang */
        gfx_SetColor(COL_BLACK);
        gfx_FillRectangle(ROOM_X, ROOM_Y, ROOM_W, ROOM_H);
        gfx_SetColor(COL_BLOOD_DK);
        gfx_FillRectangle(146, 24, 28, 192);
        gfx_FillRectangle(64, 106, 192, 28);
        gfx_SetColor(COL_BLOOD);
        gfx_FillRectangle(154, 32, 12, 176);
        gfx_FillRectangle(72, 114, 176, 12);
    } else if (g->rooms[g->cur].kind == RM_SACRI) {
        /* cadre dore autour du pic central */
        gfx_SetColor(COL_GOLD_DK);
        gfx_Rectangle(120, 98, 64, 44);
        gfx_SetColor(COL_GOLD);
        gfx_HorizLine(120, 98, 64);
        gfx_HorizLine(120, 141, 64);
    } else if (g->rooms[g->cur].kind == RM_MAUDITE) {
        /* grandes croix de sang sur le sol */
        gfx_SetColor(COL_BLOOD_DK);
        gfx_FillRectangle(138, 32, 44, 192);
        gfx_FillRectangle(64, 106, 192, 44);
        gfx_SetColor(COL_BLOOD);
        gfx_FillRectangle(148, 42, 24, 172);
        gfx_FillRectangle(74, 116, 172, 24);
    }
}

static void draw_doors(const Game *g, uint8_t combat)
{
    uint8_t d;
    for (d = 0; d < 4; d++) {
        uint8_t cx, cy;
        if (!has_door(g, d)) continue;
        for (cy = 0; cy < MAP_H; cy++)
            for (cx = 0; cx < MAP_W; cx++)
                if (g->grid[cy][cx] == g->cur + 1) {
                    uint8_t nb = gen_neighbor(g, cx, cy, d);
                    const RoomState *rn = &g->rooms[nb - 1];
                    const gfx_sprite_t *s;
                    uint8_t locked = 0;
                    int px = 0, py = 0;
                    if (rn->kind == RM_TREASURE && !(rn->flags & 4)) locked = 1;
                    if (d == DIR_N) { px = 144; py = 0; }
                    if (d == DIR_S) { px = 144; py = 224; }
                    if (d == DIR_W) { px = 0;   py = 112; }
                    if (d == DIR_E) { px = 304; py = 112; }
                    if (locked) s = (d == DIR_W || d == DIR_E) ? door_v_lock : door_h_lock;
                    else if (combat) s = (d == DIR_W || d == DIR_E) ? door_v_closed : door_h_closed;
                    else s = (d == DIR_W || d == DIR_E) ? door_v_open : door_h_open;
                    gfx_TransparentSprite_NoClip(s, (uint24_t)px, (uint8_t)py);
                    /* bande de couleur selon la salle voisine */
                    {
                        uint8_t band = 0;
                        switch (rn->kind) {
                        case RM_TREASURE: band = COL_GOLD_DK;  break;
                        case RM_SHOP:     band = COL_GOLD;     break;
                        case RM_BOSS:     band = COL_BLOOD;    break;
                        case RM_DEVIL:    band = COL_BLOOD_DK; break;
                        case RM_SACRI:    band = COL_OFFWHITE; break;
                        case RM_MAUDITE:  band = COL_BLOOD_DK; break;
                        case RM_SECRET:   band = COL_OFFWHITE; break;
                        default:          band = 0;            break;
                        }
                        if (band) {
                            gfx_SetColor(band);
                            if (d == DIR_N || d == DIR_S) {
                                gfx_FillRectangle((uint24_t)(px - 6),
                                    (uint8_t)(py + 6), 4, 10);
                                gfx_FillRectangle((uint24_t)(px + 18),
                                    (uint8_t)(py + 6), 4, 10);
                            } else {
                                gfx_FillRectangle((uint24_t)(px + 6),
                                    (uint8_t)(py - 6), 10, 4);
                                gfx_FillRectangle((uint24_t)(px + 6),
                                    (uint8_t)(py + 18), 10, 4);
                            }
                        }
                    }
                    if (rn->kind == RM_MAUDITE) {
                        /* porte qui mord : marque de sang au pied */
                        gfx_SetColor(COL_BLOOD);
                        gfx_FillRectangle((uint24_t)(px + 4),
                            (uint8_t)(py + (d == DIR_S ? 0 : 12)), 8, 4);
                    }
                    cy = MAP_H; break; /* sortir des boucles */
                }
    }
}

/* direction de sortie -> porte verrouillee : gere la cle dans play_update? 
   Non : cle geree dans play_transit (main). */

/* ombre portee grossiere au sol sous une entite */
static void draw_shadow(uint8_t col, uint24_t px, uint8_t py, uint8_t wd)
{
    gfx_SetColor(col);
    gfx_FillRectangle((uint24_t)(px - wd), (uint8_t)(py + 5), wd * 2, 2);
    gfx_FillRectangle((uint24_t)(px - wd + 3), (uint8_t)(py + 7), wd * 2 - 6, 1);
}

static const gfx_sprite_t *item_sprite(uint8_t item)
{
    switch (item) {
    case IT_ONION:     return it_onion;
    case IT_EYE:       return it_eye;
    case IT_MUSH:      return it_mush;
    case IT_BREAKFAST: return it_breakfast;
    case IT_SPOON:     return it_spoon;
    case IT_BLOOD:     return it_blood;
    case IT_LUNCH:     return it_lunch;
    case IT_PACTE:     return it_blood;
    case IT_PECHE:     return it_eye;
    case IT_BOUSSOLE:  return it_compass;
    case IT_AIMANT:    return it_magnet;
    case IT_BOTTES:    return it_boots;
    case IT_STEAK:     return it_steak;
    case IT_CRANE:     return it_skull;
    case IT_AIGUILLE:  return it_needle;
    case IT_SPECTRE:   return it_ghost;
    case IT_TELEGUIDE: return it_homing;
    case IT_MASSE:     return it_bullet;
    case IT_SOUFFLE:   return it_wind;
    case IT_DEMON:     return it_demon;
    case IT_VENIN:     return it_poison;
    case IT_TREFLE:    return it_clover;
    default:           return it_num1;
    }
}

void play_draw(const Game *g, const Isaac *iz)
{
    const RoomState *r = cur_room_c(g);
    uint8_t i;
    uint8_t combat = (n_en > 0);
    static uint8_t bobt;              /* tick d'animation global */
    uint8_t bob;

    bobt++;
    bob = (uint8_t)((bobt >> 4) & 1);  /* flottement des pickups (+/-1 px) */

    draw_floor_walls(g);
    draw_doors(g, combat);

    /* obstacles */
    for (i = 0; i < r->n_obst; i++) {
        const Obst *o = &r->obst[i];
        uint24_t px = (uint24_t)(ROOM_X + o->tx * 16);
        uint8_t  py = (uint8_t)(ROOM_Y + o->ty * 16);
        switch (o->type) {
        case OB_ROCK:   gfx_TransparentSprite_NoClip(rock, px, py); break;
        case OB_ROCK2:  gfx_TransparentSprite_NoClip(rock2, px, py); break;
        case OB_SPIKE:  gfx_TransparentSprite_NoClip(spike, px, py); break;
        case OB_FIRE:
            gfx_TransparentSprite_NoClip(((bobt >> 3) & 1) ? fire_1 : fire_0,
                                         px, py);
            break;
        case OB_PIT:    gfx_TransparentSprite_NoClip(pit, px, py); break;
        case OB_BLOCK:  gfx_TransparentSprite_NoClip(block, px, py); break;
        case OB_POOP:   gfx_TransparentSprite_NoClip(poop, px, py); break;
        case OB_CACTUS: gfx_TransparentSprite_NoClip(cactus, px, py); break;
        case OB_BONES:  gfx_TransparentSprite_NoClip(bones, px, py); break;
        case OB_STOCK: {
            /* machine a dons : caisse, fente et bac a pieces */
            gfx_SetColor(COL_WOOD);
            gfx_FillRectangle((uint24_t)(px + 2), (uint8_t)(py + 1), 12, 14);
            gfx_SetColor(COL_WOOD_DK);
            gfx_FillRectangle((uint24_t)(px + 3), (uint8_t)(py + 2), 10, 3);
            gfx_SetColor(COL_BLACK);
            gfx_FillRectangle((uint24_t)(px + 5), (uint8_t)(py + 6), 6, 2);
            gfx_SetColor(COL_GOLD);
            gfx_FillRectangle((uint24_t)(px + 5), (uint8_t)(py + 11), 6, 2);
            break;
        }
        default: break;   /* OB_NONE : detruit, invisible */
        }
    }

    /* decals de sang persistants */
    for (i = 0; i < MAX_DECALS; i++) {
        uint24_t px, py;
        if (!decals[i].ttl) continue;
        decals[i].ttl--;
        px = (uint24_t)(ROOM_X + (decals[i].x >> 1));
        py = (uint8_t)(ROOM_Y + (decals[i].y >> 1));
        gfx_SetColor(COL_BLOOD_DK);
        gfx_FillRectangle((uint24_t)(px - 3), py, 7, 3);
        gfx_FillRectangle((uint24_t)(px - 5), (uint8_t)(py + 1), 2, 2);
        gfx_SetColor(COL_BLOOD);
        gfx_FillRectangle((uint24_t)(px - 1), (uint8_t)(py - 2), 2, 2);
    }

    /* pedestal (trone / boutique) */
    if (r->kind == RM_TREASURE || r->kind == RM_SHOP) {
        gfx_TransparentSprite_NoClip(pedestal, ROOM_X + 128, ROOM_Y + 96);
        if (!(r->flags & 8)) {
            gfx_TransparentSprite_NoClip(
                item_sprite((r->kind == RM_SHOP) ? g->item2 : g->item),
                ROOM_X + 128, ROOM_Y + 80);
            if (r->kind == RM_SHOP) {
                gfx_TransparentSprite_NoClip(mini_coin,
                    ROOM_X + 114, ROOM_Y + 114);
                gfx_SetTextScale(1, 1);
                gfx_SetTextFGColor(COL_GOLD);
                gfx_SetTextXY(ROOM_X + 124, ROOM_Y + 114);
                gfx_PrintUInt(PRICE_ITEM, 2);
            }
        }
    }

    /* pickups */
    for (i = 0; i < r->n_pick; i++) {
        const Pickup *p = &r->pick[i];
        uint24_t px, pxs;
        uint8_t py;
        if (!p->type) continue;
        px = (uint24_t)(ROOM_X + (p->x >> 1));
        py = (uint8_t)(ROOM_Y + (p->y >> 1));
        if (p->type == PK_TRAP) {
            uint24_t txp = (uint24_t)(ROOM_X + p->tx * 16);
            uint8_t  typ = (uint8_t)(ROOM_Y + p->ty * 16);
            gfx_TransparentSprite_NoClip(trapdoor, txp, typ);
            /* halo clignotant pour attirer l'oeil */
            if ((bobt >> 4) & 1) {
                gfx_SetColor(COL_GOLD_DK);
                gfx_Rectangle((uint24_t)(txp - 1), (uint8_t)(typ - 1), 18, 18);
            }
            continue;
        }
        pxs = px - 8;
        switch (p->type) {
        case PK_HEART: gfx_TransparentSprite_NoClip(pickup_heart, pxs, (uint8_t)(py - 8 - bob)); break;
        case PK_HALF:  gfx_TransparentSprite_NoClip(pickup_half, pxs, (uint8_t)(py - 8 - bob)); break;
        case PK_COIN:  gfx_TransparentSprite_NoClip(pickup_coin, pxs, (uint8_t)(py - 8 - bob)); break;
        case PK_KEY:   gfx_TransparentSprite_NoClip(pickup_key, pxs, (uint8_t)(py - 8 - bob)); break;
        case PK_BOMB:  gfx_TransparentSprite_NoClip(pickup_bomb, pxs, (uint8_t)(py - 8 - bob)); break;
        case PK_SILVER:
            gfx_TransparentSprite_NoClip(pickup_silver, pxs,
                                         (uint8_t)(py - 8 - bob));
            break;
        case PK_SOUL:
            gfx_TransparentSprite_NoClip(pickup_soul, pxs,
                                         (uint8_t)(py - 8 - bob));
            break;
        case PK_CHEST:
        case PK_LOCKED: {
            uint8_t trim = (p->type == PK_LOCKED) ? COL_GOLD : COL_WALL_DK;
            gfx_SetColor(trim);
            gfx_FillRectangle(pxs, (uint8_t)(py - 8), 16, 16);
            gfx_SetColor(COL_WALL);
            gfx_FillRectangle((uint24_t)(pxs + 2), (uint8_t)(py - 6), 12, 12);
            gfx_SetColor(COL_WALL_DK);
            gfx_FillRectangle((uint24_t)(pxs + 4), (uint8_t)(py - 2), 8, 2);
            gfx_SetColor(COL_BLACK);
            gfx_FillRectangle((uint24_t)(pxs + 7), (uint8_t)(py - 1), 2, 3);
            break;
        }
        case PK_DEVIL: {
            /* pacte : socle noir + objet + prix en coeurs */
            uint8_t it = (p->tx <= 8) ? IT_PACTE : IT_PECHE;
            gfx_SetColor(COL_WALL_DK);
            gfx_FillRectangle((uint24_t)(pxs - 2), (uint8_t)(py + 4), 20, 4);
            gfx_SetColor(COL_BLACK);
            gfx_FillRectangle((uint24_t)(pxs + 2), (uint8_t)(py - 2), 12, 6);
            gfx_TransparentSprite_NoClip(item_sprite(it),
                (uint24_t)(pxs + 2), (uint8_t)(py - 12 - bob));
            gfx_SetColor(COL_BLOOD);
            gfx_FillRectangle((uint24_t)(pxs + 3), (uint8_t)(py + 10), 4, 4);
            gfx_FillRectangle((uint24_t)(pxs + 9), (uint8_t)(py + 10), 4, 4);
            break;
        }
        default: break;
        }
        /* etiquette de prix en boutique */
        if (r->kind == RM_SHOP && p->price) {
            gfx_SetTextScale(1, 1);
            gfx_SetTextFGColor(COL_GOLD);
            gfx_SetTextXY((uint24_t)(px - 6), (uint8_t)(py + 10));
            gfx_PrintUInt(p->price, 2);
        }
    }

    /* ennemis : ombre portee puis sprite ; champions en rouge */
    {
        uint8_t shadow_col = (g->floor == 1) ? COL_FLOOR_DK
                          : (g->floor == 2) ? COL_FLOOR2_DK : COL_WALL_DD;
        for (i = 0; i < n_en; i++) {
            const Enemy *e = &en[i];
            uint24_t px = (uint24_t)(ROOM_X + (e->x >> 1));
            uint8_t  py = (uint8_t)(ROOM_Y + (e->y >> 1));
            uint8_t f = (e->t >> 3) & 1;
            if (e->flash && (e->t & 1)) continue; /* clignote quand touche */
            /* ombre : volants projetes plus loin, MAMAN collee au mur */
            if (e->kind != EN_MAMAN) {
                uint8_t wd = (e->kind == EN_MONSTRO) ? 12
                           : (e->kind == EN_GEMINI || e->kind == EN_CROIX) ? 7 : 5;
                uint8_t dy = (e->kind == EN_FLY ||
                              e->kind == EN_POOTER) ? 3 : 0;
                draw_shadow(shadow_col, px, (uint8_t)(py + dy), wd);
            }
            switch (e->kind) {
            case EN_GAPER: gfx_TransparentSprite_NoClip(
                               e->champ ? gaper_c : (f ? gaper_1 : gaper_0),
                               (uint24_t)(px - 8), (uint8_t)(py - 8)); break;
            case EN_FLY:   gfx_TransparentSprite_NoClip(
                               e->champ ? fly_c : (f ? fly_1 : fly_0),
                               (uint24_t)(px - 8), (uint8_t)(py - 8)); break;
            case EN_POOTER:gfx_TransparentSprite_NoClip(
                               e->champ ? pooter_c : (f ? pooter_1 : pooter_0),
                               (uint24_t)(px - 8), (uint8_t)(py - 8)); break;
            case EN_HORF:  gfx_TransparentSprite_NoClip(
                               e->champ ? horf_c : horf,
                               (uint24_t)(px - 8), (uint8_t)(py - 8)); break;
            case EN_MONSTRO:
                if (e->st == 1)
                    gfx_ScaledTransparentSprite_NoClip(monstro_0,
                        (uint24_t)(px - 24), (uint8_t)(py - 8), 3, 1);
                else
                    gfx_ScaledTransparentSprite_NoClip((e->st == 2) ? monstro_1 :
                        ((e->t >> 3) & 1 ? monstro_1 : monstro_0),
                        (uint24_t)(px - 16), (uint8_t)(py - 16), 2, 2);
                break;
            case EN_CHARGER:
                gfx_TransparentSprite_NoClip(
                    e->champ ? gaper_c : (f ? gaper_1 : gaper_0),
                    (uint24_t)(px - 8), (uint8_t)(py - 8));
                gfx_SetColor(e->st ? COL_BLOOD : COL_BLOOD_DK);
                gfx_FillRectangle((uint24_t)(px - 4), (uint8_t)(py - 3), 3, 2);
                gfx_FillRectangle((uint24_t)(px + 2), (uint8_t)(py - 3), 3, 2);
                break;
            case EN_SPIDER:
                gfx_SetColor(e->champ ? COL_BLOOD : COL_BLACK);
                gfx_FillRectangle((uint24_t)(px - 7), (uint8_t)(py - 1), 3, 2);
                gfx_FillRectangle((uint24_t)(px + 5), (uint8_t)(py - 1), 3, 2);
                gfx_FillRectangle((uint24_t)(px - 6), (uint8_t)(py + 4), 3, 2);
                gfx_FillRectangle((uint24_t)(px + 4), (uint8_t)(py + 4), 3, 2);
                gfx_TransparentSprite_NoClip(e->champ ? fly_c : (f ? fly_1 : fly_0),
                    (uint24_t)(px - 4), (uint8_t)(py - 4));
                break;
            case EN_GEMINI:
                gfx_ScaledTransparentSprite_NoClip(
                    ((e->t >> 3) & 1) ? monstro_1 : monstro_0,
                    (uint24_t)(px - 8), (uint8_t)(py - 8), 1, 1);
                gfx_SetColor(COL_BLOOD);
                gfx_FillRectangle((uint24_t)(px - 3), (uint8_t)(py - 2), 2, 2);
                gfx_FillRectangle((uint24_t)(px + 2), (uint8_t)(py - 2), 2, 2);
                break;
            case EN_SAUTEUR:
                /* gaper bondissant : marque de jambes, decolle au bond */
                gfx_TransparentSprite_NoClip(
                    e->champ ? gaper_c : (f ? gaper_1 : gaper_0),
                    (uint24_t)(px - 8),
                    (uint8_t)(py - 8 - (e->st == 2 ? 6 : 0)));
                gfx_SetColor((e->st == 1) ? COL_BLOOD : COL_BLOOD_DK);
                gfx_FillRectangle((uint24_t)(px - 5), (uint8_t)(py + 5), 10, 2);
                break;
            case EN_CROIX:
                /* statue cracheur : croix marquee au-dessus */
                gfx_TransparentSprite_NoClip(
                    e->champ ? horf_c : horf,
                    (uint24_t)(px - 8), (uint8_t)(py - 8));
                gfx_SetColor((e->st & 1) ? COL_BLOOD : COL_BLOOD_DK);
                gfx_FillRectangle((uint24_t)(px - 1), (uint8_t)(py - 13), 2, 7);
                gfx_FillRectangle((uint24_t)(px - 3), (uint8_t)(py - 11), 6, 2);
                break;
            case EN_MAMAN:
                /* MAMAN : grande face rose, ecrasee au telegraph */
                if (e->st == 1)
                    gfx_ScaledTransparentSprite_NoClip(maman_0,
                        (uint24_t)(px - 24 + (e->t & 1)), (uint8_t)(py - 8), 3, 1);
                else
                    gfx_ScaledTransparentSprite_NoClip(maman_0,
                        (uint24_t)(px - 24), (uint8_t)(py - 24), 3, 3);
                break;
            default: break;
            }
        }
    }

    /* larmes */
    for (i = 0; i < n_tr; i++) {
        uint24_t px = (uint24_t)(ROOM_X + (tr[i].x >> 1));
        uint8_t  py = (uint8_t)(ROOM_Y + (tr[i].y >> 1));
        if (!tr[i].hostile && iz->dmg >= 10)
            gfx_ScaledTransparentSprite_NoClip(tear,
                (uint24_t)(px - 8), (uint8_t)(py - 8), 2, 2);
        else
            gfx_TransparentSprite_NoClip(tr[i].hostile ? eshot : tear,
                                          (uint24_t)(px - 4), (uint8_t)(py - 4));
    }

    /* bombes */
    for (i = 0; i < n_bm; i++) {
        if ((bm[i].fuse & 3) < 2) continue;
        gfx_TransparentSprite_NoClip(pickup_bomb,
            (uint24_t)(ROOM_X + (bm[i].x >> 1) - 8),
            (uint8_t)(ROOM_Y + (bm[i].y >> 1) - 8));
    }

    /* isaac : sprite reel du personnage, ombre, clignement des yeux */
    {
        uint8_t blink = (uint8_t)(iz->iframe && ((iz->iframe >> 1) & 1));
        if (!blink) {
            uint24_t sx = (uint24_t)(ROOM_X + (iz->x >> 1));
            uint8_t  sy = (uint8_t)(ROOM_Y + (iz->y >> 1));
            uint8_t walking = (uint8_t)(iz->anim && ((iz->anim >> 2) & 1));
            const gfx_sprite_t *s;
            draw_shadow((g->floor == 1) ? COL_FLOOR_DK
                      : (g->floor == 2) ? COL_FLOOR2_DK : COL_WALL_DD,
                        sx, sy, 6);
            switch (play_char_id) {
            case CHAR_MAGDA:
                s = iz->iframe ? magda_h : (walking ? magda_1 : magda_0);
                break;
            case CHAR_CAINE:
                s = iz->iframe ? caine_h : (walking ? caine_1 : caine_0);
                break;
            case CHAR_LAZARE:
                s = iz->iframe ? lazare_h : (walking ? lazare_1 : lazare_0);
                break;
            default:
                s = iz->iframe ? isaac_h : (walking ? isaac_1 : isaac_0);
                break;
            }
            gfx_TransparentSprite_NoClip(s, (uint24_t)(sx - 8), (uint8_t)(sy - 8));
            /* clignement periodique des yeux */
            if ((bobt & 127) < 6) {
                gfx_SetColor(COL_SKIN);
                gfx_FillRectangle((uint24_t)(sx - 3), (uint8_t)(sy - 3), 2, 2);
                gfx_FillRectangle((uint24_t)(sx + 1), (uint8_t)(sy - 3), 2, 2);
            }
        }
    }

    /* particules */
    for (i = 0; i < n_pt; i++) {
        gfx_SetColor(pt[i].col);
        gfx_FillRectangle((uint24_t)(ROOM_X + (pt[i].x >> 1) - 1),
                          (uint8_t)(ROOM_Y + (pt[i].y >> 1) - 1), 2, 2);
    }

    /* petits textes flottants */
    gfx_SetTextScale(1, 1);
    for (i = 0; i < MAX_POPS; i++) {
        if (!pops[i].ttl) continue;
        pops[i].ttl--;
        pops[i].y--;
        gfx_SetTextFGColor(pops[i].col);
        gfx_PrintStringXY(pops[i].txt,
            (uint24_t)(ROOM_X + (pops[i].x >> 1) - 8),
            (uint8_t)(ROOM_Y + (pops[i].y >> 1) - 10));
    }

    /* bandeau nom d'objet ramasse */
    if (banner_ttl) {
        banner_ttl--;
        if (banner_ttl > 8) {
            uint24_t w, dw;
            gfx_SetTextScale(2, 2);
            w = gfx_GetStringWidth(banner_txt);
            gfx_SetColor(COL_BLACK);
            gfx_FillRectangle((uint24_t)(158 - w), 22, (uint24_t)(2 * w + 12), 32);
            gfx_SetTextFGColor(COL_GOLD);
            gfx_PrintStringXY(banner_txt, (uint24_t)(160 - w), 26);
            gfx_SetTextScale(1, 1);
            if (banner_desc) {
                dw = gfx_GetStringWidth(banner_desc);
                gfx_SetTextFGColor(COL_OFFWHITE);
                gfx_PrintStringXY(banner_desc, (uint24_t)(160 - (dw >> 1)), 44);
            }
        }
    }

    /* ecran d'intro du boss */
    if (boss_intro > 6) {
        uint24_t w;
        gfx_SetColor(COL_BLACK);
        gfx_FillRectangle(ROOM_X, ROOM_Y, ROOM_W, ROOM_H);
        gfx_SetTextScale(3, 3);
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("!!", 148, 70);
        gfx_SetTextScale(2, 2);
        w = gfx_GetStringWidth(boss_name);
        gfx_SetTextFGColor(COL_OFFWHITE);
        gfx_PrintStringXY(boss_name, (uint24_t)(160 - (w >> 1)), 110);
        gfx_SetTextScale(1, 1);
        gfx_SetTextFGColor(COL_GREY);
        gfx_PrintStringXY("PREPARE-TOI...", 122, 136);
    }

    /* bordure rouge : Isaac vient de prendre des degats */
    if (hurtflash) {
        hurtflash--;
        gfx_SetColor(COL_BLOOD);
        gfx_FillRectangle(0, 0, 320, 6);
        gfx_FillRectangle(0, 234, 320, 6);
        gfx_FillRectangle(0, 0, 6, 240);
        gfx_FillRectangle(314, 0, 6, 240);
    }
}

void play_draw_hud(const Game *g, const Isaac *iz)
{
    uint8_t nhearts = (uint8_t)(iz->hp_max >> 1);
    uint8_t i;

    /* coeurs */
    for (i = 0; i < nhearts && i < 8; i++) {
        const gfx_sprite_t *s = hud_empty;
        if (iz->hp >= (uint8_t)(2 * i + 2)) s = hud_heart;
        else if (iz->hp == (uint8_t)(2 * i + 1)) s = hud_half;
        gfx_TransparentSprite_NoClip(s, (uint24_t)(2 + i * 9), 2);
    }

    /* coeurs d'ame (a droite des rouges) */
    {
        uint8_t ns = (uint8_t)(iz->hp_soul >> 1);
        uint8_t rem = (uint8_t)(iz->hp_soul & 1);
        uint8_t x0 = (uint8_t)(2 + (nhearts > 8 ? 8 : nhearts) * 9);
        uint8_t j;
        for (j = 0; j < ns && j < 4; j++)
            gfx_TransparentSprite_NoClip(hud_soul, (uint24_t)(x0 + j * 9), 2);
        if (rem && ns < 4)
            gfx_TransparentSprite_NoClip(hud_soul_half,
                                         (uint24_t)(x0 + ns * 9), 2);
    }

    /* compteurs */
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_SetTextScale(1, 1);
    gfx_TransparentSprite_NoClip(mini_coin, (uint24_t)4, 226);
    gfx_PrintStringXY("x", 13, 226);
    gfx_PrintUInt(iz->coins, 2);
    gfx_TransparentSprite_NoClip(mini_bomb, (uint24_t)52, 226);
    gfx_PrintStringXY("x", 61, 226);
    gfx_PrintUInt(iz->bombs, 2);
    gfx_TransparentSprite_NoClip(mini_key, (uint24_t)100, 226);
    gfx_PrintStringXY("x", 109, 226);
    gfx_PrintUInt(iz->keys, 2);

    /* malédiction de l'etage : point d'exclamation */
    if (g->curse) {
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("!", 289, 2);
    }

    /* etage */
    gfx_PrintStringXY((g->floor == 1) ? "B1"
                     : (g->floor == 2) ? "B2" : "B3", 300, 2);

    /* barre HP boss */
    {
        uint8_t alive = 0;
        uint16_t hp = 0;
        uint16_t maxhp = 0;
        for (i = 0; i < n_en; i++) {
            if (en[i].kind == EN_MAMAN) {
                alive = 1;
                hp += en[i].hp;
                maxhp = (uint16_t)(maxhp + 1000);
            } else if (en[i].kind == EN_MONSTRO) {
                alive = 1;
                hp += en[i].hp;
                maxhp = (uint16_t)(maxhp + ((g->floor == 2) ? 800 : 500));
            } else if (en[i].kind == EN_GEMINI) {
                alive = 1;
                hp += en[i].hp;
                maxhp = (uint16_t)(maxhp + 320);
            }
        }
        if (alive && maxhp) {
            uint24_t w = (uint24_t)(100 * hp / maxhp);
            gfx_SetColor(COL_BLACK);
            gfx_FillRectangle(214, 2, 100, 8);
            gfx_SetColor(COL_HEART_DK);
            gfx_FillRectangle(215, 3, 98, 6);
            gfx_SetColor(COL_BLOOD);
            gfx_FillRectangle(215, 3, w, 6);
        }
    }
}
