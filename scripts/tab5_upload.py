"""Bindet den geprüften Tab5-Upload in PlatformIO ein. Anschluss: --upload-port oder upload_port, sonst das einzige angeschlossene Tab5. TAB5_ERSTFLASH=1 richtet ein Werksgerät einmalig ein."""

import os
from pathlib import Path
import sys

Import("env")
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from flash_tab5 import esptool_command, flash, FIRST_FLASH_VARIABLE, WEB_APP


def upload_tab5(source, target, env):
    """Verwendet Bootloader, Tabelle und App dieses Environments. Sicherungen von Werksgeräten landen unter .pio/recovery/."""
    build = Path(env.subst("$BUILD_DIR"))
    images = [(0x8000, build / "partitions.bin"), (WEB_APP[2], build / "firmware.bin")]
    flash(esptool_command(env.subst("$OBJCOPY")), env.subst("$UPLOAD_PORT"), images, bootloader=build / "bootloader.bin", first_flash=os.environ.get(FIRST_FLASH_VARIABLE) == "1", recovery_root=Path(env.subst("$PROJECT_DIR")) / ".pio" / "recovery")


env.Replace(UPLOADCMD=env.VerboseAction(upload_tab5, "Geprüfter Upload auf das Tab5"))
