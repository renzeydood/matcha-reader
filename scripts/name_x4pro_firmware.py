from pathlib import Path
from shutil import copy2

Import("env")


def copy_named_x4pro_firmware(source, target, env):
    if env.get("PIOENV") != "x4pro":
        return

    config = env.GetProjectConfig()
    version = config.get("crosspoint", "version")
    build_dir = Path(env.subst("$BUILD_DIR"))
    source_bin = build_dir / f"{env.subst('$PROGNAME')}.bin"
    named_bin = build_dir / f"matchareader-{version}-x4pro.bin"

    copy2(source_bin, named_bin)
    print(f"Named X4 Pro firmware: {named_bin}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_named_x4pro_firmware)
