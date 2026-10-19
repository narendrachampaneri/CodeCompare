#!/usr/bin/env python3
"""Cross-check codecompare-cli against `git diff --no-index`.

Generates random folder pairs (source-like text, CRLF/LF, binary, added/deleted
files, nested folders), then checks for every pair that:
  * the set of changed files is identical to git's, and
  * the per-file added/removed line counts are identical to `git diff --minimal --numstat`.

Usage: python3 compare_with_git.py <path-to-codecompare-cli> [rounds]
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

WORDS = ["int", "void", "return", "if", "else", "for", "while", "x", "y", "count",
         "buf", "len", "{", "}", "(", ")", ";", "=", "+", "0", "1", "uart", "gpio"]


def rand_line(rng):
    if rng.random() < 0.12:
        return ""
    return " " * rng.choice([0, 0, 4, 8]) + " ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 7)))


def rand_text(rng, n):
    return [rand_line(rng) for _ in range(n)]


def mutate(rng, lines):
    out = []
    for line in lines:
        r = rng.random()
        if r < 0.06:
            continue
        if r < 0.10:
            out.append(rand_line(rng))
            continue
        if r < 0.14:
            out.append(rand_line(rng))
        out.append(line)
    if rng.random() < 0.3:
        out += rand_text(rng, rng.randint(1, 5))
    return out


def write(path, lines, eol, final_newline):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = eol.join(lines)
    if lines and final_newline:
        data += eol
    with open(path, "wb") as f:
        f.write(data.encode())


def make_pair(rng, root):
    left, right = os.path.join(root, "L"), os.path.join(root, "R")
    os.makedirs(left)
    os.makedirs(right)
    for i in range(rng.randint(3, 12)):
        rel = os.path.join(*(["d%d" % rng.randint(0, 2)] * rng.randint(0, 2) + ["f%d.c" % i]))
        base = rand_text(rng, rng.randint(0, 80))
        kind = rng.random()
        eol_l = "\r\n" if rng.random() < 0.2 else "\n"
        eol_r = eol_l if rng.random() < 0.85 else ("\n" if eol_l == "\r\n" else "\r\n")
        fin_l = rng.random() < 0.9
        fin_r = fin_l if rng.random() < 0.9 else not fin_l
        if kind < 0.1:
            write(os.path.join(left, rel), base, eol_l, fin_l)          # deleted
        elif kind < 0.2:
            write(os.path.join(right, rel), base, eol_r, fin_r)         # added
        elif kind < 0.35:
            write(os.path.join(left, rel), base, eol_l, fin_l)          # identical
            write(os.path.join(right, rel), base, eol_l, fin_l)
        else:
            write(os.path.join(left, rel), base, eol_l, fin_l)
            write(os.path.join(right, rel), mutate(rng, base), eol_r, fin_r)
    if rng.random() < 0.3:
        os.makedirs(os.path.join(left, "bin"), exist_ok=True)
        os.makedirs(os.path.join(right, "bin"), exist_ok=True)
        blob = bytes(rng.randrange(256) for _ in range(300)) + b"\0"
        with open(os.path.join(left, "bin", "fw.bin"), "wb") as f:
            f.write(blob)
        with open(os.path.join(right, "bin", "fw.bin"), "wb") as f:
            f.write(blob[:-5] + b"\1\2\3\4\0")
    return left, right


def git_numstat(left, right):
    # Run from the common parent with relative names so git prints "{L => R}/path",
    # "L/path => /dev/null" or "/dev/null => R/path".
    root = os.path.dirname(left)
    p = subprocess.run(["git", "-c", "core.autocrlf=false", "diff", "--no-index", "--minimal",
                        "--numstat", "--no-renames", "L", "R"],
                       capture_output=True, text=True, cwd=root)
    res = {}
    for line in p.stdout.splitlines():
        a, d, path = line.split("\t", 2)
        if path.startswith("{L => R}/"):
            rel = path[len("{L => R}/"):]
        elif path.endswith(" => /dev/null"):
            rel = path[:-len(" => /dev/null")][len("L/"):]
        elif path.startswith("/dev/null => "):
            rel = path[len("/dev/null => "):][len("R/"):]
        else:
            raise RuntimeError("unexpected git path: " + path)
        res[rel] = (a, d)
    return res


def cc_numstat(cli, left, right):
    p = subprocess.run([cli, "--numstat", left, right], capture_output=True, text=True)
    if p.returncode == 2:
        raise RuntimeError(p.stderr)
    res = {}
    for line in p.stdout.splitlines():
        a, d, path = line.split("\t", 2)
        res[path] = (a, d)
    return res


def main():
    cli = os.path.abspath(sys.argv[1])
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rng = random.Random(2026)
    failures = 0
    files_checked = 0
    for r in range(rounds):
        root = tempfile.mkdtemp(prefix="ccgit_")
        try:
            left, right = make_pair(rng, root)
            g = git_numstat(left, right)
            c = cc_numstat(cli, left, right)
            files_checked += len(g)
            if g != c:
                failures += 1
                print("Round %d MISMATCH" % r)
                for k in sorted(set(g) | set(c)):
                    if g.get(k) != c.get(k):
                        print("   %-20s git=%s cc=%s" % (k, g.get(k), c.get(k)))
                if failures > 5:
                    break
        finally:
            shutil.rmtree(root)
    print("%d rounds, %d changed files checked, %d mismatching rounds" % (rounds, files_checked, failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
