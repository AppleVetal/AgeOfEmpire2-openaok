"""Package the tested Windows executable and the user's prepared game data."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def quoted(value):
    value = str(value)
    if any(character in value for character in '\r\n"'):
        raise ValueError("Unsupported character in installer path")
    return '"' + value.replace("$", "$$") + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nsis", type=Path, required=True, help="Path to portable makensis.exe")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    support = repo.parent / "OpenAoK-build-support"
    output = (args.output or repo.parent / "installers").resolve()
    work = support / "installer" / datetime.now().strftime("work-%Y%m%d-%H%M%S")
    payload = work / "payload"
    payload.mkdir(parents=True, exist_ok=False)
    output.mkdir(parents=True, exist_ok=True)
    nsis = args.nsis.resolve(strict=True)

    def copy(source, relative):
        destination = payload / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    def tree(source, relative):
        if source.is_dir():
            shutil.copytree(source, payload / relative,
                ignore=lambda directory, names: [name for name in names
                    if name.lower() in {".gitignore", "__pycache__"} or
                    (Path(directory).name.lower() == "sound" and name.lower() == "stream")])

    for name in ("OpenAoK.exe", "SDL2.dll"):
        copy(repo / "build" / name, "build/" + name)
    for folder in ("extracted_data", "sprites", "original/Campaign", "original/History",
                   "original/Scenario", "original/interfac_extracted", "original/Avi", "original/Sound"):
        tree(repo / "assets" / folder, "assets/" + folder)
    for name in ("gamedata.drs", "graphics.drs", "interfac.drs", "terrain.drs", "sounds.drs",
                 "blendomatic.dat", "TileEdge.Dat", "BlkEdge.Dat"):
        copy(repo / "assets/original/Data" / name, "assets/original/Data/" + name)
    tree(repo / "original_game_1999/SYSTEM/FONTS", "original_game_1999/SYSTEM/FONTS")
    tree(repo / "original_game_1999/GAME/SOUND/STREAM", "original_game_1999/GAME/SOUND/STREAM")
    tree(repo / "original_game_1999/GOODIES", "original_game_1999/GOODIES")
    tree(repo / "fonts", "fonts")
    tree(repo / "mods/worldcraft", "mods/worldcraft")
    tree(repo / "LICENSES", "LICENSES")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        copy(repo / name, name)
    copy(support / "deps/SDL2-2.32.10/LICENSE.txt", "LICENSES/SDL2.txt")
    copy(support / "deps/freetype-2.13.3/docs/FTL.TXT", "LICENSES/FreeType.txt")
    copy(here / "Play-OpenAoK.cmd", "Play-OpenAoK.cmd")
    copy(here / "PLAY.txt", "PLAY.txt")
    (payload / ".openaok-worldcraft").write_text("OpenAoK Worldcraft installer v1\n", encoding="utf-8")

    required = ["assets/extracted_data/rules.json", "assets/extracted_data/strings.json",
        "assets/original/interfac_extracted/50500.bina", "assets/sprites/terrain/blendmasks.png",
        "original_game_1999/SYSTEM/FONTS/TIMES.TTF", "fonts/Arial.ttf", "fonts/Arial Bold.ttf",
        "mods/worldcraft/manifest.json", "mods/worldcraft/rules.json", "mods/worldcraft/graphics.json",
        "mods/worldcraft/strings.ru.json", "original_game_1999/GAME/SOUND/STREAM/OPEN.MP3"]
    for relative in required:
        if not (payload / relative).is_file():
            raise FileNotFoundError(f"Missing runtime file: {relative}")
    forbidden = [path for path in payload.rglob("*") if path.is_file() and
        path.suffix.lower() in {".exe", ".dll", ".py", ".ps1", ".cpp", ".h", ".obj"} and
        path.relative_to(payload).as_posix() not in {"build/OpenAoK.exe", "build/SDL2.dll"}]
    if forbidden:
        raise ValueError(f"Unexpected executable/development files: {forbidden}")
    version = json.loads((payload / "mods/worldcraft/manifest.json").read_text(encoding="utf-8"))["version"]
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    info = {"version": version, "engineCommit": commit, "engineSha256": digest(payload / "build/OpenAoK.exe"),
            "source": "https://github.com/AppleVetal/AgeOfEmpire2-openaok", "gameData": "local user copy",
            "developmentToolsRequired": False, "installerCompiler": subprocess.check_output([str(nsis), "/VERSION"], text=True).strip()}
    (payload / "INSTALLATION.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    files = sorted(path for path in payload.rglob("*") if path.is_file())
    manifest = {path.relative_to(payload).as_posix(): {"bytes": path.stat().st_size, "sha256": digest(path)} for path in files}
    (work / "payload-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    directories = {parent for path in files for parent in path.parents if parent != payload and parent.is_relative_to(payload)}
    deletes = ["Delete " + quoted("$INSTDIR\\" + path.relative_to(payload).as_posix().replace("/", "\\")).replace("$$INSTDIR", "$INSTDIR")
               for path in files if path.relative_to(payload).as_posix() != "build/OpenAoK.exe"]
    deletes += ["RMDir " + quoted("$INSTDIR\\" + path.relative_to(payload).as_posix().replace("/", "\\")).replace("$$INSTDIR", "$INSTDIR")
                for path in sorted(directories, key=lambda path: (-len(path.parts), str(path)))]
    (work / "uninstall-files.nsh").write_text("\n".join(deletes) + "\n", encoding="utf-8-sig")
    total = sum(item["bytes"] for item in manifest.values())
    installer = output / f"OpenAoK-Worldcraft-{version}-win64-Setup.exe"
    command = [str(nsis), "/INPUTCHARSET", "UTF8", "/V3", f"/DPAYLOAD={payload}", f"/DOUTPUT={installer}",
               f"/DUNINSTALL_FILES={work / 'uninstall-files.nsh'}", f"/DAPP_VERSION={version}",
               f"/DINSTALL_KB={(total + 1023) // 1024}", str(here / "OpenAoK-Worldcraft.nsi")]
    print(f"Packaging {len(files)} files, {total / (1024 * 1024):.1f} MiB; work={work}", flush=True)
    with (work / "compile.log").open("wb") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
    if result.returncode:
        raise RuntimeError(f"Installer compiler exited {result.returncode}; see {work / 'compile.log'}")
    receipt = dict(info, installer=str(installer), installerSha256=digest(installer), installerBytes=installer.stat().st_size,
                   installedBytes=total, files=len(files), work=str(work), payload=str(payload))
    (output / "installer-manifest.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    (output / (installer.name + ".sha256")).write_text(receipt["installerSha256"] + "  " + installer.name + "\n", encoding="ascii")
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == "__main__":
    main()
