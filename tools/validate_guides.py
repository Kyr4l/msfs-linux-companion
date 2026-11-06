#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Checks data/*_guide.json against the format in docs/GUIDES.md and against the aircraft's catalog.

Errors (exit status 1): malformed nodes, dangling `next` / topic starts, unreachable nodes, questions that cannot be
answered, steps without a source, controls that do not resolve to exactly one catalog entry, checks without a probe.
Warnings: steps whose control behaviour is not verified on the real simulator (information only).

    tools/validate_guides.py [FILE ...]
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPS = {"==", "!=", ">", "<", ">=", "<="}
TYPES = {"question", "steps", "info", "end"}


def load_catalogs():
    cats = {}
    for p in glob.glob(os.path.join(ROOT, "data", "*_catalog.json")):
        with open(p) as f:
            d = json.load(f)
        cats[d["aircraft"]] = d["entries"]
    return cats


def load_keypads():
    pads = {}
    for p in glob.glob(os.path.join(ROOT, "data", "*_keypads.json")):
        with open(p) as f:
            for pad in json.load(f).get("pads", []):
                pads[pad["id"]] = {k["id"] for k in pad["keys"]}
    return pads


def probe_ok(obj, where, errs):
    code, ev = obj.get("code", ""), obj.get("input_event", "")
    if bool(code) == bool(ev):
        errs.append("%s: needs exactly one of code / input_event" % where)
        return
    if code and not (code.startswith("(") and code.count("(") == code.count(")")):
        errs.append("%s: calculator code must start with '(' and balance its parentheses: %r" % (where, code))
    if ev and not re.fullmatch(r"[A-Za-z0-9_]+(\|[A-Za-z0-9_]+)*", ev):
        errs.append("%s: bad input event name %r" % (where, ev))


def validate(path, cats, pads):
    errs, warns = [], []
    with open(path) as f:
        g = json.load(f)
    ac = g.get("aircraft", "")
    pad_keys = pads.get(g.get("keypad", ""))
    if g.get("keypad") and pad_keys is None:
        errs.append("keypad %r is not in any data/*_keypads.json" % g["keypad"])
    entries = cats.get(ac)
    if entries is None:
        return ["%s: no catalog for aircraft %r" % (path, ac)], []
    by_api = {}
    for e in entries:
        by_api.setdefault(e["api"], []).append(e)
    nodes = g.get("nodes", {})
    if not g.get("sources"):
        errs.append("no sources listed")
    if not g.get("topics"):
        errs.append("no topics")
    edges = {}
    for nid, n in nodes.items():
        w = "node %s" % nid
        t = n.get("type")
        if t not in TYPES:
            errs.append("%s: unknown type %r" % (w, t))
            continue
        if not n.get("title") and t != "end":
            errs.append("%s: no title" % w)
        outs = []
        if t == "question":
            ch = n.get("choices", [])
            if len(ch) < 2:
                errs.append("%s: a question needs at least two choices" % w)
            ids = [c.get("id") for c in ch]
            if len(set(ids)) != len(ids):
                errs.append("%s: duplicate choice ids" % w)
            for c in ch:
                if not c.get("id") or not c.get("label"):
                    errs.append("%s: a choice needs id and label" % w)
                if c.get("next") not in nodes:
                    errs.append("%s: choice %s goes to the unknown node %r" % (w, c.get("id"), c.get("next")))
                outs.append(c.get("next"))
            if not n.get("text"):
                errs.append("%s: a question needs text" % w)
            a = n.get("auto")
            if a is not None:
                probe_ok(a, w + " auto", errs)
                for r in a.get("rules", []):
                    if r.get("op") not in OPS or r.get("choice") not in ids:
                        errs.append("%s: bad auto rule %r" % (w, r))
        else:
            nx = n.get("next", "")
            if t in ("steps", "info") and nx and nx not in nodes:
                errs.append("%s: next goes to the unknown node %r" % (w, nx))
            if nx:
                outs.append(nx)
            if t == "info" and not n.get("text"):
                errs.append("%s: info without text" % w)
        if t == "steps":
            if not n.get("source"):
                errs.append("%s: a steps node needs a source" % w)
            if not n.get("steps"):
                errs.append("%s: no steps" % w)
            for i, s in enumerate(n.get("steps", [])):
                ws = "%s step %d" % (w, i + 1)
                if not s.get("text"):
                    errs.append("%s: no text" % ws)
                if not s.get("source"):
                    errs.append("%s: no source" % ws)
                for c in s.get("controls", []):
                    hits = by_api.get(c.get("api"), [])
                    if c.get("function"):
                        hits = [e for e in hits if e["function"] == c["function"]]
                    if len(hits) != 1:
                        errs.append("%s: control %r resolves to %d catalog entries (needs exactly 1; add \"function\")" % (ws, c, len(hits)))
                    else:
                        e = hits[0]
                        if e.get("kind") == "input_event" and not e.get("verified"):
                            warns.append("%s: %s is not verified on the real simulator" % (ws, e["api"]))
                if s.get("keys"):
                    if pad_keys is None:
                        errs.append("%s: has keys but the guide names no keypad" % ws)
                    for entry in s["keys"]:
                        for k in entry.split("/"):
                            if pad_keys is not None and k not in pad_keys:
                                errs.append("%s: key %r is not on the keypad %s" % (ws, k, g.get("keypad")))
                for r in s.get("readouts", []):
                    probe_ok(r, ws + " readout", errs)
                    if not r.get("label"):
                        errs.append("%s: a readout needs a label" % ws)
                if "check" in s:
                    probe_ok(s["check"], ws + " check", errs)
                    if s["check"].get("op", "==") not in OPS or "value" not in s["check"]:
                        errs.append("%s: check needs op and value" % ws)
        edges[nid] = [o for o in outs if o]
    seen, todo = set(), []
    for t in g.get("topics", []):
        for k in ("id", "title", "start"):
            if not t.get(k):
                errs.append("topic %r has no %s" % (t.get("id"), k))
        if t.get("start") not in nodes:
            errs.append("topic %s starts at the unknown node %r" % (t.get("id"), t.get("start")))
        else:
            todo.append(t["start"])
    while todo:
        x = todo.pop()
        if x in seen:
            continue
        seen.add(x)
        todo.extend(edges.get(x, []))
    for nid in nodes:
        if nid not in seen:
            errs.append("node %s is not reachable from any topic" % nid)
    return ["%s: %s" % (os.path.basename(path), e) for e in errs], ["%s: %s" % (os.path.basename(path), x) for x in warns]


def main():
    paths = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, "data", "*_guide.json")))
    cats = load_catalogs()
    pads = load_keypads()
    bad = 0
    for p in paths:
        errs, warns = validate(p, cats, pads)
        for w in warns:
            print("warning:", w)
        for e in errs:
            print("ERROR:", e)
        with open(p) as f:
            g = json.load(f)
        steps = sum(len(n.get("steps", [])) for n in g.get("nodes", {}).values())
        print("%s: %d topics, %d nodes, %d steps, %d errors, %d warnings" % (os.path.basename(p), len(g.get("topics", [])), len(g.get("nodes", {})), steps, len(errs), len(warns)))
        bad += len(errs)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
