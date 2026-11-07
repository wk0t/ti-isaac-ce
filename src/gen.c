#include "gen.h"
#include "rng.h"

#include <string.h>

static const int8_t DIR_DX[4] = { 0, 0, -1, 1 };
static const int8_t DIR_DY[4] = { -1, 1, 0, 0 };

uint8_t gen_room_at(const Game *g, uint8_t cx, uint8_t cy)
{
    if (cx >= MAP_W || cy >= MAP_H) return 0;
    return g->grid[cy][cx];
}

uint8_t gen_neighbor(const Game *g, uint8_t cx, uint8_t cy, uint8_t dir)
{
    int nx = (int)cx + DIR_DX[dir];
    int ny = (int)cy + DIR_DY[dir];
    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) return 0;
    return g->grid[ny][nx];
}

uint8_t gen_degree(const Game *g, uint8_t cx, uint8_t cy)
{
    uint8_t d = 0, i;
    if (!gen_room_at(g, cx, cy)) return 0;
    for (i = 0; i < 4; i++)
        if (gen_neighbor(g, cx, cy, i)) d++;
    return d;
}

/* Profondeur BFS (en nombre de salles) depuis la salle aux coordonnees passees.
   depth[y][x] ; retoune le nombre de salles atteintes. */
static uint8_t bfs_depth(const Game *g, uint8_t sx, uint8_t sy,
                         uint8_t depth[MAP_H][MAP_W])
{
    uint8_t queue[MAP_W * MAP_H][2];
    uint16_t qs = 0, qe = 0;
    uint8_t count = 0;

    memset(depth, 0xFF, sizeof(uint8_t) * MAP_H * MAP_W);
    if (!gen_room_at(g, sx, sy)) return 0;
    queue[qe][0] = sx; queue[qe][1] = sy; qe++;
    depth[sy][sx] = 0;
    while (qs < qe) {
        uint8_t x = queue[qs][0], y = queue[qs][1], i;
        qs++;
        count++;
        for (i = 0; i < 4; i++) {
            uint8_t nx = (uint8_t)((int)x + DIR_DX[i]);
            uint8_t ny = (uint8_t)((int)y + DIR_DY[i]);
            if (nx < MAP_W && ny < MAP_H && g->grid[ny][nx]
                && depth[ny][nx] == 0xFF) {
                depth[ny][nx] = (uint8_t)(depth[y][x] + 1);
                queue[qe][0] = nx; queue[qe][1] = ny; qe++;
            }
        }
    }
    return count;
}

uint8_t gen_reachable(const Game *g, uint8_t cx, uint8_t cy)
{
    uint8_t depth[MAP_H][MAP_W];
    return bfs_depth(g, cx, cy, depth);
}

/* --- placement d'obstacles --------------------------------------------- */

static void put_obst(RoomState *r, uint8_t type, uint8_t tx, uint8_t ty)
{
    uint8_t i;
    if (r->n_obst >= MAX_OBST) return;
    for (i = 0; i < r->n_obst; i++)
        if (r->obst[i].tx == tx && r->obst[i].ty == ty)
            return;
    r->obst[r->n_obst].type = type;
    r->obst[r->n_obst].tx = tx;
    r->obst[r->n_obst].ty = ty;
    r->n_obst++;
}

static void layout_obstacles(RoomState *r)
{
    switch (rng_range(7)) {
    case 0:
        break;
    case 1: /* carre central de rochers */
        put_obst(r, OB_ROCK, 7, 5);
        put_obst(r, OB_ROCK, 10, 5);
        put_obst(r, OB_ROCK, 7, 7);
        put_obst(r, OB_ROCK, 10, 7);
        put_obst(r, OB_ROCK, 8, 6);
        break;
    case 2: /* deux rangees horizontales etroitees */
        put_obst(r, OB_ROCK, 4, 6);
        put_obst(r, OB_ROCK, 5, 6);
        put_obst(r, OB_ROCK, 6, 6);
        put_obst(r, OB_ROCK, 11, 6);
        put_obst(r, OB_ROCK, 12, 6);
        put_obst(r, OB_ROCK, 13, 6);
        break;
    case 3: /* pics dans les angles */
        put_obst(r, OB_SPIKE, 3, 3);
        put_obst(r, OB_SPIKE, 14, 3);
        put_obst(r, OB_SPIKE, 3, 9);
        put_obst(r, OB_SPIKE, 14, 9);
        break;
    case 4: /* feux */
        put_obst(r, OB_FIRE, 5, 6);
        put_obst(r, OB_FIRE, 12, 6);
        break;
    case 5: /* piliers */
        put_obst(r, OB_ROCK, 3, 3);
        put_obst(r, OB_ROCK, 14, 3);
        put_obst(r, OB_ROCK, 3, 9);
        put_obst(r, OB_ROCK, 14, 9);
        put_obst(r, OB_ROCK, 8, 6);
        put_obst(r, OB_ROCK, 9, 6);
        break;
    default: /* mixte */
        put_obst(r, OB_ROCK, 6, 4);
        put_obst(r, OB_SPIKE, 6, 8);
        put_obst(r, OB_ROCK, 11, 8);
        put_obst(r, OB_FIRE, 11, 4);
        break;
    }
}

/* voisin direct de la salle de depart ? (manhattan = 1) */
static uint8_t adj_start(uint8_t x, uint8_t y, uint8_t sx, uint8_t sy)
{
    uint8_t dx = (uint8_t)(x > sx ? x - sx : sx - x);
    uint8_t dy = (uint8_t)(y > sy ? y - sy : sy - y);
    return (uint8_t)(dx + dy == 1);
}

/* --- generation d'un etage --------------------------------------------- */

void gen_floor(Game *g, uint8_t floor)
{
    uint8_t target, tries;
    uint8_t start_x, start_y;
    uint8_t cx_rooms[MAP_W * MAP_H], cy_rooms[MAP_W * MAP_H];
    uint8_t depth[MAP_H][MAP_W];
    uint8_t i, boss;

    memset(g->grid, 0, sizeof g->grid);
    memset(g->rooms, 0, sizeof g->rooms);
    g->n_rooms = 0;
    g->floor = floor;
    g->kills = 0;
    g->curse = 0;
    /* etages profonds : malédiction OBSCURITE ou HATE */
    if (floor >= 2) {
        uint8_t roll = rng_range(100);
        if (roll < 30)      g->curse = CURSE_DARKNESS;
        else if (roll < 50) g->curse = CURSE_HASTE;
    }

    start_x = 4;
    start_y = (uint8_t)(3 + rng_range(2));

    g->grid[start_y][start_x] = (uint8_t)(g->n_rooms + 1);
    g->rooms[g->n_rooms].kind = RM_START;
    cx_rooms[g->n_rooms] = start_x;
    cy_rooms[g->n_rooms] = start_y;
    g->n_rooms++;

    target = (uint8_t)((floor == 1) ? 7 + rng_range(3) : 8 + rng_range(3));

    for (tries = 0; tries < 250 && g->n_rooms < target; tries++) {
        uint8_t idx = rng_range(g->n_rooms);
        uint8_t dir = rng_range(4);
        int nx = (int)cx_rooms[idx] + DIR_DX[dir];
        int ny = (int)cy_rooms[idx] + DIR_DY[dir];
        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
        if (g->grid[ny][nx]) continue;
        g->grid[ny][nx] = (uint8_t)(g->n_rooms + 1);
        g->rooms[g->n_rooms].kind = RM_NORMAL;
        cx_rooms[g->n_rooms] = (uint8_t)nx;
        cy_rooms[g->n_rooms] = (uint8_t)ny;
        g->n_rooms++;
    }

    /* --- boss : cul-de-sac le plus profond -------------------------------- */
    bfs_depth(g, start_x, start_y, depth);
    boss = 0;
    {
        uint8_t best = 0;
        for (i = 0; i < g->n_rooms; i++) {
            uint8_t x = cx_rooms[i], y = cy_rooms[i];
            uint8_t score, deg = gen_degree(g, x, y);
            if (g->rooms[i].kind != RM_NORMAL) continue;
            score = (uint8_t)(depth[y][x] + ((deg == 1) ? 2 : 0));
            if (score > best) { best = score; boss = i; }
        }
    }
    if (!boss) boss = (uint8_t)(g->n_rooms - 1);
    g->rooms[boss].kind = RM_BOSS;

    /* --- trone : cul-de-sac ou coude, hors boss, hors voisins du depart -- */
    {
        uint8_t treas = 0;
        for (tries = 0; tries < 60 && !treas; tries++) {
            uint8_t idx = rng_range(g->n_rooms);
            if (g->rooms[idx].kind != RM_NORMAL) continue;
            if (gen_degree(g, cx_rooms[idx], cy_rooms[idx]) > 2) continue;
            if (adj_start(cx_rooms[idx], cy_rooms[idx], start_x, start_y)) continue;
            treas = idx;
        }
        for (i = 0; i < g->n_rooms && !treas; i++)
            if (g->rooms[i].kind == RM_NORMAL
                && !adj_start(cx_rooms[i], cy_rooms[i], start_x, start_y)) treas = i;
        if (!treas)
            for (i = 0; i < g->n_rooms && !treas; i++)
                if (g->rooms[i].kind == RM_NORMAL) treas = i;
        if (treas) g->rooms[treas].kind = RM_TREASURE;
    }

    /* --- boutique : un autre NORMAL degre <= 2, hors voisins du depart -- */
    {
        uint8_t shop = 0;
        for (tries = 0; tries < 60 && !shop; tries++) {
            uint8_t idx = rng_range(g->n_rooms);
            if (g->rooms[idx].kind != RM_NORMAL) continue;
            if (gen_degree(g, cx_rooms[idx], cy_rooms[idx]) > 2) continue;
            if (adj_start(cx_rooms[idx], cy_rooms[idx], start_x, start_y)) continue;
            shop = idx;
        }
        for (i = 0; i < g->n_rooms && !shop; i++)
            if (g->rooms[i].kind == RM_NORMAL
                && !adj_start(cx_rooms[i], cy_rooms[i], start_x, start_y)) shop = i;
        for (i = 0; i < g->n_rooms && !shop; i++)
            if (g->rooms[i].kind == RM_NORMAL) shop = i;
        if (shop) {
            RoomState *r = &g->rooms[shop];
            r->kind = RM_SHOP;
            r->pick[0].type = PK_HEART; r->pick[0].price = PRICE_HEART;
            r->pick[0].tx = 5; r->pick[0].ty = 6;
            r->pick[0].x = (int16_t)(5 * 2 * TILE + TILE);
            r->pick[0].y = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[1].type = PK_KEY; r->pick[1].price = PRICE_KEY;
            r->pick[1].tx = 11; r->pick[1].ty = 6;
            r->pick[1].x = (int16_t)(11 * 2 * TILE + TILE);
            r->pick[1].y = (int16_t)(6 * 2 * TILE + TILE);
            r->n_pick = 2;
            put_obst(r, OB_STOCK, 13, 8);   /* machine a dons */
        }
    }

    /* --- salle secrete : case vide adjacente a une salle non-boss ------- */
    {
        uint8_t sx = 0, sy = 0, found = 0;
        for (tries = 0; tries < 120 && !found; tries++) {
            uint8_t idx = rng_range(g->n_rooms);
            uint8_t dir = rng_range(4);
            int nx, ny;
            if (g->rooms[idx].kind == RM_BOSS) continue;
            nx = (int)cx_rooms[idx] + DIR_DX[dir];
            ny = (int)cy_rooms[idx] + DIR_DY[dir];
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (g->grid[ny][nx]) continue;
            sx = (uint8_t)nx; sy = (uint8_t)ny; found = 1;
        }
        if (!found) {   /* balayage deterministe en secours */
            uint8_t x, y, d;
            for (y = 0; y < MAP_H && !found; y++)
                for (x = 0; x < MAP_W && !found; x++) {
                    if (g->grid[y][x]) continue;
                    for (d = 0; d < 4; d++) {
                        int nx = (int)x + DIR_DX[d];
                        int ny = (int)y + DIR_DY[d];
                        uint8_t nb;
                        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
                        nb = g->grid[ny][nx];
                        if (nb && g->rooms[nb - 1].kind != RM_BOSS) {
                            sx = x; sy = y; found = 1;
                            break;
                        }
                    }
                }
        }
        if (found) {
            RoomState *r = &g->rooms[g->n_rooms];
            g->grid[sy][sx] = (uint8_t)(g->n_rooms + 1);
            cx_rooms[g->n_rooms] = sx;
            cy_rooms[g->n_rooms] = sy;
            r->kind = RM_SECRET;
            /* butin : 4 pieces en losange + coeur au centre */
            r->pick[0].type = PK_COIN; r->pick[0].tx = 8;  r->pick[0].ty = 4;
            r->pick[0].x = (int16_t)(8 * 2 * TILE + TILE);
            r->pick[0].y = (int16_t)(4 * 2 * TILE + TILE);
            r->pick[1].type = PK_COIN; r->pick[1].tx = 6;  r->pick[1].ty = 6;
            r->pick[1].x = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[1].y = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[2].type = PK_COIN; r->pick[2].tx = 10; r->pick[2].ty = 6;
            r->pick[2].x = (int16_t)(10 * 2 * TILE + TILE);
            r->pick[2].y = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[3].type = PK_COIN; r->pick[3].tx = 8;  r->pick[3].ty = 8;
            r->pick[3].x = (int16_t)(8 * 2 * TILE + TILE);
            r->pick[3].y = (int16_t)(8 * 2 * TILE + TILE);
            r->pick[4].type = PK_HEART; r->pick[4].tx = 8;  r->pick[4].ty = 6;
            r->pick[4].x = (int16_t)(8 * 2 * TILE + TILE);
            r->pick[4].y = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[5].type = PK_LOCKED; r->pick[5].tx = 8;  r->pick[5].ty = 10;
            r->pick[5].x = (int16_t)(8 * 2 * TILE + TILE);
            r->pick[5].y = (int16_t)(10 * 2 * TILE + TILE);
            r->n_pick = 6;
            g->n_rooms++;
        }
    }

    /* --- salle du diable (etages 1-2) : adjacente au boss, fermee ---- */
    if (floor <= 2) {
        uint8_t bxc = cx_rooms[boss], byc = cy_rooms[boss];
        for (tries = 0; tries < 120; tries++) {
            uint8_t dir = rng_range(4);
            int nx = (int)bxc + DIR_DX[dir];
            int ny = (int)byc + DIR_DY[dir];
            RoomState *r;
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (g->grid[ny][nx]) continue;
            g->grid[ny][nx] = (uint8_t)(g->n_rooms + 1);
            cx_rooms[g->n_rooms] = (uint8_t)nx;
            cy_rooms[g->n_rooms] = (uint8_t)ny;
            r = &g->rooms[g->n_rooms];
            r->kind = RM_DEVIL;   /* flags : fermee tant que bit2 absent */
            r->pick[0].type = PK_DEVIL;
            r->pick[0].price = (uint8_t)(3 + rng_range(3));
            r->pick[0].tx = 6;  r->pick[0].ty = 6;
            r->pick[0].x = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[0].y = (int16_t)(6 * 2 * TILE + TILE);
            r->pick[1].type = PK_DEVIL;
            r->pick[1].price = (uint8_t)(3 + rng_range(3));
            r->pick[1].tx = 10; r->pick[1].ty = 6;
            r->pick[1].x = (int16_t)(10 * 2 * TILE + TILE);
            r->pick[1].y = (int16_t)(6 * 2 * TILE + TILE);
            r->n_pick = 2;
            g->n_rooms++;
            break;
        }
    }

    /* --- salle de sacrifice (etage 1) ou salle maudite (etages 2+)
           adjacente a une salle ordinaire ---- */
    {
        uint8_t sx = 0, sy = 0, found = 0;
        uint8_t cursed = (uint8_t)(floor >= 2 && rng_range(2) == 0);
        for (tries = 0; tries < 120 && !found; tries++) {
            uint8_t idx = rng_range(g->n_rooms);
            uint8_t dir = rng_range(4);
            int nx, ny;
            uint8_t k = g->rooms[idx].kind;
            if (k == RM_BOSS || k == RM_SECRET || k == RM_START
                || k == RM_DEVIL || k == RM_SACRI) continue;
            nx = (int)cx_rooms[idx] + DIR_DX[dir];
            ny = (int)cy_rooms[idx] + DIR_DY[dir];
            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (g->grid[ny][nx]) continue;
            sx = (uint8_t)nx; sy = (uint8_t)ny; found = 1;
        }
        if (found) {
            RoomState *r = &g->rooms[g->n_rooms];
            g->grid[sy][sx] = (uint8_t)(g->n_rooms + 1);
            cx_rooms[g->n_rooms] = sx;
            cy_rooms[g->n_rooms] = sy;
            if (cursed) {
                /* salle maudite : gros butin mais pieges et porte qui mord */
                r->kind = RM_MAUDITE;
                put_obst(r, OB_SPIKE, 8, 6);
                put_obst(r, OB_SPIKE, 4, 4);
                put_obst(r, OB_SPIKE, 12, 4);
                r->pick[0].type = PK_LOCKED;
                r->pick[0].tx = 5;  r->pick[0].ty = 8;
                r->pick[0].x = (int16_t)(5 * 2 * TILE + TILE);
                r->pick[0].y = (int16_t)(8 * 2 * TILE + TILE);
                r->pick[1].type = PK_SILVER;
                r->pick[1].tx = 11; r->pick[1].ty = 8;
                r->pick[1].x = (int16_t)(11 * 2 * TILE + TILE);
                r->pick[1].y = (int16_t)(8 * 2 * TILE + TILE);
                r->n_pick = 2;
            } else {
                r->kind = RM_SACRI;
                put_obst(r, OB_SPIKE, 8, 6);   /* pic central du sacrifice */
            }
            g->n_rooms++;
        }
    }

    /* --- remplissage des salles normales ----------------------------------- */
    for (i = 0; i < g->n_rooms; i++) {
        RoomState *r = &g->rooms[i];
        if (r->kind != RM_NORMAL) continue;
        layout_obstacles(r);
        /* ~1 chance sur 10 d'un pickup initial */
        if (rng_range(10) == 0 && r->n_pick < MAX_PICKUPS) {
            uint8_t type = (uint8_t)(PK_HEART + rng_range(5));
            if (type == PK_HEART && rng_range(2)) type = PK_HALF;
            if (type == PK_COIN && rng_range(6) == 0) type = PK_SILVER;
            if (rng_range(12) == 0) type = PK_SOUL;   /* coeur d'ame, rare */
            r->pick[r->n_pick].type = type;
            r->pick[r->n_pick].tx = (uint8_t)(3 + rng_range(12));
            r->pick[r->n_pick].ty = (uint8_t)(2 + rng_range(9));
            r->pick[r->n_pick].x =
                (int16_t)(r->pick[r->n_pick].tx * 2 * TILE + TILE);
            r->pick[r->n_pick].y =
                (int16_t)(r->pick[r->n_pick].ty * 2 * TILE + TILE);
            r->n_pick++;
        }
        /* coffre simple : on touche, ca s'ouvre */
        if (rng_range(100) < ((g->floor == 1) ? 25 : 30)) {
            uint8_t attempts, tx = 4, ty = 4, ok = 0;
            for (attempts = 0; attempts < 20 && !ok; attempts++) {
                uint8_t o;
                tx = (uint8_t)(3 + rng_range(6));
                ty = (uint8_t)(2 + rng_range(9));
                ok = 1;
                for (o = 0; o < r->n_obst; o++)
                    if (r->obst[o].tx == tx && r->obst[o].ty == ty) ok = 0;
            }
            if (ok && r->n_pick < MAX_PICKUPS) {
                Pickup *p = &r->pick[r->n_pick++];
                p->type = PK_CHEST; p->price = 0;
                p->tx = tx; p->ty = ty;
                p->x = (int16_t)(tx * 2 * TILE + TILE);
                p->y = (int16_t)(ty * 2 * TILE + TILE);
            }
        }
        /* coffre verrouille : cle requise, butin garanti en argent */
        if (rng_range(100) < ((g->floor == 1) ? 12 : 18)) {
            uint8_t attempts, tx = 12, ty = 4, ok = 0;
            for (attempts = 0; attempts < 20 && !ok; attempts++) {
                uint8_t o;
                tx = (uint8_t)(9 + rng_range(6));
                ty = (uint8_t)(2 + rng_range(9));
                ok = 1;
                for (o = 0; o < r->n_obst; o++)
                    if (r->obst[o].tx == tx && r->obst[o].ty == ty) ok = 0;
            }
            if (ok && r->n_pick < MAX_PICKUPS) {
                Pickup *p = &r->pick[r->n_pick++];
                p->type = PK_LOCKED; p->price = 0;
                p->tx = tx; p->ty = ty;
                p->x = (int16_t)(tx * 2 * TILE + TILE);
                p->y = (int16_t)(ty * 2 * TILE + TILE);
            }
        }
    }

    {
        /* items : pool commun et pool rare, rarete croissante avec l'etage */
        static const uint8_t pool_commun[11] = {
            IT_ONION, IT_BREAKFAST, IT_SPOON, IT_BLOOD, IT_LUNCH, IT_NUM1,
            IT_BOUSSOLE, IT_AIMANT, IT_BOTTES, IT_STEAK, IT_CRANE
        };
        static const uint8_t pool_rare[10] = {
            IT_EYE, IT_MUSH, IT_AIGUILLE, IT_SPECTRE, IT_TELEGUIDE,
            IT_MASSE, IT_SOUFFLE, IT_DEMON, IT_VENIN, IT_TREFLE
        };
        uint8_t rare_pct = (uint8_t)((floor == 1) ? 15
                                  : (floor == 2) ? 40 : 65);
        uint8_t tries3;
        g->item = (rng_range(100) < rare_pct)
            ? pool_rare[rng_range(10)] : pool_commun[rng_range(11)];
        g->item2 = (rng_range(100) < (uint8_t)(rare_pct / 2))
            ? pool_rare[rng_range(10)] : pool_commun[rng_range(11)];
        for (tries3 = 0; tries3 < 6 && g->item2 == g->item; tries3++)
            g->item2 = pool_commun[rng_range(11)];
    }

    /* position : salle de depart */
    g->cur = (uint8_t)(g->grid[start_y][start_x] - 1);
}
