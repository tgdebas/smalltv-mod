Import("env")
import os
import subprocess

# Windows: SCons's default cmd.exe-based SPAWN can mis-tokenize quoted/long
# build-tool command lines, dropping args. This re-tokenizes and dispatches directly.
PROJECT_DIR = env.subst("$PROJECT_DIR")


def _tokenize(cmdline):
    # \"-escaped quotes (e.g. ESP8266's ARDUINO_BOARD_ID=\"esp12e\") must become a
    # literal " in the token, not get dropped like a bare ".
    tokens = []
    current = []
    in_quotes = False
    i, n = 0, len(cmdline)
    while i < n:
        c = cmdline[i]
        if c == "\\" and i + 1 < n and cmdline[i + 1] == '"':
            current.append('"')
            i += 2
            continue
        if c == '"':
            in_quotes = not in_quotes
        elif c.isspace() and not in_quotes:
            if current:
                tokens.append("".join(current))
                current = []
        else:
            current.append(c)
        i += 1
    if current:
        tokens.append("".join(current))
    return tokens


def _spawn(sh, escape, cmd, args, spawnenv):
    full_env = dict(os.environ)
    full_env.update(spawnenv)
    cmdline = " ".join(str(a) for a in args)
    real_args = _tokenize(cmdline)
    try:
        return subprocess.run(real_args, env=full_env, shell=False, cwd=PROJECT_DIR).returncode
    except OSError:
        return subprocess.run(cmdline, env=full_env, shell=True, cwd=PROJECT_DIR).returncode

# Windows-only: POSIX SCons spawns via sh with single-quote escaping, which
# the double-quote-only tokenizer above would mis-parse.
if os.name == "nt":
    env["SPAWN"] = _spawn


# ESP8266 RAM: Arduino_GFX's YCbCr->RGB tables (YCbCr2RGB.h, ~6.8 KB of
# static const arrays) are referenced only by Arduino_DataBus::
# writeYCbCrPixels, behind drawYCbCrBitmap for video frames. Nothing in this
# firmware draws YCbCr, but the virtual keeps them linked, and on this chip
# .rodata sits in RAM. This keeps them in flash instead, by adding one rule to
# the linker script the framework generates on every build, ahead of the
# catch-all .rodata. A future caller of drawYCbCrBitmap would fault on the
# 16-bit flash reads: draw RGB565 instead, or drop this rule.
_GFX_TABLES_RULE = (
    "    *:Arduino_DataBus.cpp.o(.rodata._ZL*) "
    "/* smalltv-mod: unused GFX YCbCr tables stay in flash */\n"
)


def _keep_gfx_tables_in_flash(target, source, env):
    path = target[0].get_abspath()
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    if "smalltv-mod: unused GFX YCbCr tables" in text:
        return
    anchor = "*(.irom0.literal .irom.literal"
    at = text.find(anchor)
    if at < 0:
        print("smalltv-mod: linker script anchor not found; GFX tables stay in RAM")
        return
    line_start = text.rfind("\n", 0, at) + 1
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:line_start] + _GFX_TABLES_RULE + text[line_start:])


def _patch_ld_before_link(target, source, env):
    ld = env.subst(os.path.join("$BUILD_DIR", "ld", "local.eagle.app.v6.common.ld"))
    if os.path.isfile(ld):
        _keep_gfx_tables_in_flash([env.File(ld)], source, env)


if env.get("PIOPLATFORM") == "espressif8266":
    # After the script is generated, and again right before every link: SCons
    # can regenerate the script without running a post action (its content
    # signature changed because of this very patch), and the link must never
    # see an unpatched copy. The patch is idempotent.
    env.AddPostAction(
        os.path.join("$BUILD_DIR", "ld", "local.eagle.app.v6.common.ld"),
        _keep_gfx_tables_in_flash,
    )
    env.AddPreAction(os.path.join("$BUILD_DIR", "${PROGNAME}.elf"), _patch_ld_before_link)
