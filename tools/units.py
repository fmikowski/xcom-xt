# SPDX-License-Identifier: GPL-3.0-or-later
"""Assembles whole-unit frames from X-COM's body-part sprite sheets.

Follows OpenXcom's drawing routines as far as standing and walking go:
  0 / 10  soldiers, sectoids, mutons: legs, torso and two arms
  1       floaters: hovering body and two arms (5 walk frames per direction)
  3       cyberdiscs (2x2): four quarters by direction, propulsion below
  4       ethereals, civilians, zombies: whole-body frames
  5       sectopods, reapers (2x2): four quarters by direction
  6       snakemen: tail, torso and two arms
  7       chryssalids: legs, head and two arms
  8       silacoids: a pulsating blob, the same in every direction
  9       celatids: a floating, turning body, the same in every direction

2x2 units (routines 3 and 5) use the same 40-frame slot as the others:
frame part * 8 + direction (part 0 = the unit's tile, 1 = x+1, 2 = y+1,
3 = x+1 and y+1), no walk animation, frames 32-39 empty.
"""

SUPPORTED_ROUTINES = (0, 1, 3, 4, 5, 6, 7, 8, 9, 10)
BIG_ROUTINES = (3, 5)
CREW_ROUTINES = (0, 1, 4, 6, 10)      # race packs; the rest go to terror packs
PULSATE = (0, 1, 2, 3, 4, 3, 2, 1)     # silacoid frames by walk phase

WALK_BOB = (1, 0, -1, 0, 1, 0, -1, 0)
WALK_PHASES = (0, 2, 4, 6)     # half of the original 8, to save memory
# Walk slots (1-4) the engine draws: src/battle.c walk() alternates 2 and 4.
# The others are left empty (trim), which saves 40 % of a unit set; if the
# engine ever draws all four, pass trim=False (or widen this tuple).
DRAWN_WALK_SLOTS = (2, 4)


def _overlay(dst, src, dy=0):
    for y, row in enumerate(src):
        ty = y + dy
        if 0 <= ty < len(dst):
            for x, c in enumerate(row):
                if c:
                    dst[ty][x] = c


def _order(direction, larm, body_parts, rarm, bob):
    """Arms behind or in front of the body, depending on the facing."""
    body = [(p, dy) for p, dy in body_parts]
    if direction in (0, 1, 2):
        return [(larm, bob)] + body + [(rarm, bob)]
    if direction == 3:
        return body + [(larm, bob), (rarm, bob)]
    return [(rarm, bob)] + body + [(larm, bob)]


def compose(frames, direction, phase=None, routine=0):
    """phase None = standing, else walk phase 0..7."""
    d = direction
    if routine == 4:
        return frames[d] if phase is None else frames[8 + 8 * d + phase]
    if routine == 1:
        larm, rarm = frames[8 + d], frames[d]
        body = frames[16 + d] if phase is None else frames[24 + 5 * d + phase * 5 // 8]
        order = _order(d, larm, [(body, 0)], rarm, 0)
    elif routine == 8:
        return frames[0] if phase is None else frames[PULSATE[phase]]
    elif routine == 9:
        return frames[0] if phase is None else frames[phase]
    elif routine == 7:
        torso = frames[24 + d]
        if phase is None:
            legs, larm, rarm, bob = frames[16 + d], frames[d], frames[8 + d], 0
        else:
            larm = frames[32 + 24 * d + phase]
            rarm = frames[40 + 24 * d + phase]
            legs = frames[48 + 24 * d + phase]
            bob = WALK_BOB[phase]
        order = _order(d, larm, [(legs, 0), (torso, bob)], rarm, bob)
    elif routine == 6:
        larm, rarm = frames[d], frames[8 + d]
        tail = frames[16 + d] if phase is None else frames[32 + 8 * d + phase]
        torso = frames[24 + d]
        order = _order(d, larm, [(tail, 0), (torso, 0)], rarm, 0)
    else:
        if phase is None:
            legs, larm, rarm, bob = frames[16 + d], frames[d], frames[8 + d], 0
        else:
            legs = frames[56 + 24 * d + phase]
            larm = frames[40 + 24 * d + phase]
            rarm = frames[48 + 24 * d + phase]
            bob = WALK_BOB[phase]
        torso = frames[32 + d]
        order = _order(d, larm, [(legs, 0), (torso, bob)], rarm, bob)
    out = [[0] * 32 for _ in range(40)]
    for part, dy in order:
        _overlay(out, part, dy)
    return out


KNEEL_DROP = 4                 # body pixels lower when kneeling


def compose_kneel(frames, direction):
    """Drawing routine 0 kneeling: kneel legs, body and arms 4 pixels down."""
    d = direction
    legs, larm, rarm, torso = frames[24 + d], frames[d], frames[8 + d], frames[32 + d]
    order = _order(d, larm, [(legs, 0), (torso, KNEEL_DROP)], rarm, KNEEL_DROP)
    out = [[0] * 32 for _ in range(40)]
    for part, dy in order:
        _overlay(out, part, dy)
    return out


def compose_big(frames, part, direction, routine):
    """One quarter of a standing 2x2 unit."""
    out = [[0] * 32 for _ in range(40)]
    if routine == 3 and part > 0:
        _overlay(out, frames[32 + (part - 1) * 8])     # propulsion glow
    _overlay(out, frames[part * 8 + direction])
    return out


def _blank():
    return [[0] * 32 for _ in range(40)]


# Heavy weapons platforms (TANKS.PCK, OpenXcom UnitSprite::drawRoutine2):
# tank quarters 0-31, hovertank quarters 32-63 (part * 8 + direction),
# turrets 64 + type * 8 + direction, the hovertank's propulsion 104 +
# (part - 1) * 8 + animation frame under parts 1-3. The turret sits 4
# pixels up on the front quarter, a hovertank's by its body direction.
TURRET_Y = -4
HOVER_X = (-2, -7, -5, 0, 5, 7, 2, 0)
HOVER_Y = (-1, -3, -4, -5, -4, -3, -1, -1)


def _overlay_at(dst, src, dx, dy):
    for y, row in enumerate(src):
        ty = y + dy
        if 0 <= ty < len(dst):
            for x, c in enumerate(row):
                tx = x + dx
                if c and 0 <= tx < len(dst[0]):
                    dst[ty][tx] = c


def hwp_frames(frames, hover, turret):
    """Body quarters part * 8 + direction (0-31), the turret by its
    direction (32-39, drawn TURRET_Y pixels up, plus HOVER_Y[body
    direction] on a hovertank); hovertanks also get the front quarter
    with the turret facing the body's way (40-47, OpenXcom's offsets: the
    turret's sideways shift HOVER_X is not on the 16-pixel grid the
    engine draws on, so a turret turned elsewhere sits without it) and
    their propulsion, drawn under quarters 1-3 by the animation frame
    (48 + (part - 1) * 8 + frame, from TANKS.PCK 104 on)."""
    base = 32 if hover else 0
    out = []
    for part in range(4):
        for d in range(8):
            f = _blank()
            _overlay(f, frames[base + part * 8 + d])
            out.append(f)
    out += [[row[:] for row in frames[64 + turret * 8 + d]] for d in range(8)]
    for d in range(8 if hover else 0):
        f = [row[:] for row in out[24 + d]]
        _overlay_at(f, frames[64 + turret * 8 + d], HOVER_X[d] if hover else 0,
                    TURRET_Y + (HOVER_Y[d] if hover else 0))
        out.append(f)
    if hover:
        out += [[row[:] for row in frames[104 + i]] for i in range(24)]
    return out


def unit_frames(frames, routine=0, kneel=False, trim=True):
    """Index = dir * 5 + (0 standing, 1..4 walk phases 0, 2, 4, 6); with
    kneel, 8 kneeling frames (by direction) follow at index 40. 2x2 units:
    see the module notes. trim: only the walk slots the engine draws."""
    if routine in BIG_ROUTINES:
        result = [compose_big(frames, p, d, routine) for p in range(4) for d in range(8)]
        return result + [_blank() for _ in range(8)]
    result = []
    for d in range(8):
        result.append(compose(frames, d, None, routine))
        for slot, p in enumerate(WALK_PHASES, 1):
            drawn = not trim or slot in DRAWN_WALK_SLOTS
            result.append(compose(frames, d, p, routine) if drawn else _blank())
    if kneel:
        result.extend(compose_kneel(frames, d) for d in range(8))
    return result
