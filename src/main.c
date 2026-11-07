/* main.c — Mini Binding of Isaac, TI-83 Premium CE (eZ80). */
#include <graphx.h>
#include <keypadc.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

#include "types.h"
#include "gen.h"
#include "rng.h"
#include "palette.h"
#include "play.h"
#include "save.h"
#include "selftest.h"
#include "gfx/gfx.h"

enum { ST_TITLE = 0, ST_SAVE, ST_CHAR, ST_PLAY, ST_MAP, ST_OVER, ST_WIN };

extern uint8_t play_char_id;    /* personnage courant (defini dans play.c) */

static Game game;
static Isaac isaac;
static SaveSlot sv;             /* slot courant (meta + run) */
static SaveSlot scratchsv;      /* lecture menu / effacement */
static uint8_t cur_slot;         /* 0..2 */
static uint8_t save_sel, char_sel, confirm_clear;
static uint16_t run_sec;         /* chrono de la run en cours */
static uint8_t frame_tick;
static uint8_t laz_used;         /* resurrection de Lazare consommee */
static uint8_t map_page;         /* ST_MAP : 0 = carte, 1 = stats */
static uint8_t prev_up, prev_dn, prev_ent, prev_yeq, prev_mode;
static uint8_t prev_lt, prev_rt;

typedef struct {
    uint8_t used, in_progress, char_id, wins_acc, unlocked, floor;
    uint16_t run_time, kills_acc;
} SlotInfo;
static SlotInfo sinfo[3];

static const char *const char_names[4] =
    { "ISAAC", "MAGDA", "CAINE", "LAZARE" };
static const char *const char_cond[4] =
    { "", "GAGNER 1 PARTIE", "100 ENNEMIS (TOTAL)", "GAGNER 3 PARTIES" };

/* --- sync 30 FPS : 32768 / 30 = 1092 ticks/frame --- */
static void frame_wait(void)
{
    static clock_t next = 0;
    clock_t now = clock();
    if ((int32_t)(now - next) < 0) {
        while ((int32_t)(clock() - next) < 0) { }
        next += 1092;
    } else {
        next = now + 1092;
    }
}

/* cellule grille de la salle courante */
static uint8_t cur_cell(const Game *g, uint8_t *cx, uint8_t *cy)
{
    uint8_t x, y;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (g->grid[y][x] == g->cur + 1) {
                *cx = x;
                *cy = y;
                return 1;
            }
    return 0;
}

static void char_stats(uint8_t ch, uint8_t *hp, uint8_t *sp,
                       uint8_t *dm, uint8_t *pc)
{
    *hp = 6; *sp = 12; *dm = 7; *pc = 0;
    switch (ch) {
    case CHAR_MAGDA:  *hp = 8;  *sp = 10; *dm = 8; break;
    case CHAR_CAINE:  *sp = 14; *dm = 6;  *pc = 5; break;
    case CHAR_LAZARE: *hp = 4; break;
    default: break;
    }
}

/* mm:ss */
static void print_time(uint16_t sec)
{
    gfx_PrintUInt(sec / 60, 2);
    gfx_PrintChar(':');
    if (sec % 60 < 10) gfx_PrintChar('0');
    gfx_PrintUInt(sec % 60, 2);
}

/* ecrit le slot courant (sv + run en cours) dans son AppVar.
   Toujours archive : une sauvegarde laissee en RAM seule serait perdue
   par un reset ou pendant le transfert d'une nouvelle version. */
static void save_sync(void)
{
    if ((uint16_t)(sv.kills_acc + game.kills) >= 100)
        sv.unlocked |= CHAR_UNLOCK_CAINE;
    save_pack(&sv, &game, &isaac, run_sec);
    save_write(cur_slot, &sv, 1);
}

/* recharge les infos des 3 slots pour le menu */
static void refresh_infos(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        SlotInfo *si = &sinfo[i];
        memset(si, 0, sizeof *si);
        if (save_read(i, &scratchsv)) {
            si->used = 1;
            si->in_progress = scratchsv.in_progress;
            si->char_id = scratchsv.char_id;
            si->wins_acc = scratchsv.wins_acc;
            si->unlocked = scratchsv.unlocked;
            si->floor = scratchsv.floor;
            si->run_time = scratchsv.run_time;
            si->kills_acc = scratchsv.kills_acc;
        }
    }
}

static void run_new(uint8_t ch)
{
    uint32_t s = (uint32_t)clock();
    s = s * 1664525u + 1013904223u;
    if (!s) s = 42u;
    rng_seed(s);
    game.kills = 0;
    game.dmg_floor = 0;
    game.dmg_run = 0;
    gen_floor(&game, 1);
    game.seed_pub = (uint16_t)(s >> 16);
    play_reset_isaac(&isaac);
    {
        uint8_t hp, sp, dm, pc;
        char_stats(ch, &hp, &sp, &dm, &pc);
        isaac.hp = hp;
        isaac.hp_max = hp;
        isaac.speed = sp;
        isaac.dmg = dm;
        isaac.coins = pc;
    }
    play_char_id = ch;
    sv.char_id = ch;
    sv.in_progress = 1;
    run_sec = 0;
    frame_tick = 0;
    laz_used = 0;
    play_start_floor(&game, &isaac);
    save_sync();
}

/* reprise de la run sauvegardee dans sv */
static void run_continue(void)
{
    uint32_t t = (uint32_t)clock();
    save_unpack(&game, &isaac, &sv);
    play_char_id = (uint8_t)(sv.char_id % CHAR_NUM1);
    run_sec = sv.run_time;
    frame_tick = 0;
    laz_used = 0;
    rng_seed(t | 1u);
    play_enter_room(&game, &isaac, -1);
}

/* cloture la run (victoire ou defaite) */
static void run_finish(uint8_t won)
{
    sv.kills_acc = (uint16_t)(sv.kills_acc + game.kills);
    if (won) {
        sv.wins_acc = (uint8_t)(sv.wins_acc + 1);
        sv.unlocked |= CHAR_UNLOCK_MAGDA;
        if (sv.wins_acc >= 3) sv.unlocked |= CHAR_UNLOCK_LAZARE;
    }
    sv.in_progress = 0;
    save_sync();
}

static void draw_title(void)
{
    gfx_FillScreen(COL_FLOOR);
    gfx_SetTextFGColor(COL_BLOOD_DK);
    gfx_SetTextScale(3, 3);
    gfx_PrintStringXY("MINI ISAAC", 44, 40);
    gfx_SetTextScale(1, 1);
    gfx_PrintStringXY("3 ETAGES - BOSS FINAL : MAMAN", 68, 76);
    gfx_ScaledTransparentSprite_NoClip((gfx_sprite_t *)isaac_0, 128, 108, 4, 4);
    gfx_SetTextFGColor(COL_BLACK);
    gfx_PrintStringXY("ENTREE : SAUVEGARDES", 100, 196);
    gfx_PrintStringXY("[Yequ] : SELFTEST", 112, 210);
    gfx_PrintStringXY("BOUGE : 4 8 6 2   TIR : 5 OU FLECHES", 78, 222);
    gfx_PrintStringXY("BOMBE : ALPHA OU +   ITEM ACTIF : STATS", 70, 232);
    gfx_SwapDraw();
}

/* --- menu des 3 sauvegardes --- */
static void draw_save_screen(uint8_t sel, uint8_t confirm)
{
    uint8_t i;
    gfx_FillScreen(COL_BLACK);
    gfx_SetTextFGColor(COL_GOLD);
    gfx_SetTextScale(2, 2);
    gfx_PrintStringXY("SAUVEGARDES", 85, 6);
    gfx_SetTextScale(1, 1);
    for (i = 0; i < 3; i++) {
        int y = 48 + i * 24;
        gfx_SetTextFGColor((i == sel) ? COL_GOLD : COL_OFFWHITE);
        gfx_SetTextXY(34, y + 5);
        gfx_PrintUInt(i + 1, 1);
        gfx_PrintChar('.');
        if (!sinfo[i].used) {
            gfx_PrintStringXY("---- VIERGE ----", 74, y + 5);
        } else if (sinfo[i].in_progress) {
            gfx_PrintStringXY(char_names[sinfo[i].char_id % 4], 74, y + 5);
            gfx_PrintStringXY(sinfo[i].floor == 1 ? "B1"
                            : sinfo[i].floor == 2 ? "B2" : "B3", 144, y + 5);
            gfx_SetTextXY(172, y + 5);
            print_time(sinfo[i].run_time);
            gfx_PrintStringXY("EN COURS", 244, y + 5);
        } else {
            gfx_PrintStringXY(char_names[sinfo[i].char_id % 4], 74, y + 5);
            gfx_SetTextXY(144, y + 5);
            gfx_PrintString("V:");
            gfx_PrintUInt(sinfo[i].wins_acc, 1);
            gfx_PrintString(" K:");
            gfx_PrintUInt(sinfo[i].kills_acc, 3);
            gfx_PrintStringXY("LIBRE", 254, y + 5);
        }
        if (i == sel) {
            gfx_SetColor(COL_GOLD);
            gfx_Rectangle(18, y, 284, 16);
        }
    }
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("8/2 : CHOISIR   ENTREE : OK", 80, 136);
    gfx_PrintStringXY("Yequ : EFFACER RUN   MODE : RETOUR", 68, 150);
    if (confirm) {
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("EFFACER LA RUN ?  ENTREE : OUI", 62, 180);
    }
    gfx_SwapDraw();
}

/* --- menu de personnage : carrousel --- */
static void draw_char_screen(uint8_t sel)
{
    uint8_t hp, sp, dm, pc;
    uint8_t locked = (sel != CHAR_ISAAC)
                   && !(sv.unlocked & (uint8_t)(1u << (sel - 1)));

    gfx_FillScreen(COL_BLACK);
    gfx_SetTextFGColor(COL_GOLD);
    gfx_SetTextScale(2, 2);
    gfx_PrintStringXY("PERSONNAGE", 86, 4);
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_SetTextXY(112, 34);
    gfx_PrintString("SLOT ");
    gfx_PrintUInt(cur_slot + 1, 1);
    gfx_PrintString(" / PERSO ");
    gfx_PrintUInt(sel + 1, 1);
    gfx_PrintString("/4");

    /* fleches du carrousel */
    gfx_SetTextScale(3, 3);
    gfx_SetTextFGColor(COL_GOLD);
    gfx_PrintStringXY("<", 36, 88);
    gfx_PrintStringXY(">", 272, 88);
    gfx_SetTextScale(1, 1);

    /* grand portrait reel du personnage, x3 */
    {
        const gfx_sprite_t *port = isaac_0;
        if (sel == CHAR_MAGDA)       port = magda_0;
        else if (sel == CHAR_CAINE)  port = caine_0;
        else if (sel == CHAR_LAZARE) port = lazare_0;
        gfx_ScaledTransparentSprite_NoClip((gfx_sprite_t *)port, 136, 56, 3, 3);
    }

    /* nom */
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(locked ? COL_GREY : COL_GOLD);
    {
        uint24_t w = gfx_GetStringWidth(char_names[sel]);
        gfx_PrintStringXY(char_names[sel], (uint24_t)(160 - w), 112);
    }
    gfx_SetTextScale(1, 1);

    char_stats(sel, &hp, &sp, &dm, &pc);
    if (locked) {
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("VERROUILLE",
            (uint24_t)(160 - gfx_GetStringWidth("VERROUILLE") / 2), 140);
        gfx_SetTextFGColor(COL_OFFWHITE);
        {
            const char *c = char_cond[sel];
            gfx_PrintStringXY(c, (uint24_t)(160 - gfx_GetStringWidth(c) / 2), 156);
        }
    } else {
        gfx_SetTextFGColor(COL_OFFWHITE);
        gfx_SetTextXY(117, 140);
        gfx_PrintString("PV ");
        gfx_PrintUInt(hp / 2, 1);
        gfx_PrintString("  VIT ");
        gfx_PrintUInt(sp, 2);
        gfx_PrintString("  DMG ");
        gfx_PrintUInt(dm, 2);
        gfx_SetTextXY(sel == CHAR_LAZARE ? 110 : 136, 156);
        gfx_PrintString("PIECES ");
        gfx_PrintUInt(pc, 2);
        if (sel == CHAR_MAGDA || sel == CHAR_LAZARE) {
            gfx_SetTextFGColor(COL_GOLD);
            gfx_PrintString(sel == CHAR_MAGDA ? " TANK" : " RESURRECTION");
        }
    }

    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("4/6 : CHOISIR   ENTREE : JOUER", 95, 212);
    gfx_PrintStringXY("MODE : RETOUR", 131, 222);
    gfx_SwapDraw();
}

/* --- minimap plein ecran --- */
static void draw_map(const Game *g, uint16_t sec)
{
    uint8_t cx, cy;
    uint8_t comp = (isaac.has_item & (uint32_t)(1UL << IT_BOUSSOLE)) != 0;

    gfx_FillScreen(COL_BLACK);
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_SetTextScale(2, 2);
    gfx_PrintStringXY(g->floor == 1 ? "BASEMENT I"
                    : g->floor == 2 ? "BASEMENT II" : "L'ANTRE", 76, 8);
    gfx_SetTextScale(1, 1);
    gfx_PrintStringXY("[MODE] : STATS   [ENTREE] : REPRENDRE", 42, 32);
    gfx_SetTextXY(230, 8);
    gfx_PrintString("TEMPS ");
    print_time(sec);

    for (cy = 0; cy < MAP_H; cy++) {
        for (cx = 0; cx < MAP_W; cx++) {
            uint8_t id = g->grid[cy][cx];
            const RoomState *r;
            int x = 52 + (int)cx * 24;
            int y = 52 + (int)cy * 20;
            uint8_t col;

            if (!id) continue;
            r = &g->rooms[id - 1];

            /* secrete : visible seulement si visitee ou revelee par bombe ;
               diable : reveal seulement quand le pacte est ouvert */
            if ((r->flags & 1)
                || (r->kind == RM_SECRET && (r->flags & SECRET_BIT))
                || (r->kind == RM_DEVIL && (r->flags & 4))
                || (comp && r->kind != RM_SECRET && r->kind != RM_DEVIL)) {
                col = (r->kind == RM_BOSS)     ? COL_BLOOD :
                      (r->kind == RM_DEVIL)    ? COL_BLOOD_DK :
                      (r->kind == RM_MAUDITE)  ? COL_BLOOD_DK :
                      (r->kind == RM_TREASURE) ? COL_GOLD   :
                      (r->kind == RM_SECRET)   ? COL_OFFWHITE :
                      (r->kind == RM_START)    ? COL_WHITE  : COL_GREY;
                gfx_SetColor(col);
                gfx_FillRectangle(x + 6, y + 6, 12, 8);
                /* liaisons vers voisins visites */
                if (cx + 1 < MAP_W && (g->grid[cy][cx + 1]) &&
                    (g->rooms[g->grid[cy][cx + 1] - 1].flags & 1))
                    gfx_FillRectangle(x + 18, y + 9, 12, 2);
                if (cy + 1 < MAP_H && (g->grid[cy + 1][cx]) &&
                    (g->rooms[g->grid[cy + 1][cx] - 1].flags & 1))
                    gfx_FillRectangle(x + 11, y + 14, 2, 12);
                /* sigle par type de salle */
                gfx_SetTextScale(1, 1);
                gfx_SetTextFGColor(COL_BLACK);
                if (r->kind == RM_BOSS)
                    gfx_PrintStringXY("!", x + 9, y + 6);
                else if (r->kind == RM_TREASURE)
                    gfx_PrintStringXY("T", x + 9, y + 6);
                else if (r->kind == RM_SHOP)
                    gfx_PrintStringXY("S", x + 9, y + 6);
                else if (r->kind == RM_SECRET)
                    gfx_PrintStringXY("?", x + 9, y + 6);
                else if (r->kind == RM_DEVIL)
                    gfx_PrintStringXY("D", x + 9, y + 6);
                else if (r->kind == RM_SACRI)
                    gfx_PrintStringXY("+", x + 9, y + 6);
                else if (r->kind == RM_MAUDITE)
                    gfx_PrintStringXY("M", x + 9, y + 6);
            } else if (!(r->kind == RM_SECRET && !(r->flags & SECRET_BIT))
                       && !(r->kind == RM_DEVIL && !(r->flags & 4))) {
                /* apercu brouillard */
                uint8_t near =
                    (cx > 0 && g->grid[cy][cx - 1] &&
                     (g->rooms[g->grid[cy][cx - 1] - 1].flags & 1)) ||
                    (cx + 1 < MAP_W && g->grid[cy][cx + 1] &&
                     (g->rooms[g->grid[cy][cx + 1] - 1].flags & 1)) ||
                    (cy > 0 && g->grid[cy - 1][cx] &&
                     (g->rooms[g->grid[cy - 1][cx] - 1].flags & 1)) ||
                    (cy + 1 < MAP_H && g->grid[cy + 1][cx] &&
                     (g->rooms[g->grid[cy + 1][cx] - 1].flags & 1));
                if (near && !(g->curse & CURSE_DARKNESS)) {
                    gfx_SetColor(COL_WALL_DD);
                    gfx_FillRectangle(x + 6, y + 6, 12, 8);
                }
            }

            if (id - 1 == g->cur) {                    /* salle courante */
                gfx_SetColor(COL_OFFWHITE);
                gfx_Rectangle(x + 3, y + 3, 18, 14);
            }
        }
    }
    gfx_SwapDraw();
}

/* --- page stats de la map --- */
static void draw_stats(const Game *g, const Isaac *iz, uint16_t sec)
{
    uint8_t i, shown = 0;
    gfx_FillScreen(COL_BLACK);
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(COL_GOLD);
    gfx_PrintStringXY("STATISTIQUES", 76, 8);
    gfx_SetTextScale(1, 1);
    gfx_SetTextXY(226, 8);
    gfx_PrintString("TEMPS ");
    print_time(sec);

    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_SetTextXY(30, 44);
    gfx_PrintString("PV ");
    gfx_PrintUInt(iz->hp, 3);
    gfx_PrintString("/");
    gfx_PrintUInt(iz->hp_max, 3);
    if (iz->hp_soul) {
        gfx_PrintString("  AME ");
        gfx_PrintUInt(iz->hp_soul, 2);
    }
    gfx_PrintStringXY("DMG", 30, 60);
    gfx_SetTextXY(74, 60);
    gfx_PrintUInt(iz->dmg, 2);
    gfx_PrintStringXY("VIT", 170, 60);
    gfx_SetTextXY(214, 60);
    gfx_PrintUInt(iz->speed, 2);
    gfx_PrintStringXY("CAD", 30, 74);
    gfx_SetTextXY(74, 74);
    gfx_PrintUInt(iz->fire_delay, 2);
    gfx_PrintStringXY("PORTEE", 170, 74);
    gfx_SetTextXY(226, 74);
    gfx_PrintUInt(iz->range, 3);

    /* item actif TELE : jauge de charge en kills */
    {
        uint8_t ch = play_tele_charge();
        gfx_SetTextXY(30, 92);
        gfx_PrintString("TELE ");
        gfx_PrintUInt(ch, 2);
        gfx_PrintString(" KILLS");
        gfx_SetColor(COL_WALL_DD);
        gfx_FillRectangle(150, 90, 75, 9);
        gfx_SetColor(COL_GOLD);
        gfx_FillRectangle(150, 90, (uint24_t)(75 * ch / TELE_MAX), 9);
    }

    /* malediction de l'etage */
    if (g->curse) {
        gfx_SetTextFGColor(COL_BLOOD);
        gfx_PrintStringXY("MALEDICTION DE L'ETAGE !", 30, 108);
    }

    /* objets acquis */
    gfx_SetTextFGColor(COL_GOLD);
    gfx_PrintStringXY("OBJETS :", 30, 124);
    for (i = 1; i < 24; i++) {
        if (iz->has_item & (uint32_t)(1UL << i)) {
            if (shown < 10)
                gfx_PrintStringXY(play_item_name(i),
                    (uint24_t)(30 + (shown % 2) * 145),
                    (uint8_t)(136 + (shown / 2) * 11));
            shown++;
        }
    }
    if (!shown) {
        gfx_SetTextFGColor(COL_OFFWHITE);
        gfx_PrintStringXY("AUCUN POUR L'INSTANT", 30, 136);
    }
    if (shown > 10) {
        gfx_SetTextFGColor(COL_OFFWHITE);
        gfx_SetTextXY(196, 190);
        gfx_PrintChar('+');
        gfx_PrintUInt((uint8_t)(shown - 10), 2);
    }
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("[MODE] : CARTE   [ENTREE] : REPRENDRE", 42, 210);
    gfx_SwapDraw();
}

static void draw_end(uint8_t win, uint16_t sec)
{
    uint8_t i, shown = 0, tot = 0;
    gfx_FillScreen(COL_BLACK);
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(win ? COL_GOLD : COL_BLOOD);
    gfx_PrintStringXY(win ? "VICTOIRE !" : "VOUS ETES MORT", win ? 92 : 76, 44);
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("ENNEMIS VAINCUS : ", 56, 80);
    gfx_PrintUInt(game.kills, 3);
    gfx_PrintStringXY("PIECES : ", 76, 96);
    gfx_PrintUInt(isaac.coins, 3);
    gfx_PrintStringXY("TEMPS : ", 97, 112);
    print_time(sec);
    gfx_SetTextFGColor(COL_GOLD);
    gfx_PrintStringXY("OBJETS :", 56, 132);
    for (i = 1; i < 24; i++) {
        if (isaac.has_item & (uint32_t)(1UL << i)) {
            if (shown < 6) {
                gfx_PrintStringXY(play_item_name(i),
                    (uint24_t)(56 + (shown & 1) * 138),
                    (uint8_t)(144 + (shown >> 1) * 12));
                shown++;
            }
            tot++;
        }
    }
    if (tot > 6) {
        gfx_SetTextXY(56, 180);
        gfx_PrintChar('+');
        gfx_PrintUInt((uint8_t)(tot - 6), 1);
    }
    if (win && game.dmg_run == 0) {
        gfx_SetTextFGColor(COL_GOLD);
        gfx_PrintStringXY("RUN PARFAITE !", 108, 192);
    }
    if (win) {
        gfx_SetTextFGColor(COL_GOLD);
        gfx_PrintStringXY("MERCI D'AVOIR JOUE", 99, 210);
    }
    gfx_SetTextFGColor(COL_OFFWHITE);
    gfx_PrintStringXY("ENTREE : MENU", 120, 228);
    gfx_SwapDraw();
}

int main(void)
{
    uint8_t st = ST_TITLE;
    uint8_t prevalpha = 0, prevmode = 0, prevplus = 0;
    uint8_t running = 1;

    gfx_Begin();
    gfx_SetPalette(global_palette, sizeof_global_palette, 0);
    gfx_SetTransparentColor(COL_TRANSP);
    gfx_SetDraw(gfx_buffer);

    draw_title();

    while (running) {
        kb_Scan();

        if (st == ST_TITLE) {
            if (kb_Data[6] & kb_Enter) {
                refresh_infos();
                save_sel = 0;
                confirm_clear = 0;
                prev_ent = 1;      /* Enter encore maintenu */
                draw_save_screen(save_sel, confirm_clear);
                st = ST_SAVE;
            } else if (kb_Data[1] & kb_Yequ) {
                selftest_run();
                draw_title();
            }
        } else if (st == ST_SAVE) {
            uint8_t up   = (kb_Data[4] & kb_8) || (kb_Data[7] & kb_Up);
            uint8_t dn   = (kb_Data[4] & kb_2) || (kb_Data[7] & kb_Down);
            uint8_t ent  = kb_Data[6] & kb_Enter;
            uint8_t yeq  = kb_Data[1] & kb_Yequ;
            uint8_t mode = kb_Data[1] & kb_Mode;

            if (up && !prev_up) { save_sel = (uint8_t)((save_sel + 2) % 3); confirm_clear = 0; }
            if (dn && !prev_dn) { save_sel = (uint8_t)((save_sel + 1) % 3); confirm_clear = 0; }
            if (yeq && !prev_yeq)
                confirm_clear = (sinfo[save_sel].in_progress)
                              ? (uint8_t)!confirm_clear : 0;
            if (ent && !prev_ent) {
                if (confirm_clear) {
                    save_read(save_sel, &scratchsv);
                    scratchsv.in_progress = 0;
                    save_write(save_sel, &scratchsv, 1);
                    refresh_infos();
                    confirm_clear = 0;
                } else if (sinfo[save_sel].in_progress) {
                    cur_slot = save_sel;
                    save_read(save_sel, &sv);
                    run_continue();
                    st = ST_PLAY;
                } else {
                    cur_slot = save_sel;
                    if (sinfo[save_sel].used) save_read(save_sel, &sv);
                    else { memset(&sv, 0, sizeof sv); sv.char_id = CHAR_ISAAC; }
                    char_sel = 0;
                    prev_ent = 1;  /* Enter encore maintenu */
                    st = ST_CHAR;
                }
            }
            if (mode && !prev_mode) {
                draw_title();
                st = ST_TITLE;
            }
            prev_up = up; prev_dn = dn; prev_ent = ent;
            prev_yeq = yeq; prev_mode = mode;
            if (st == ST_SAVE) draw_save_screen(save_sel, confirm_clear);
        } else if (st == ST_CHAR) {
            uint8_t up   = (kb_Data[4] & kb_8) || (kb_Data[7] & kb_Up);
            uint8_t dn   = (kb_Data[4] & kb_2) || (kb_Data[7] & kb_Down);
            uint8_t lt   = (kb_Data[3] & kb_4) || (kb_Data[7] & kb_Left);
            uint8_t rt   = (kb_Data[5] & kb_6) || (kb_Data[7] & kb_Right);
            uint8_t ent  = kb_Data[6] & kb_Enter;
            uint8_t mode = kb_Data[1] & kb_Mode;

            if (up && !prev_up) char_sel = (uint8_t)((char_sel + 3) % 4);
            if (dn && !prev_dn) char_sel = (uint8_t)((char_sel + 1) % 4);
            if (lt && !prev_lt) char_sel = (uint8_t)((char_sel + 3) % 4);
            if (rt && !prev_rt) char_sel = (uint8_t)((char_sel + 1) % 4);
            if (ent && !prev_ent) {
                uint8_t locked = (char_sel != CHAR_ISAAC)
                    && !(sv.unlocked & (uint8_t)(1u << (char_sel - 1)));
                if (!locked) {
                    run_new(char_sel);
                    st = ST_PLAY;
                }
            }
            if (mode && !prev_mode) {
                st = ST_SAVE;
                prev_mode = 1;   /* encore maintenu */
            }
            prev_up = up; prev_dn = dn; prev_lt = lt; prev_rt = rt;
            prev_ent = ent; prev_mode = mode;
            if (st == ST_CHAR) draw_char_screen(char_sel);
        } else if (st == ST_PLAY) {
            Input in;
            uint8_t dir = 0;
            uint8_t ev;
            uint8_t a_now = (kb_Data[2] & kb_Alpha) != 0;
            uint8_t p_now = (kb_Data[6] & kb_Add) != 0;
            uint8_t m_now = (kb_Data[1] & kb_Mode) != 0;
            static uint8_t prevstat = 0;
            uint8_t st_now = (kb_Data[4] & kb_Stat) != 0;

            /* deplacement : 4 / 8 / 6 / 2 ; tir : fleches ou [5] */
            in.up    = (kb_Data[4] & kb_8) != 0;
            in.down  = (kb_Data[4] & kb_2) != 0;
            in.left  = (kb_Data[3] & kb_4) != 0;
            in.right = (kb_Data[5] & kb_6) != 0;
            in.f_up     = (kb_Data[7] & kb_Up) != 0;
            in.f_down   = (kb_Data[7] & kb_Down) != 0;
            in.f_left   = (kb_Data[7] & kb_Left) != 0;
            in.f_right  = (kb_Data[7] & kb_Right) != 0;
            in.f_center = (kb_Data[4] & kb_5) != 0;
            in.bomb  = (a_now && !prevalpha) || (p_now && !prevplus);
            in.map   = m_now && !prevmode;
            in.active = st_now && !prevstat;
            in.quit  = 0;
            prevalpha = a_now;
            prevplus = p_now;
            prevmode = m_now;
            prevstat = st_now;

            ev = play_update(&game, &isaac, &in, &dir);

            if (ev == EV_TRANSIT) {
                uint8_t cx, cy;
                if (cur_cell(&game, &cx, &cy)) {
                    uint8_t nb = gen_neighbor(&game, cx, cy, dir);
                    if (nb) {
                        uint8_t wipe;
                        game.cur = (uint8_t)(nb - 1);
                        play_enter_room(&game, &isaac, dir ^ 1);
                        /* balayage noir pendant le changement de salle */
                        for (wipe = 0; wipe < 3; wipe++) {
                            gfx_FillScreen(COL_BLACK);
                            gfx_SetColor(COL_WALL_DD);
                            gfx_FillRectangle(0, (uint8_t)(30 * wipe), 320, 30);
                            gfx_SwapDraw();
                            frame_wait();
                        }
                        /* sauvegarde archivee a chaque changement de
                           salle : survit aux resets et aux mises a
                           jour du programme */
                        save_sync();
                    }
                }
            } else if (ev == EV_TELE) {
                /* item TELE : saut vers une salle deja visitee */
                uint8_t tries;
                for (tries = 0; tries < 40; tries++) {
                    uint8_t idx = (uint8_t)rng_range(game.n_rooms);
                    const RoomState *rn = &game.rooms[idx];
                    if (idx == game.cur) continue;
                    if (!(rn->flags & 1)) continue;
                    if (rn->kind == RM_SECRET && !(rn->flags & SECRET_BIT))
                        continue;
                    if (rn->kind == RM_DEVIL && !(rn->flags & 4)) continue;
                    game.cur = idx;
                    play_enter_room(&game, &isaac, -1);
                    {
                        uint8_t wipe;
                        for (wipe = 0; wipe < 3; wipe++) {
                            gfx_FillScreen(COL_BLACK);
                            gfx_SetColor(COL_WALL_DD);
                            gfx_FillRectangle(0, (uint8_t)(30 * wipe), 320, 30);
                            gfx_SwapDraw();
                            frame_wait();
                        }
                    }
                    save_sync();
                    break;
                }
            } else if (ev == EV_NEXT_FLOOR) {
                if (game.floor < 3) {
                    gen_floor(&game, (uint8_t)(game.floor + 1));
                    play_start_floor(&game, &isaac);
                    save_sync();
                }
            } else if (ev == EV_DEAD) {
                if (play_char_id == CHAR_LAZARE && !laz_used) {
                    laz_used = 1;
                    isaac.hp = 2;
                    isaac.iframe = 120;
                    play_notify("LAZARE", "IL REVIT !");
                } else {
                    run_finish(0);
                    st = ST_OVER;
                }
            } else if (ev == EV_WIN) {
                run_finish(1);
                st = ST_WIN;
            }

            /* chrono : play_update tourne a 30 FPS */
            if (st == ST_PLAY) {
                frame_tick++;
                if (frame_tick >= 30) {
                    frame_tick = 0;
                    run_sec++;
                }
            }

            play_draw(&game, &isaac);
            play_draw_hud(&game, &isaac);
            gfx_SwapDraw();
            frame_wait();

            if (st == ST_PLAY && in.map) {
                st = ST_MAP;
                prevmode = 1;
                map_page = 0;
                draw_map(&game, run_sec);
            } else if (st != ST_PLAY) {
                draw_end(st == ST_WIN, run_sec);
            }
            continue;
        } else if (st == ST_MAP) {
            uint8_t m_now = (kb_Data[1] & kb_Mode) != 0;
            if (!m_now) {
                prevmode = 0;
            } else if (m_now && !prevmode) {
                prevmode = 1;
                map_page = !map_page;      /* MODE : carte <-> stats */
                if (map_page) draw_stats(&game, &isaac, run_sec);
                else draw_map(&game, run_sec);
            }
            if (kb_Data[6] & kb_Enter) {
                play_draw(&game, &isaac);
                play_draw_hud(&game, &isaac);
                gfx_SwapDraw();
                st = ST_PLAY;
                map_page = 0;
            }
        } else {                                       /* OVER / WIN */
            if (kb_Data[6] & kb_Enter) {
                draw_title();
                st = ST_TITLE;
            }
        }

        if ((st == ST_PLAY || st == ST_MAP) && (kb_Data[6] & kb_Clear)) {
            save_sync();          /* quitte en cours de partie : on sauve */
            running = 0;
        } else if (st != ST_TITLE && (kb_Data[6] & kb_Clear)) {
            running = 0;
        }
        frame_wait();
    }

    gfx_End();
    return 0;
}
