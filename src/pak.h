/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PAK_H
#define PAK_H

/* Mission pack written by tools/convert.py (version 2). */

#define MCD_STOP_LOS    0x01
#define MCD_NO_FLOOR    0x02
#define MCD_BIG_WALL    0x04
#define MCD_DOOR        0x08
#define MCD_UFO_DOOR    0x10
#define MCD_GRAVLIFT    0x20
#define MCD_EXPL        0x40    /* explodes when destroyed (HE_Strength: Pack.expl) */
#define MCD_SMOKE       0x80    /* Block_Smoke: stops smoke (an object) */

#define PART_FLOOR      0
#define PART_WESTWALL   1
#define PART_NORTHWALL  2
#define PART_OBJECT     3

#define SPECIAL_START   1   /* X-COM start tile */
#define SPECIAL_END     13  /* exit area (END_POINT): Mars, to the second stage */
#define SPECIAL_XBASE   64  /* X-COM base object (MCD isBaseModule; BASEx.PAK from
                               the facility damage on, tools/convert.py) */

#define MAX_EXPL        8   /* explosive MCD records (Pack.expl) */
#define MAX_LIGHTS      8   /* MCD records that are light sources (Pack.lights) */
#define UNIT_FRAMES     40  /* 8 directions x (stand + 4 walk phases) */
#define MAX_UNIT_SETS   16  /* mission, race, terror units, armours, HWPs */

#pragma pack(1)
typedef struct {
    unsigned short sprite;
    unsigned char  tile_type;
    unsigned char  p_level;     /* sprite y offset */
    signed char    t_level;     /* standing height offset (negative = up) */
    unsigned char  tu_walk;
    unsigned char  vcol;        /* voxel column (src/vox.c; the MCD's sliding TU
                                   byte, unused, tools/convert.py loft_section) */
    unsigned char  tu_fly;
    unsigned char  flags;
    unsigned char  armor;
    unsigned char  die_mcd, alt_mcd;
    unsigned char  special;
    unsigned char  burn;        /* big wall kind (MCD_BW) << 4 | fuel (MCD_FUEL);
                                   the Flammable byte is MCD_FLAM */
    unsigned short or_sprite;   /* floors: OR-format ground floor sprite (OR_SPRITE);
                                   packs with g_pak.occ: cover data (render.c) */
} Mcd;

/* The OR sprite of a floor record, OR_NONE if it has none. */
#define OR_NONE         0x7FFF
#define OR_SPRITE(m)    ((m)->or_sprite & OR_NONE)

/* Mcd.tile_type: the part (PART_*) in the low 2 bits; above them the
   index of its HE blockage (the pack trailer's "XHB1") in Pack.he, which
   holds the battle's different HE blockages (at most 32: a pack has up to
   28; more take the last one); MCD_LIGHT: a light source ("XLT1"). */
#define MCD_PART(m)     ((m)->tile_type & 3)
#define MCD_HE(m)       (g_pak.he[(m)->tile_type >> 2 & 31])
#define MCD_LIGHT       0x80
#define MAX_HE_VALUES   32
/* Record r's Flammable (DOS MCD byte 45: 255 = never burns; "XFM1"),
   after the HE blockages in Pack.he. Record 0 (nothing) has 255. */
#define MCD_FLAM(r)     (g_pak.he[MAX_HE_VALUES + (r)])
/* The big wall kind (OpenXcom's bigWall with its MCD patches, how a big
   wall blocks blasts by side: 0 none, 1 block, 2 NE-SW and 3 NW-SE
   diagonals, 4-7 west, north, east, south, 8 east and south, 9 west and
   north) and the fuel (Fuel: rounds a fire burns, less 1). Record 0 has
   both 0. */
#define MCD_BW(m)       ((m)->burn >> 4)
#define MCD_FUEL(m)     ((m)->burn & 15)

typedef struct {
    unsigned char x, y, z, rank;
} RouteNode;

/* An alien rank from a race pack (or a terror pack: terror units). */
typedef struct {
    char name[12];
    unsigned char sprite_set, tu, hp, accuracy, reactions;
    unsigned char armor[3];
    unsigned char weapon;       /* 1 plasma pistol, 2 rifle, 3 heavy, 0 melee */
    unsigned char valid;
    unsigned char flags;        /* AT_ flags */
    unsigned char melee;        /* melee power (strength), 0 = none */
} AlienType;

/* Battle rules of an alien rank: the race pack's "XST1" section (defaults
   for older packs). Damage modifiers in percent by damage type - 1. */
typedef struct {
    unsigned char strength, stamina, bravery, throwing, under, en_rec;
    unsigned char height, kneel, floating;  /* voxels (24 a level) */
    unsigned char width;        /* body width in voxels; 0 = a 2x2 unit */
    unsigned char melee;        /* melee accuracy */
    unsigned char mod[9];       /* AP, IN, HE, laser, plasma, stun, melee, acid, smoke */
    unsigned char live;         /* live alien when captured: IT_LIVE + live - 1, 0 none */
} UnitXStat;

#define AT_BIG      0x01        /* 2x2 unit (cyberdisc, sectopod, reaper) */
#define AT_ZOMBIFY  0x02        /* melee victims rise as zombies (chryssalid) */
#define AT_TERROR   0x04        /* terror unit: built-in weapon, no item sets */
#define AT_FLY      0x08        /* it flies (armors.rul movementType 1) */

typedef struct {
    unsigned char rank, count;
} Spawn;
#pragma pack()

typedef struct {
    unsigned char sx, sy, sz, flags;
    unsigned long file_off;     /* tiles in the pack file: sz * sy * sx * 4 */
} Block;

#define SCRIPT_CRASH    0       /* landing zones, UFO, craft, fill */
#define SCRIPT_URBAN    1       /* terror site: craft, roads, big blocks, fill */
#define SCRIPT_ALIEN_BASE 2     /* entry lifts, command centre, rooms, tunnels */
#define SCRIPT_MARS     3       /* Cydonia surface: craft, pyramid block, fill */
#define SCRIPT_BOSS     4       /* Cydonia base: entry lifts, the brain, tunnels */
#define NO_BLOCK        0xFFFF

typedef struct {
    unsigned n_mcd, n_sprites, n_blocks;
    unsigned map_x, map_y, map_z;
    unsigned char n_fill, n_landing, n_big;
    unsigned char video;        /* 0 = EGA sprites, 1 = CGA sprites */
    unsigned ufo_block, craft_block;
    unsigned n_unit_sets, cursor_base;
    unsigned char n_alien_types, n_spawn;
    char title[24];
    unsigned char script;       /* SCRIPT_ */
    unsigned char road[3];      /* east-west, north-south, crossing blocks */
    unsigned char n_civilians;  /* civilian sets are unit sets 1 and 2 */
    unsigned char n_big_blocks; /* 20x20 blocks to place */
    unsigned char zombie_set;   /* unit set of zombies, 0xFF = none */
    unsigned char occ;          /* 1: or_sprite holds cover data (tools/occlude.py) */
    Mcd *mcd;
    unsigned char *fill, *landing, *big;
    unsigned unit_base[MAX_UNIT_SETS];
    unsigned char **sprite;
    Block *block;
    unsigned n_nodes;
    RouteNode *node;
    AlienType *alien_type;      /* unused (version 2 mission packs) */
    Spawn *spawn;
    AlienType race[8];          /* by rank, from the race pack */
    unsigned char n_qty;        /* spawn entries with quantities (0 = old pack) */
    unsigned char spawn_qty[16][3];     /* lowQty, highQty, dQty per spawn entry */
    /* Map scripts without a UFO (alien base, Mars, Cydonia base): the
       "XSC1" section after the quantities. */
    unsigned char tunnel[4];    /* digTunnel records: west, north, corner, floor */
    unsigned char objective;    /* special type of objective tiles, 0 none */
    unsigned char *block_nodes; /* per block: u8 count, count x RouteNode */
    UnitXStat race_x[8];        /* battle rules by rank (race and terror packs) */
    /* "XRT2" (newer packs; "XRT1": 10-byte records): every block's route
       nodes in full, per block u8 count, count x 11 bytes (MapNode as in
       the RMP file, then 1 for a base defence target node); the share of
       each crew entry placed outside the UFO. The first byte of routes is
       the record size (10 or 11), the nodes follow. */
    unsigned char *routes;
    unsigned char outside[16];
    /* "XIS1": alien equipment (alienDeployments.rul itemSets) per spawn
       entry and item level 0-2: weapon 0-7 (1 plasma pistol, 2 rifle,
       3 heavy plasma, 4 small launcher, 5 blaster launcher), clips - 1
       << 3, alien grenades << 5, 0x80 mind probe; the levels' odds by
       month (alienItemLevels.rul: rolls 0-9 below [0] are level 0, below
       [0] + [1] level 1, else 2). */
    unsigned char n_iset;
    unsigned char iset[16][3];
    unsigned char ilevel[10][2];
    /* "XLT1" in the trailer: the scorched earth record (BLANKS.MCD 1,
       Tile::destroy; 0 = none, older packs), the records that are light
       sources of 6 or more (record, light) and, in a dark battle, the map
       columns the terrain's light sources light enough to see units by
       (calculateTerrainLighting: a bit per x, y; NULL = none), worked out
       again from the map's parts as they stand when one of them broke
       (relight, pak_light_map). */
    unsigned char scorched;
    unsigned char *lit;
    unsigned char lights[MAX_LIGHTS][2];
    unsigned char n_lights, relight;
    /* The battle's HE blockages (MAX_HE_VALUES, MCD_HE), then every
       record's Flammable (MCD_FLAM). */
    unsigned char *he;
    /* "XEX1": the records that explode when destroyed (MCD_EXPL; MCD
       HE_Strength, Tile::destroy) and their strength, record 0 = the end
       of the list (src/bturn.c). */
    unsigned char expl[MAX_EXPL][2];
    /* "XCR1" (packs with the transports' maps): the landing craft's
       records follow the common ones (ids craft_rec0 on; in the pack's
       table from craft_rec_disk on, which holds n_disk_mcd records in all;
       packs without the section: craft_rec0 = craft_rec_disk = n_mcd), its
       block is craft_block. deploy: its seats (crafts.rul deployment: x, y,
       z in the craft block, direction), n_deploy 0 = the start tiles in map
       order (the Skyranger); freed once the squad is placed. */
    unsigned n_disk_mcd, craft_rec0, craft_rec_disk;
    unsigned char craft;        /* CM_* of the craft on the map */
    unsigned char n_deploy;
    unsigned char (*deploy)[4];
} Pack;

extern Pack g_pak;

/* The transport whose map the next pak_load lands (CM_*; set by the
   geoscape, quick battles: CRAFT, a battle save: BsCtx.craft). A pack
   without the transports' maps has the Skyranger's. */
enum { CM_SKYRANGER, CM_LIGHTNING, CM_AVENGER, N_CRAFT_MAPS };
extern unsigned char g_pak_craft;

/* Frees the craft's seat list (Pack.deploy). */
void pak_free_deploy(void);

/* Returns NULL on success, else an error message. */
const char *pak_load(const char *path);

/* Reads the mission's sprites once the map exists: unit and cursor frames,
   and terrain only for what is on the map. NULL on success. */
const char *pak_load_sprites(void);

/* Bytes pak_load_sprites would load for the map as it is now. */
unsigned long pak_sprite_bytes(void);

/* Tiles of map block i, read from the pack file (valid until the next call).
   NULL if it cannot be read. */
const unsigned char *pak_block_tiles(unsigned i);

/* Closes the pack file and frees the block buffer (only map_generate needs
   the blocks). */
void pak_free_blocks(void);

/* Reads the pack's voxel shapes (g_vox, src/vox.c) after pak_free_blocks,
   into the heap's room the blocks left. NULL on success. */
const char *pak_vox(void);

/* Adds a race pack (alien unit frames and ranks) to the loaded mission:
   its sprite sets become unit sets 1 and up. NULL on success. */
const char *race_load(const char *path);

/* Adds a terror pack (terror units by rank, zombies): its ranks fill the
   ones the race pack has none for. NULL on success. */
const char *terror_load(const char *path);

/* Adds an armour pack (one soldier sprite set) and records its unit set
   in g_armour_set[armour]. NULL on success. Frames after the 48 (the
   Flying Suit's hovering legs at 48-55) are the standing ones until
   floorld.c loads them into g_suit_extra, when there is room (freed by
   pak_free). */
extern unsigned char *g_suit_extra;
/* With upper or extended memory, walk slots 1 and 3 of some unit sets
   (floorld.c walk_load, from <pack>W.PAK, tools/walkpak.py): bit n of
   g_walk2 = unit set n walks through slots 2 and 4 only, as without them
   (battle.c `walk`); the frames are a sprite bank of their own (freed by
   pak_free). */
extern unsigned g_walk2;
/* The sprite banks (DOS blocks) pak_free frees. */
#define MAX_BANKS 12
extern unsigned char *g_banks[MAX_BANKS];
extern int g_n_banks;
const char *armour_load(const char *path, int armour);

/* Adds a pack of one unit set (an HWP's, src/hwp.c); *set gets its unit
   set. NULL on success. */
const char *unit_set_load(const char *path, unsigned char *set);

/* Tight battles (docs/design.md): sizes before loading, room in the sprite
   table beforehand, sets that show another set's pictures. */
enum { ALIAS_SAME, ALIAS_SUIT, ALIAS_HOVER };
long unit_pack_bytes(const char *path, int suit);
const char *unit_set_alias(unsigned char from, int how, unsigned n, unsigned char *set);
extern unsigned g_alias_sets;
/* The disk cache (sprcache.c, resident). */
extern int g_sc_fd;
extern const unsigned long *g_sc_off;
extern unsigned long g_sc_pos;
extern unsigned char g_no_rows[2];
extern unsigned char *g_sc_buf;
extern unsigned g_sc_size, g_sc_head, g_sc_common;
void __cdecl sprite_fault(unsigned char **e, unsigned i);   /* sndasm.asm */
void __cdecl sprite_in(unsigned char **e, unsigned i);      /* sndasm.asm: sprite_fault if needed */
extern unsigned g_sc_n, mission_sprites;
extern unsigned g_sc_tag, g_sc_cur, g_sc_top, g_sc_xh;
extern char block_path[80];
unsigned long mark_needed(unsigned char *need);
unsigned long spr_size(unsigned i);
int sc_decide(unsigned char *need);
int sc_keep(const unsigned long *off, unsigned long pos, unsigned common, unsigned n);
void sc_stop(void);
const char *xs_units(const char *path, int suit, unsigned char *set);
unsigned long pak_sprite_paged(void);
const char *sc_start(void);
/* floorld.c walk_load: the picture store's room in bytes (0: none open), and
   n pictures (sizes from off[0..n]) read from f (a FILE *) where it stands
   into it: their pointers in p; 0 when all went. */
unsigned long sc_room(void);
int sc_walk(void *f, const unsigned long *off, unsigned n, unsigned char **p);
const char *armours_load(void);

/* Releases everything pak_load allocated. */
void pak_free(void);

/* In a dark battle, the map columns the terrain's light sources light
   (Pack.lit) from the map as it is: at the setup, and when a light source
   broke (Pack.relight, effects.c fx_lit). */
void pak_light_map(void);

#endif
