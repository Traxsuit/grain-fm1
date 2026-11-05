#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Copyright (C) 2026 M-Vave-OP1 contributors
"""Target-side cost estimate of the hot code, from the pi32v2 objdump listing tools/build.py writes
(build/firmware.dis). For the audio ISR, the scan ISR and every function named in the budget file (the app's render
code: add a line `name 0` and run with BUDGET_UPDATE=1): the instructions inside loops (every address range closed
by a backward branch: the per-sample loops and what they contain), and the hardware divides and calls in them.
cost = the loop instructions, each weighted 4 per level of nesting (an inner loop runs several times per sample) and
1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide, something not
inlined any more). Compared with tests/target_budget.txt at +10 %.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

PLATFORM = ["fm1_alnk0_irq", "fm1_timer5_irq",     # the audio ISR (app_audio inlined or called); TIMER5: scan + USB
            "aud_fade_half"]                       # (audio.c): the DAC faded around a flash erase
OPTIONAL = {}                                      # name: budget, for functions only some builds have
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/firmware.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"


def budget_names(path):
    """the functions the budget file lists (the app's), after the platform's"""
    names = list(PLATFORM)
    if os.path.exists(path):
        for line in open(path):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#") and p[0] not in names:
                names.append(p[0])
    return names


FUNCS = budget_names(budget)
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


FPU = re.compile(r"^3f [ef]5 $")                     # the FPU's 4-byte instructions: objdump knows only their first half


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out. An FPU instruction
    (3f e5 / 3f f5 .. : objdump prints '<unknown instruction>' and decodes its second half as an instruction of
    its own, at times a branch) counts once, as 'fpu'"""
    out, cur, half = {}, None, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                half = None
                continue
            m = INSN.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            if half is not None and a == half:
                half = None                              # (the FPU instruction's second half)
                continue
            half = None
            if FPU.match(m.group(2)) and m.group(3).strip() == "<unknown instruction>":
                half = a + 2
                if cur is not None:
                    cur.append((a, "fpu"))
                continue
            if cur is not None and not m.group(3).lstrip().startswith("<"):
                cur.append((a, m.group(3).strip()))
    return out


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it); an instruction inside d
    spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the outer one)"""
    lo = insns[0][0]
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# Target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/firmware.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                if n not in OPTIONAL:
                    f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
