#!/usr/bin/env python3
"""One-off repair for reading statistics orphaned by the /read folder move.

Finishing a book with "Move Finished Books to Read Folder" enabled renames the file.
Reading history is keyed by book path in two places, and before MATC-012 neither followed
the move, so the history survived on the card but under an identity nothing looked up:

  /system/bookstats/<hash>.bin   named after a hash of the book path
  /system/reading_stats.bin      per-book totals and the finished-book list, paths in plain text

Each bookstats file also stores its book path *inside* it (the firmware uses it to detect hash
collisions), so nothing has to be reverse-engineered: this reads the original path out, notices
the book is no longer there, finds it in /read, and re-files the record under the new path.

Run with no flags first -- it reports and changes nothing. Add --apply to write.
"""

import argparse
import os
import shutil
import struct
import sys

BKST_MAGIC = b"BKST"
BKST_VERSION = 2
STATS_VERSION_MIN = 3  # per-book block exists from v3


# --- libstdc++ std::hash<std::string> on a 32-bit target (ESP32) -----------------------------
# _Hash_bytes is MurmurHash2, 32-bit variant, seeded 0xc70f6907. The script does not trust this
# blindly: verify_hash() below checks it against every file on the card before anything is
# written, because each file's name is the hash of the path stored inside it.
def std_hash_32(data: bytes) -> int:
    m = 0x5BD1E995
    mask = 0xFFFFFFFF
    length = len(data)
    h = (0xC70F6907 ^ length) & mask

    i = 0
    while length - i >= 4:
        k = struct.unpack_from("<I", data, i)[0]
        k = (k * m) & mask
        k ^= k >> 24
        k = (k * m) & mask
        h = (h * m) & mask
        h ^= k
        i += 4

    rem = length - i
    if rem == 3:
        h ^= data[i + 2] << 16
    if rem >= 2:
        h ^= data[i + 1] << 8
    if rem >= 1:
        h ^= data[i]
        h = (h * m) & mask

    h ^= h >> 13
    h = (h * m) & mask
    h ^= h >> 15
    return h & mask


def stats_filename(book_path: str) -> str:
    return "%08x.bin" % std_hash_32(book_path.encode("utf-8"))


# --- card layout ----------------------------------------------------------------------------
def find_dir(root: str, name: str):
    """FAT is case-insensitive; the card may show /Read or /read."""
    if not os.path.isdir(root):
        return None
    for entry in os.listdir(root):
        if entry.lower() == name.lower() and os.path.isdir(os.path.join(root, entry)):
            return os.path.join(root, entry)
    return None


def card_path_exists(root: str, book_path: str) -> bool:
    rel = book_path.lstrip("/").replace("/", os.sep)
    full = os.path.join(root, rel)
    if os.path.exists(full):
        return True
    # Walk it case-insensitively, one component at a time.
    cur = root
    for part in rel.split(os.sep):
        if not os.path.isdir(cur):
            return False
        match = next((e for e in os.listdir(cur) if e.lower() == part.lower()), None)
        if match is None:
            return False
        cur = os.path.join(cur, match)
    return True


def read_bookstats(path: str):
    """Returns (stored_book_path, sessions, day_count, payload_after_path) or None."""
    with open(path, "rb") as f:
        head = f.read(15)
        if len(head) != 15 or head[0:4] != BKST_MAGIC or head[4] != BKST_VERSION:
            return None
        sessions, day_count, path_len = struct.unpack_from("<IIH", head, 5)
        if path_len > 500:
            return None
        raw = f.read(path_len)
        if len(raw) != path_len:
            return None
        return raw.decode("utf-8", "replace"), sessions, day_count, f.read()


def write_bookstats(path: str, book_path: str, sessions: int, day_count: int, tail: bytes):
    raw = book_path.encode("utf-8")
    with open(path, "wb") as f:
        f.write(BKST_MAGIC)
        f.write(bytes([BKST_VERSION]))
        f.write(struct.pack("<IIH", sessions, day_count, len(raw)))
        f.write(raw)
        f.write(tail)


# --- /system/reading_stats.bin --------------------------------------------------------------
class Cursor:
    def __init__(self, blob):
        self.b = blob
        self.i = 0

    def take(self, n):
        out = self.b[self.i : self.i + n]
        if len(out) != n:
            raise EOFError("truncated reading_stats.bin")
        self.i += n
        return out

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]


def patch_reading_stats(blob: bytes, old: str, new: str):
    """Rewrites the file with old -> new in the finished list and per-book block.

    Parsed field by field rather than blind-replaced: a raw byte substitution would corrupt the
    length prefixes whenever the two paths differ in length, which is the normal case here.
    """
    c = Cursor(blob)
    out = bytearray()
    changes = 0

    version = c.u8()
    out += bytes([version])
    day_count = c.u16()
    out += struct.pack("<H", day_count)
    if version >= 2:
        out += c.take(2)  # booksFinished, a monotonic tally -- left exactly as is
    out += c.take(day_count * 6)

    path_count = c.u16()
    out += struct.pack("<H", path_count)
    for _ in range(path_count):
        n = c.u16()
        p = c.take(n).decode("utf-8", "replace")
        if p == old:
            p, changes = new, changes + 1
        raw = p.encode("utf-8")
        out += struct.pack("<H", len(raw)) + raw

    if version >= STATS_VERSION_MIN:
        book_count = c.u16()
        out += struct.pack("<H", book_count)
        for _ in range(book_count):
            n = c.u16()
            p = c.take(n).decode("utf-8", "replace")
            if p == old:
                p, changes = new, changes + 1
            raw = p.encode("utf-8")
            out += struct.pack("<H", len(raw)) + raw
            ln = c.u8()
            out += bytes([ln]) + c.take(ln)
            out += c.take(8)  # minutesRead + lastReadDay

    out += c.b[c.i :]  # per-language block and anything a newer version appended
    return bytes(out), changes


# --- self-check -----------------------------------------------------------------------------
def verify_hash(records):
    """Every file is named after the hash of the path inside it, so the card proves the hash."""
    ok = bad = 0
    for fname, (book_path, *_rest) in records.items():
        if stats_filename(book_path) == fname.lower():
            ok += 1
        else:
            bad += 1
            print("    mismatch: %s holds %r -> expected %s" % (fname, book_path, stats_filename(book_path)))
    return ok, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sd_root", help="SD card root, e.g. F:\\ or /Volumes/XTEINK")
    ap.add_argument("--apply", action="store_true", help="write the repair (default: report only)")
    args = ap.parse_args()

    root = args.sd_root
    if not os.path.isdir(root):
        sys.exit("Not a directory: %s" % root)

    system_dir = find_dir(root, "system")
    stats_dir = find_dir(system_dir, "bookstats") if system_dir else None
    if not stats_dir:
        sys.exit("No /system/bookstats on %s -- is this the right card?" % root)

    records = {}
    for fname in sorted(os.listdir(stats_dir)):
        if not fname.lower().endswith(".bin"):
            continue
        rec = read_bookstats(os.path.join(stats_dir, fname))
        if rec is None:
            print("  skipping unreadable/old-format %s" % fname)
            continue
        records[fname] = rec

    print("Found %d book history file(s) in %s\n" % (len(records), stats_dir))
    if not records:
        return

    print("Verifying hash implementation against the card...")
    ok, bad = verify_hash(records)
    print("  %d of %d filenames reproduced exactly\n" % (ok, ok + bad))
    if bad or not ok:
        sys.exit("Hash does not match this card's files -- aborting rather than guessing.")

    read_dir = find_dir(root, "read")
    # The firmware's library scan composes each book's path from the ACTUAL directory entry
    # name it reads off the card, so the hash depends on the real letter case. "/read" is only
    # what mkdir() requests; a card that already had "/Read" keeps that spelling, and filing
    # history under the wrong case would hash to something the firmware never looks up.
    read_name = os.path.basename(read_dir.rstrip("\\/")) if read_dir else "read"
    orphans = []
    for fname, (book_path, sessions, day_count, tail) in records.items():
        if card_path_exists(root, book_path):
            continue
        base = book_path.rsplit("/", 1)[-1]
        found = None
        if read_dir:
            found = next((e for e in os.listdir(read_dir) if e.lower() == base.lower()), None)
        orphans.append((fname, book_path, sessions, day_count, tail, base, found))
    if not orphans:
        print("Every history file points at a book that is still on the card. Nothing to repair.")
        return

    print("Orphaned history (the book is no longer at the recorded path):")
    actionable = []
    for fname, book_path, sessions, day_count, tail, base, found in orphans:
        days = len(tail) // 6
        minutes = sum(struct.unpack_from("<H", tail, i * 6 + 4)[0] for i in range(days))
        print("\n  %s" % fname)
        print("    recorded path : %s" % book_path)
        print("    history       : %d session(s), %d day(s), %d minute(s)" % (sessions, days, minutes))
        if found:
            new_path = "/" + read_name + "/" + found
            print("    found on card : %s" % new_path)
            print("    will re-file as: %s" % stats_filename(new_path))
            actionable.append((fname, book_path, new_path, sessions, day_count, tail))
        else:
            print("    NOT found in /%s -- the book was deleted or renamed; left untouched." % read_name)

    if not actionable:
        print("\nNothing can be repaired automatically.")
        return

    if not args.apply:
        print("\nDry run. Re-run with --apply to perform the repair above.")
        return

    print("\nApplying...")
    for fname, old_path, new_path, sessions, day_count, tail in actionable:
        src = os.path.join(stats_dir, fname)
        dst = os.path.join(stats_dir, stats_filename(new_path))
        shutil.copy2(src, src + ".bak")
        write_bookstats(dst, new_path, sessions, day_count, tail)
        if os.path.abspath(dst) != os.path.abspath(src):
            os.remove(src)
        print("  book history: %s -> %s" % (old_path, new_path))

        stats_file = os.path.join(system_dir, "reading_stats.bin")
        real = next((e for e in os.listdir(system_dir) if e.lower() == "reading_stats.bin"), None)
        if not real:
            print("  (no reading_stats.bin -- per-book totals unchanged)")
            continue
        stats_file = os.path.join(system_dir, real)
        with open(stats_file, "rb") as f:
            blob = f.read()
        try:
            patched, n = patch_reading_stats(blob, old_path, new_path)
        except EOFError as e:
            print("  reading_stats.bin: %s -- left untouched" % e)
            continue
        if n:
            shutil.copy2(stats_file, stats_file + ".bak")
            with open(stats_file, "wb") as f:
                f.write(patched)
            print("  reading_stats.bin: repointed %d record(s)" % n)
        else:
            print("  reading_stats.bin: no record named that path")

    print("\nDone. Originals kept alongside as .bak -- delete them once you have confirmed the stats.")


if __name__ == "__main__":
    main()
