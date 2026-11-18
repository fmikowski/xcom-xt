/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RULES_H
#define RULES_H

/* Battle formulas of the DOS game, after OpenXcom (src/rules.c; table in
   docs/design.md "Battle rules vs DOS"). Shared by battle.c, inv.c,
   effects.c and the alien AI. */

#include "unit.h"

/* Damage types (OpenXcom ItemDamageType order). */
#define DT_AP       1
#define DT_IN       2
#define DT_HE       3
#define DT_LASER    4
#define DT_PLASMA   5
#define DT_STUN     6
#define DT_MELEE    7
#define DT_ACID     8
#define DT_SMOKE    9

/* Armour sides of rl_side (Unit.armor[0] front, [1] right, [2] rear,
   armor_left, armor_under). */
#define SIDE_FRONT  0
#define SIDE_RIGHT  1
#define SIDE_REAR   2
#define SIDE_LEFT   3
#define SIDE_UNDER  4

int rl_weight(int item);                /* items.rul weight */
extern const unsigned char g_weights[N_ITEMS];  /* (the table; DGROUP) */
int rl_item_dtype(int item);            /* damage type of a weapon, clip or grenade */
int rl_round_dtype(const Unit *u);      /* ... of the round in u's weapon */

/* Damage modifier of v's armour against a damage type, percent. */
int rl_dmg_mod(const Unit *v, int dtype);

/* Armour of a side, and the side wear (power / 10 + 1 per wound). */
int rl_armour(const Unit *v, int side);
void rl_wear(Unit *v, int side, int power);

/* Side hit from a direction (rx, ry: from the unit towards the source),
   with the 1-in-3 diagonal rule; 0, 0 = under. */
int rl_side(const Unit *v, int rx, int ry);

/* Body part hit on a side: hclass 2 head, 1 upper body, 0 legs. */
int rl_part(int side, int hclass);

/* Time units. */
int rl_throw_tu(const Unit *u, int item);   /* belt to hand 4, prime 50 %, throw 25 % */
int rl_reaction_score(const Unit *u);       /* reactions x TU / max TU */

/* Farthest throw of an item in voxels (16 a tile; ProjectileFlyBState::
   getMaxThrowDistance), dz = drop from the hand to the target in voxels. */
int rl_throw_range(const Unit *u, int item, int dz);
unsigned rl_isqrt(unsigned long v);
/* a x b, 16 x 16 -> 32 bits by one imul; (a x b) >> 14 (a product below
   2^29). Inline code. */
long rl_imul(int a, int b);
#pragma aux rl_imul = "imul dx" parm [ax] [dx] value [dx ax] modify exact [ax dx];
int rl_mul14(int a, int b);
#pragma aux rl_mul14 = "imul dx" "shl ax,1" "rcl dx,1" "shl ax,1" "rcl dx,1"     parm [ax] [dx] value [dx] modify exact [ax dx];

/* Blast radius of an exploding item or round (items.rul blastRadius, else
   power / 20, at most 11). */
int rl_blast_radius(int item, int power);

/* Projectile::applyAccuracy on voxel coordinates (16 x 16 x 24 a tile):
   moves the target point *t from origin o for an accuracy in percent. */
void rl_deviate(const int *o, int *t, int acc);
/* Where a throw from voxel o at voxel *t flies (Projectile::calculateThrow):
   rl_deviate's miss turns the arc's direction and steepness at the same
   length (calculateParabola's deltas, curvature curv / 1024); *t becomes
   the point the arc heads for. */
void rl_throw_aim(const int *o, int *t, int acc, int curv);

/* Morale modifiers (SavedBattleGame::getMoraleModifier): of a fallen
   X-COM unit's rank, and of the squad's highest-ranked soldier alive. */
int rl_rank_mod(const Unit *u);
int rl_leader_mod(void);

/* Units that bleed (X-COM soldiers) and that feel fear (1x1 units). */
#define RL_WOUNDABLE(u) ((u)->xtype >= XT_XCOM && (u)->xtype < XT_CIVILIAN)
#define RL_FEARABLE(u)  (!(u)->big)

#endif
