"""Install into an isolated folder, verify runtime files, exercise the game, then uninstall."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import winreg


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    repo = Path(__file__).resolve().parents[2]
    receipt = json.loads((repo.parent / "installers/installer-manifest.json").read_text())
    work = Path(receipt["work"]).resolve()
    destination = (work / "Test Installation").resolve()
    if not destination.is_relative_to(work) or destination.exists():
        raise RuntimeError("The installer test requires a fresh directory inside its work folder")
    registry_path = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenAoKWorldcraft"
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_path):
            raise RuntimeError("An installed copy exists; it must not be changed by this test")
    except FileNotFoundError:
        pass
    env = {key: value for key, value in os.environ.items() if not key.startswith("AOE_")}
    env["PATH"] = os.environ["WINDIR"] + r"\System32;" + os.environ["WINDIR"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    started = time.monotonic()
    results = {"installerSha256": sha256(Path(receipt["installer"])), "developmentToolsInPath": False}
    if results["installerSha256"] != receipt["installerSha256"]:
        raise RuntimeError("The installer changed after packaging")
    # NSIS requires the final /D= path to be unquoted, including spaces.
    # Pass a Windows command line directly, without invoking a shell.
    install_command = subprocess.list2cmdline([receipt["installer"], "/S"]) + " /D=" + str(destination)
    installed = subprocess.run(install_command, env=env,
        startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW, timeout=300)
    if installed.returncode:
        raise RuntimeError(f"Silent install failed: {installed.returncode}")
    manifest = json.loads((work / "payload-manifest.json").read_text())
    for relative, expected in manifest.items():
        actual = destination / relative
        if not actual.is_file() or actual.stat().st_size != expected["bytes"] or sha256(actual) != expected["sha256"]:
            raise RuntimeError(f"Installed file differs: {relative}")
    results["filesVerified"] = len(manifest)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
        if Path(winreg.QueryValueEx(key, "InstallLocation")[0]) != destination:
            raise RuntimeError("Incorrect uninstall registration")
    print(f"Installed and verified {len(manifest)} files", flush=True)
    binary = destination / "build/OpenAoK.exe"

    def game(name, arguments, custom_env=None):
        with (work / f"{name}.stdout.log").open("wb") as output, (work / f"{name}.stderr.log").open("wb") as errors:
            process = subprocess.run([str(binary), *arguments], cwd=binary.parent, env=custom_env or env,
                stdout=output, stderr=errors, startupinfo=startup,
                creationflags=subprocess.CREATE_NO_WINDOW, timeout=300)
        results[name] = process.returncode
        if process.returncode:
            raise RuntimeError(f"Installed game check failed: {name} ({process.returncode})")
        print(f"{name}: ok", flush=True)

    checks = ["worldjobs", "forestnet", "compatibility", "help", "save", "recording", "net", "lobby"]
    check_env = dict(env, AOE_VERIFY_ONLY=",".join(checks))
    game("installed-verification", ["--verify", "10"], check_env)
    verdicts = (work / "installed-verification.stdout.log").read_text(encoding="utf-8", errors="replace").splitlines()
    if not all(verdicts.count(name + "=ok") == 1 for name in checks):
        raise RuntimeError("Installed verification omitted a requested check")
    common = ["--windowed", "--resolution", "1024x768"]
    game("installed-menu", [*common, "--menu", "main", "--frames", "10", "--screenshot", str(work / "installed-menu.png")])
    game("installed-planting", [*common, "--game", "--no-ai", "--select", "VMBAS", "--reveal", "all",
        "--clicks", "97,686;down:620,300;move:620,372;up:620,372;move:950,550",
        "--frames", "900", "--screenshot", str(work / "installed-planting.png")])
    for name in ("installed-menu", "installed-planting"):
        if not (work / f"{name}.png").is_file():
            raise RuntimeError(f"No screenshot for {name}")
        errors = (work / f"{name}.stderr.log").read_text(encoding="utf-8", errors="replace")
        if errors.strip():
            raise RuntimeError(f"Game diagnostic in {name}: {errors[:1000]}")
    # Preserve an unrelated file to prove that uninstall is not a recursive wipe.
    keep = destination / "user-file-to-preserve.txt"
    keep.write_text("keep after uninstall\n", encoding="utf-8")
    # _?= avoids the uninstaller's detached temporary copy, so its exit is awaited.
    uninstall_command = subprocess.list2cmdline([str(destination / "Uninstall.exe"), "/S"]) + " _?=" + str(destination)
    uninstalled = subprocess.run(uninstall_command,
        cwd=work, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW, timeout=120)
    if uninstalled.returncode:
        raise RuntimeError(f"Uninstall failed: {uninstalled.returncode}")
    remaining = [relative for relative in manifest if (destination / relative).exists()]
    if remaining or keep.read_text(encoding="utf-8") != "keep after uninstall\n":
        raise RuntimeError(f"Uninstall failed to remove only its own files: {remaining[:5]}")
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_path):
            raise RuntimeError("Uninstall registration remains")
    except FileNotFoundError:
        pass
    results.update(uninstall=0, unrelatedFilePreserved=True, seconds=round(time.monotonic() - started, 2))
    (work / "verification.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2), flush=True)


if __name__ == "__main__":
    main()
