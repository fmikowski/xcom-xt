# SPDX-License-Identifier: GPL-3.0-or-later
r"""XCOMCONV: makes xcom-xt's packs from your own copy of X-COM: UFO Defense.

    XCOMCONV.EXE [game folder] [-o output folder]

The game folder is the one that holds GEODATA, GEOGRAPH, MAPS, ROUTES, SOUND,
TERRAIN, UFOGRAPH, UFOINTRO and UNITS (DOS version 1.4: Steam, GOG or the CD;
the Steam and GOG install folders work as they are). Without it the converter
looks in the usual Steam and GOG places, then asks.

The packs go to a PACKS folder next to XCOMCONV.EXE (or -o): the ones every
video mode shares (sound, music, names, UFOpaedia) and PACKS.VER, the pack
format version the engine checks, in PACKS itself; the pictures of each video
mode in PACKS\EGA, PACKS\CGA (also used for Hercules) and PACKS\VGA. Copy the
program and PACKS (with only the mode folders the machine needs) to the DOS PC.

The packs are made from your game files and are for your own use only: do not
share them. The converter itself is free software (GNU GPL v3 or later).
"""

import argparse
import multiprocessing
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

GAME_DIRS = ["GEODATA", "GEOGRAPH", "MAPS", "ROUTES", "SOUND", "TERRAIN", "UFOGRAPH",
             "UFOINTRO", "UNITS"]
NAME_HINTS = ("xcom", "x-com", "ufo", "enemy unknown")


def bundle_dir():
    """Where the rules (data/) and the headers (src/) are: inside the exe,
    or the repository."""
    return getattr(sys, "_MEIPASS", None) or os.path.abspath(os.path.join(HERE, ".."))


def exe_dir():
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.getcwd()


def say(*a):
    print(*a)
    sys.stdout.flush()


# ----------------------------------------------------------------------
# Finding and checking the game folder


def _children(path):
    try:
        return sorted(os.listdir(path))
    except OSError:
        return []


def is_game_dir(path):
    names = {n.lower() for n in _children(path) if os.path.isdir(os.path.join(path, n))}
    return all(d.lower() in names for d in GAME_DIRS)


def search(path, depth=2):
    """The game folders in path or one or two levels below it (names compared
    without regard to case): a Steam or GOG install folder holds the game one
    level down."""
    found = []
    if not os.path.isdir(path):
        return found
    if is_game_dir(path):
        return [path]
    if depth > 0:
        for n in _children(path):
            sub = os.path.join(path, n)
            if os.path.isdir(sub):
                found += search(sub, depth - 1)
    return found


def _steam_libraries():
    libs = []
    steam = []
    try:
        import winreg
        for hive, key, val in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                               (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam",
                                "InstallPath")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    steam.append(winreg.QueryValueEx(k, val)[0])
            except OSError:
                pass
    except ImportError:
        pass
    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        if os.environ.get(var):
            steam.append(os.path.join(os.environ[var], "Steam"))
    for s in steam:
        libs.append(s)
        vdf = os.path.join(s, "steamapps", "libraryfolders.vdf")
        try:
            with open(vdf, encoding="utf-8", errors="replace") as f:
                libs += [m.replace("\\\\", "\\") for m in re.findall(r'"path"\s+"([^"]+)"', f.read())]
        except OSError:
            pass
    return libs


def _gog_folders():
    out = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"SOFTWARE\WOW6432Node\GOG.com\Games") as games:
            for i in range(winreg.QueryInfoKey(games)[0]):
                try:
                    with winreg.OpenKey(games, winreg.EnumKey(games, i)) as k:
                        name = str(winreg.QueryValueEx(k, "gameName")[0]).lower()
                        if any(h in name for h in NAME_HINTS):
                            out.append(winreg.QueryValueEx(k, "path")[0])
                except OSError:
                    pass
    except (ImportError, OSError):
        pass
    return out


def default_candidates():
    """Folders to search with no argument, most likely first."""
    cands = [os.getcwd(), exe_dir()]
    bases = []
    for lib in _steam_libraries():
        bases.append(os.path.join(lib, "steamapps", "common"))
    pf = [os.environ.get(v) for v in ("ProgramFiles(x86)", "ProgramFiles")]
    pf = [p for p in pf if p]
    cands += _gog_folders()
    for p in pf:
        bases += [os.path.join(p, "GOG Galaxy", "Games"), p]
    for drive in "CDEFGH":
        bases += [drive + ":\\GOG Games", drive + ":\\Games", drive + ":\\"]
    home = os.path.expanduser("~")
    bases += [os.path.join(home, "Documents", "OpenXcom"), os.path.join(home, "Documents"),
              os.path.join(home, "Games"), home]
    seen = set(os.path.normcase(c) for c in cands)
    for b in bases:
        for n in _children(b):
            if any(h in n.lower() for h in NAME_HINTS) or n.lower() == "ufo":
                p = os.path.join(b, n)
                if os.path.normcase(p) not in seen:
                    seen.add(os.path.normcase(p))
                    cands.append(p)
    return cands


def find_game(arg):
    """The game folder for the command line argument (None: look around)."""
    if arg:
        hits = search(arg)
        if hits:
            return hits[0]
        return None
    for c in default_candidates():
        hits = search(c)
        if hits:
            return hits[0]
    return None


def ask_game():
    for _ in range(3):
        try:
            answer = input("Folder of your X-COM: UFO Defense installation "
                           "(empty: quit): ").strip().strip('"')
        except EOFError:
            return None
        if not answer:
            return None
        hits = search(answer)
        if hits:
            return hits[0]
        say("No X-COM data in that folder (it must hold %s)." % ", ".join(GAME_DIRS))
    return None


def check_version(root, skip=False):
    """Mirrors OpenXcom's look at the install (the game folders are there),
    then makes sure the files are DOS 1.4's: every file the converter reads
    exists, and the files that fix the data layouts have DOS 1.4's sizes.
    Returns (warnings, intro): intro is False when the optional UFOINTRO files
    are not all there. Raises SystemExit with a clear message."""
    import dos14
    import ufodata
    missing, wrong, other = [], [], 0
    for rel, size in sorted(dos14.REQUIRED.items()):
        try:
            actual = os.path.getsize(ufodata.find_file(root, rel))
        except OSError:
            missing.append(rel)
            continue
        if actual != size:
            if rel in dos14.STRICT:
                wrong.append("%s is %d bytes, DOS 1.4's is %d" % (rel, actual, size))
            else:
                other += 1
    if missing:
        raise SystemExit(
            "This is not a complete copy of X-COM: UFO Defense (DOS 1.4). Missing files:\n  "
            + "\n  ".join(missing[:12]) + ("\n  ... %d more" % (len(missing) - 12)
                                           if len(missing) > 12 else "")
            + "\nCopy the whole game folder (Steam: steamapps\\common\\XCom UFO Defense\\XCOM; "
            "GOG: the XCOM folder; CD: the XCOM or UFO folder of the disc).")
    if wrong and not skip:
        raise SystemExit(
            "This is not the DOS 1.4 version of X-COM: UFO Defense:\n  " + "\n  ".join(wrong[:8])
            + "\nXCOMCONV needs the DOS 1.4 data (the Steam, GOG and CD versions). Other versions"
            "\n(Windows/TFTD/old patches/mods) are not supported. --skip-check tries anyway.")
    warnings = []
    intro = True
    for rel, size in dos14.OPTIONAL.items():
        try:
            if os.path.getsize(ufodata.find_file(root, rel)) != size:
                other += 1
        except OSError:
            intro = False
    if not intro:
        warnings.append("UFOINTRO files are missing: no intro movie or end slides")
    if wrong:
        warnings.append("version check skipped: %d key files differ from DOS 1.4" % len(wrong))
    if other:
        warnings.append("%d of the game's files differ in size from the DOS 1.4 data the "
                        "converter was tested with (patched or modded data?): continuing" % other)
    return warnings, intro


# ----------------------------------------------------------------------
# The conversion


class Prefixed:
    """A stdout that starts every line with a tag (two converters at once)."""

    def __init__(self, tag, out):
        self.tag, self.out, self.at_start = tag, out, True

    def write(self, s):
        for part in s.splitlines(True):
            if self.at_start and part.strip():
                self.out.write(self.tag)
            self.out.write(part)
            self.at_start = part.endswith("\n")
        return len(s)

    def flush(self):
        self.out.flush()


VARIANTS = ("EGA", "CGA", "VGA")


def run_variant(variant, game, rules, out, lang_args):
    """The EGA, CGA or VGA packs (runs in its own process)."""
    import convert
    sys.stdout = Prefixed("[%s] " % variant, sys.__stdout__)
    if variant != "VGA":
        argv = [game, rules, out] + (["--cga"] if variant == "CGA" else []) + lang_args
        convert.main(argv)
        return 0
    # The VGA version (256 colours, a 286 or better): every crash site, then
    # the terror site, base defence, alien base and Cydonia, the screens with
    # their palettes, the intro and the end slides.
    import intro
    import ui
    convert.main([game, rules, out, "--vga"] + lang_args)
    convert.main([game, rules, out, "--vga", "--specials"] + lang_args)
    data, entries = ui.build(game, rules, False, True, False)
    with open(os.path.join(out, "PALV.PAK"), "wb") as f:
        f.write(ui.vga_palettes(game))
    with open(os.path.join(out, "UIV.PAK"), "wb") as f:
        f.write(data)
    print("UIV.PAK", len(data), "bytes,", len(entries), "entries")
    try:
        intro.convert(intro.parser().parse_args([game, rules, out, "--vga"]))
    except (OSError, ValueError) as e:
        print("INTROV / SLIDESV packs skipped:", e)
    return 0


def _variant_entry(variant, game, rules, out, lang_args, q):
    try:
        q.put((variant, run_variant(variant, game, rules, out, lang_args), ""))
    except BaseException as e:      # reported to the parent
        import traceback
        q.put((variant, 1, traceback.format_exc() if not isinstance(e, SystemExit) else str(e)))


def expected_packs(intro=True):
    """The packs a complete run makes."""
    import convert
    names = ["UFOPAED.PAK", "NAMES.PAK", "SOUNDS.PAK", "MUSIC.PAK"]
    for v in "EC":
        names += ["WORLD%s.PAK" % v, "UI%s.PAK" % v, "FLOOR%s.PAK" % v, "BLAST%s.PAK" % v, "CYD%s.PAK" % v, "CYD1%s.PAK" % v, "CYD2%s.PAK" % v,
                  "UBASE%s.PAK" % v, "BASE%s.PAK" % v, "UR0%s.PAK" % v]
        if intro:
            names += ["INTRO%s.PAK" % v, "SLIDES%s.PAK" % v]
        names += ["A%s%s.PAK" % (c, v) for c in convert.ARMOUR_SHEETS]
        names += ["H%d%s.PAK" % (k + 1, v) for k in range(len(convert.HWP_PACKS))]
        names += ["R%s%s.PAK" % (c, v) for c in convert.RACE_CODES.values()]
        names += ["T%s%s.PAK" % (c, v) for c in convert.RACE_CODES.values()]
        names += ["%s%d%s.PAK" % (t, u, v) for t in convert.TERRAIN_CODES.values()
                  for u in convert.UFO_CODES.values()]
    for v in "EC":              # the walk phases beside the unit packs
        names += ["A%s%sW.PAK" % (c, v) for c in convert.ARMOUR_SHEETS]
        names += ["R%s%sW.PAK" % (c, v) for c in convert.RACE_CODES.values()]
    names += ["WORLDV.PAK", "UIV.PAK", "PALV.PAK", "FLOORV.PAK", "BLASTV.PAK", "SMOKEV.PAK",
              "CYDV.PAK", "CYD1V.PAK", "CYD2V.PAK", "UBASEV.PAK", "BASEV.PAK", "UR0V.PAK"]
    if intro:
        names += ["INTROV.PAK", "SLIDESV.PAK"]
    names += ["A%sV.PAK" % c for c in convert.ARMOUR_SHEETS]
    names += ["H%dV.PAK" % (k + 1) for k in range(len(convert.HWP_PACKS))]
    names += ["R%sV.PAK" % c for c in convert.RACE_CODES.values()]
    names += ["T%sV.PAK" % c for c in convert.RACE_CODES.values()]
    names += ["%s%dV.PAK" % (t, u) for t in convert.TERRAIN_CODES.values()
              for u in convert.UFO_CODES.values()]
    return names


MODE_FOLDERS = (("EGA", "E"), ("CGA", "C"), ("VGA", "V"))


def _stem(name):
    """A pack's name without its video letter (FA1E.PAK, FA1C.PAK and FA1V.PAK
    are one stem; MUSIC.PAK has no twin and is shared)."""
    m = re.match(r"^(.+)[ECV](W?)\.PAK$", name.upper())
    return "%s|%s" % m.groups() if m else name.upper()


def sort_packs(out):
    """Moves each video mode's packs into out\\EGA, out\\CGA and out\\VGA (the
    engine looks in its mode's folder first, then in its own folder); the
    packs with no twin under another video letter stay in out. Returns
    {folder: [files, bytes]} ("" for out itself)."""
    names = [n for n in os.listdir(out) if n.upper().endswith(".PAK")
             and os.path.isfile(os.path.join(out, n))]
    stems = {}
    for n in names:
        stems.setdefault(_stem(n), []).append(n)
    sizes = {"": [0, 0]}
    for folder, _ in MODE_FOLDERS:
        os.makedirs(os.path.join(out, folder), exist_ok=True)
        sizes[folder] = [0, 0]
    for n in names:
        src = os.path.join(out, n)
        size = os.path.getsize(src)
        folder = ""
        if len(stems[_stem(n)]) > 1 or n.upper().endswith("V.PAK"):
            letter = re.match(r"^.+([ECV])W?\.PAK$", n.upper()).group(1)
            folder = [f for f, l in MODE_FOLDERS if l == letter][0]
            os.replace(src, os.path.join(out, folder, n.upper()))
        sizes[folder][0] += 1
        sizes[folder][1] += size
    return sizes


def convert_all(game, out, lang_args, sequential=False):
    rules = os.path.join(bundle_dir(), "data", "xcom1")
    if not os.path.isdir(rules):
        raise SystemExit("The rule files are missing (%s)." % rules)
    os.makedirs(out, exist_ok=True)
    marker = os.path.join(out, "PACKS.VER")
    if os.path.exists(marker):
        os.remove(marker)       # a run that stops half way leaves no marker
    t0 = time.time()
    results = []
    if sequential:
        for v in VARIANTS:
            say("--- %s packs ---" % v)
            run_variant(v, game, rules, out, lang_args)
            results.append((v, 0, ""))
    else:
        say("--- EGA, CGA and VGA packs (three converters at once) ---")
        q = multiprocessing.Queue()
        procs = [multiprocessing.Process(target=_variant_entry,
                                         args=(v, game, rules, out, lang_args, q))
                 for v in VARIANTS]
        for p in procs:
            p.start()
        for _ in procs:
            results.append(q.get())
        for p in procs:
            p.join()
    bad = [r for r in results if r[1]]
    if bad:
        for v, _, msg in bad:
            say("%s conversion failed:\n%s" % (v, msg))
        raise SystemExit("The conversion failed.")
    say("--- sound and music ---")
    import sound
    sound.main([game, out])
    return time.time() - t0


def finish(out, intro=True):
    import packver
    missing = [n for n in expected_packs(intro) if not os.path.isfile(os.path.join(out, n))]
    if missing:
        raise SystemExit("These packs were not made: %s\nThe conversion is incomplete."
                         % ", ".join(missing[:10]))
    path = packver.write_marker(out)
    return path, sort_packs(out)


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="XCOMCONV", description="Makes xcom-xt's packs from your own copy of "
        "X-COM: UFO Defense (DOS 1.4).")
    ap.add_argument("game", nargs="?", help="the game folder (it holds GEODATA, GEOGRAPH, "
                    "MAPS, ROUTES, SOUND, TERRAIN, UFOGRAPH, UFOINTRO and UNITS), or the "
                    "Steam / GOG install folder around it; default: look in the usual places")
    ap.add_argument("-o", "--output", metavar="DIR", help="where the packs go (default: a "
                    "PACKS folder next to XCOMCONV.EXE)")
    ap.add_argument("--skip-check", action="store_true", help="do not insist on DOS 1.4's "
                    "file sizes")
    ap.add_argument("--openxcom", metavar="DIR_OR_FILE", help="optional: an OpenXcom "
                    "installation (or its en-US.yml): texts the game files lack are read "
                    "from there (otherwise short texts of our own)")
    ap.add_argument("--prefer-openxcom", action="store_true", help="take every UFOpaedia, "
                    "briefing and slide text from OpenXcom's language file, not the game's")
    ap.add_argument("--sequential", action="store_true", help="one converter at a time "
                    "(less memory)")
    args = ap.parse_args(argv)

    import packver
    say("XCOMCONV: xcom-xt data converter (pack format %d)" % packver.version())
    say("Free software under the GNU GPL v3 or later; the packs it makes are for your own use.")
    game = find_game(args.game)
    if not game and args.game:
        raise SystemExit("No X-COM data found in %s (or one or two folders below it): it must "
                         "hold %s." % (args.game, ", ".join(GAME_DIRS)))
    if not game:
        say("The game was not found in the usual Steam and GOG places.")
        game = ask_game()
    if not game:
        raise SystemExit("No game folder: run XCOMCONV.EXE <folder of X-COM: UFO Defense>.")
    say("Game data:  %s" % game)
    out = os.path.abspath(args.output) if args.output else os.path.join(exe_dir(), "PACKS")
    say("Packs to:   %s" % out)
    warnings, intro = check_version(game, args.skip_check)
    for w in warnings:
        say("Note: " + w)
    say("DOS data looks right.")
    lang_args = []
    if args.openxcom:
        lang_args += ["--lang-yml", args.openxcom]
        if args.prefer_openxcom:
            lang_args.append("--prefer-yml")
    secs = convert_all(game, out, lang_args, args.sequential)
    marker, sizes = finish(out, intro)
    say("Done in %d min %02d s. Packs in %s (%s, format %d):" % (
        secs // 60, secs % 60, out, os.path.basename(marker), packver.version()))
    say("  PACKS      %3d files %5.1f MB  sound, music, names, UFOpaedia: every mode needs them"
        % (sizes[""][0] + 1, sizes[""][1] / 1048576.0))
    for folder, why in (("EGA", "EGA"), ("CGA", "CGA and Hercules"),
                        ("VGA", "VGA in 256 colours: a 286 or better with XMS")):
        say("  PACKS\\%s  %3d files %5.1f MB  %s" % (folder, sizes[folder][0],
                                                   sizes[folder][1] / 1048576.0, why))
    say("Copy the game's programs, the files in PACKS and the mode folders your PC needs")
    say("into one folder on the DOS PC (see QUICKSTART.TXT) and run XCOMXT there.")
    return 0


def run():
    multiprocessing.freeze_support()
    code = 0
    try:
        code = main()
    except SystemExit as e:
        if e.code not in (None, 0):
            print(e.code if isinstance(e.code, str) else "exit %s" % e.code, file=sys.stderr)
            code = 1
    except KeyboardInterrupt:
        print("Stopped.", file=sys.stderr)
        code = 1
    if getattr(sys, "frozen", False) and len(sys.argv) == 1 and sys.stdin and sys.stdin.isatty():
        try:
            input("Press Enter to close.")
        except EOFError:
            pass
    return code


if __name__ == "__main__":
    sys.exit(run())
