"""Setzt das Makro BT_GIT_STAND für den Firmwarestand des Pico: „+“ und der kurze Git-Commit des Builds, dahinter „m“, wenn im Ordner bt-sender/ nicht committete Änderungen liegen. Ohne Git oder außerhalb eines Repositorys bleibt das Makro leer, der Stand nennt dann nur seine Nummer. PlatformIO ruft das Skript vor dem Build auf (extra_scripts in platformio.ini)."""
import subprocess

Import("env")


def git_stand(project_dir):
    """Liefert den Zusatz zum Stand oder eine leere Zeichenkette."""
    try:
        commit = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True, check=True, cwd=project_dir).stdout.strip()
        changed = subprocess.run(["git", "status", "--porcelain", "--", "."], capture_output=True, text=True, check=True, cwd=project_dir).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""
    return "+" + commit + ("m" if changed else "")


stand = git_stand(env.subst("$PROJECT_DIR"))
env.Append(CPPDEFINES=[("BT_GIT_STAND", env.StringifyMacro(stand))])
print("Firmwarestand-Zusatz: '%s'" % stand)
