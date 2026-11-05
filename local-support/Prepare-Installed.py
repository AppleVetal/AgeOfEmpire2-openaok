"""Prepare this workspace's installed Age of Kings files with upstream extractors."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
game = repo / "original_game_1999" / "GAME"
sys.path.insert(0, str(repo / "tools" / "data"))
import datfile
import prepare_data


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    required = ["data/empires2.dat", "data/graphics.drs", "data/interfac.drs",
                "data/terrain.drs", "data/gamedata.drs", "data/sounds.drs",
                "data/blendomatic.dat", "LANGUAGE.dll", "empires2.exe"]
    for relative in required:
        if not (game / relative).is_file():
            raise SystemExit(f"Missing installed game file: {game / relative}")
    data = datfile.load()
    if data["version"] != "VER 5.7" or len(data["civs"]) != 13:
        raise SystemExit("This import requires the base Age of Kings data with 13 civilizations.")
    print(f"Base data: {data['version']}, {len(data['civs'])} civilizations", flush=True)
    print("Using installed files directly; no CD preparation stamp will be fabricated.", flush=True)

    original = repo / "assets" / "original"
    copied = []
    for folder in ("Data", "Campaign", "Scenario", "History", "Sound", "Avi"):
        source = game / folder
        if source.is_dir():
            print(f"Copying {folder}", flush=True)
            shutil.copytree(source, original / folder, dirs_exist_ok=True)
            copied.append(folder)
    font_names = (
        "GEORGIA.TTF", "GEORGIAB.TTF", "GEORGIAI.TTF", "GEORGIAZ.TTF",
        "LBLACK.TTF", "LBRITE.TTF", "LBRITED.TTF", "LBRITEI.TTF", "LBRITEDI.TTF",
        "LCALLIG.TTF", "ARIALN.TTF", "ARIALNB.TTF", "ARIALNI.TTF", "ARIALNBI.TTF",
        "COMIC.TTF", "COMICBD.TTF", "PAPYRUS.TTF", "POORICH.TTF", "VINERITC.TTF",
        "TIMES.TTF", "TIMESBD.TTF", "TIMESI.TTF", "TIMESBI.TTF")
    font_dir = repo / "original_game_1999" / "SYSTEM" / "FONTS"
    font_dir.mkdir(parents=True, exist_ok=True)
    system_fonts = Path("C:/Windows/Fonts")
    copied_fonts, missing_fonts = [], []
    for name in font_names:
        if (font_dir / name).exists():
            continue
        if (system_fonts / name).is_file():
            shutil.copy2(system_fonts / name, font_dir / name)
            copied_fonts.append(name)
        else:
            missing_fonts.append(name)
            print(f"WARNING: original font unavailable: {name}", flush=True)
    arial_dir = repo / "fonts"
    arial_dir.mkdir(exist_ok=True)
    ignore_file = arial_dir / ".gitignore"
    if not ignore_file.exists():
        ignore_file.write_text("*\n", encoding="utf-8")
    for source, destination in (("arial.ttf", "Arial.ttf"), ("arialbd.ttf", "Arial Bold.ttf")):
        if not (arial_dir / destination).exists():
            shutil.copy2(system_fonts / source, arial_dir / destination)

    prepare_data.extract_palette()
    (repo / "assets" / "extracted_data").mkdir(parents=True, exist_ok=True)
    (repo / "assets" / "sprites" / "terrain").mkdir(parents=True, exist_ok=True)
    for command in prepare_data.EXTRACTORS:
        print(f"Extracting: {command[0]}", flush=True)
        subprocess.run([sys.executable, str(repo / "tools" / "data" / command[0]),
                        *command[1:]], cwd=repo, check=True)

    manifest = {
        "source": str(game), "kind": "installed-game", "dat_version": data["version"],
        "civilizations": len(data["civs"]), "copied_folders": copied,
        "font_source": str(system_fonts), "copied_fonts": copied_fonts,
        "missing_fonts": missing_fonts,
        "source_sha256": {name: sha256(game / name) for name in required},
    }
    (support / "INSTALLED-DATA-MANIFEST.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Installed game data prepared. Baseline checks must determine compatibility.", flush=True)


if __name__ == "__main__":
    main()
