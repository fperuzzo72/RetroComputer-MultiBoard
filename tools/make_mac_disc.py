#!/usr/bin/env python3
"""Build a small bootable Mac disc from a big one, keeping only what is asked.

pico-mac's umac0.img is a 32MB HFS volume of which 3MB is used; this makes
a volume of any size holding the chosen folders and files from it, every
file with both forks and its Finder type and creator (copied as MacBinary
by hfsutils' hcopy -m), the System Folder blessed, and the source's boot
blocks, so it starts up as the original did. Needs hfsutils (brew install
hfsutils).

  python3 tools/make_mac_disc.py SOURCE.img OUT.img SIZE_KB "VOLUME NAME" PATH...

PATH is an HFS path in the source, ':'-separated, a folder or a file; its
parent folders are created as needed. For example, the disc built into the
e-ink firmwares (see README, "The Macintosh"):

  python3 tools/make_mac_disc.py ~/Downloads/umac0.img roms/mac/boot.img 1440 "Paper Mac" \\
      ":System Folder" ":Programs" ":Files" ":Games:Lode Runner" ...

Names are Mac Roman on the disc; give them here as you would type them
(UTF-8), and they are converted.
"""
import os
import shutil
import subprocess
import sys
import tempfile


def mr(s):
    return s.encode("mac_roman")


def run(*args):
    r = subprocess.run([a if isinstance(a, bytes) else a.encode() for a in args],
                       capture_output=True)
    if r.returncode:
        sys.exit("%s failed: %s" % (b" ".join(a if isinstance(a, bytes) else a.encode()
                                               for a in args).decode("mac_roman"),
                                     r.stderr.decode("mac_roman").strip()))
    return r.stdout


def listing(path):
    """(is_dir, name) for everything in an HFS folder, names as bytes."""
    out = run("hls", "-1a", "-F", path)
    items = []
    for line in out.split(b"\n"):
        if not line:
            continue
        if line.endswith(b":"):
            items.append((True, line[:-1]))
        else:
            items.append((False, line.rstrip(b"*")))
    return items


def export(src_path, is_dir, tmp, plan):
    """Copy src_path (and, for a folder, everything under it) out as
    MacBinary, remembering where each piece goes."""
    if is_dir:
        plan.append(("dir", src_path, None))
        for sub_dir, name in listing(src_path + b":"):
            export(src_path + b":" + name, sub_dir, tmp, plan)
    else:
        local = os.path.join(tmp, "%05d.bin" % len(plan))
        run("hcopy", "-m", src_path, local)
        plan.append(("file", src_path, local))


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    source, out, size_kb, volname = sys.argv[1:5]
    wanted = sys.argv[5:]
    tmp = tempfile.mkdtemp(prefix="macdisc-")
    plan = []
    try:
        run("hmount", source)
        for p in wanted:
            p = mr(p)
            parent, _, name = p.rpartition(b":")
            kinds = {n: d for d, n in listing(parent + b":" if parent else b":")}
            if name not in kinds:
                sys.exit("not on the source disc: %s" % p.decode("mac_roman"))
            # parent folders first
            parts = p.strip(b":").split(b":")
            for i in range(1, len(parts)):
                plan.append(("dir", b":" + b":".join(parts[:i]), None))
            export(p, kinds[name], tmp, plan)
        run("humount")

        with open(out, "wb") as f:
            f.truncate(int(size_kb) * 1024)
        run("hformat", "-l", mr(volname), out)
        run("hmount", out)
        made = set()
        for kind, path, local in plan:
            if kind == "dir":
                if path not in made:
                    run("hmkdir", path)
                    made.add(path)
            else:
                parent = path.rpartition(b":")[0]
                run("hcopy", "-m", local, (parent or b"") + b":")
        run("hattrib", "-b", b":System Folder")
        used = run("hvol")
        run("humount")

        # The source's boot blocks: hformat leaves them blank, and without
        # them the ROM does not start up from this disc.
        with open(source, "rb") as f:
            boot = f.read(1024)
        with open(out, "r+b") as f:
            f.write(boot)
        print(used.decode("mac_roman").strip())
        print("%s: %s kB, %d files and folders" % (out, size_kb, len(plan)))
    finally:
        subprocess.run(["humount"], capture_output=True)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
