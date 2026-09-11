#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-016: the denominator is frozen, and unfreezing is an event.

Every percentage in this plan is a fraction of a denominator measured once from Microsoft's own
assemblies. A denominator that can move without anyone noticing is not a denominator: a regenerated
inventory that finds two fewer members would raise every percentage in the report and nothing would
say so. So the measurement is frozen here, and this checks it.

Three things are compared, and each fails for a different reason:

* **the assemblies**, by SHA-256 and MVID -- a different build of the Content Pipeline was read, so
  the inventory describes something else;
* **the counts** -- the same assemblies were read and answered differently, which means the reader
  changed;
* **the inventory file itself**, by SHA-256 -- something in it was edited. This is the one that is
  allowed to change, and only by recording the new digest as an event in `regenerations` with the
  date and the reason. That is what makes a regeneration a decision rather than a diff nobody read.

Usage:
    inventory_freeze.py check  --inventory <api.json> --freeze <freeze.json>
    inventory_freeze.py record --inventory <api.json> --freeze <freeze.json> --reason <text>
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import sys
from collections import OrderedDict

FROZEN_COUNTS = ("publicTypes", "publicAndProtectedMembers", "importers",
                 "importerExtensionDeclarations", "distinctExtensions", "processors",
                 "processorProperties")


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle, object_pairs_hook=OrderedDict)


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def assembly_set(inventory):
    return OrderedDict((entry["name"], OrderedDict([("sha256", entry["sha256"]),
                                                    ("mvid", entry["mvid"]),
                                                    ("fileSize", entry["fileSize"])]))
                       for entry in inventory["assemblies"])


def counts_of(inventory):
    return OrderedDict((name, inventory["counts"][name]) for name in FROZEN_COUNTS
                       if name in inventory["counts"])


def check(arguments):
    inventory = load(arguments.inventory)
    freeze = load(arguments.freeze)
    problems = []

    frozen_assemblies = freeze["assemblies"]
    current_assemblies = assembly_set(inventory)
    for name in sorted(set(frozen_assemblies) | set(current_assemblies)):
        if name not in current_assemblies:
            problems.append("the inventory no longer describes %s" % name)
        elif name not in frozen_assemblies:
            problems.append("the inventory describes %s, which the freeze does not" % name)
        elif current_assemblies[name] != frozen_assemblies[name]:
            problems.append("%s is a different file than the one measured: %s"
                            % (name, current_assemblies[name]["sha256"]))

    for name, value in freeze["counts"].items():
        if inventory["counts"].get(name) != value:
            problems.append("the denominator moved: %s is %r and was frozen at %r"
                            % (name, inventory["counts"].get(name), value))

    current = digest(arguments.inventory)
    recorded = [freeze["sha256"]] + [event["sha256"] for event in freeze.get("regenerations", [])]
    if current not in recorded:
        problems.append(
            "%s has been edited (it is now %s) and no `regenerations` entry records that. A "
            "regeneration is a recorded event: run `inventory_freeze.py record --reason ...`."
            % (arguments.inventory, current))
    elif current != freeze["sha256"] and current != recorded[-1]:
        problems.append("%s matches an older recorded regeneration than the newest one"
                        % arguments.inventory)

    for problem in problems:
        print("inventory_freeze: " + problem)
    print("inventory_freeze: %d assembly digest(s), %d frozen count(s); %d problem(s)"
          % (len(frozen_assemblies), len(freeze["counts"]), len(problems)))
    return 1 if problems else 0


def record(arguments):
    inventory = load(arguments.inventory)
    freeze = load(arguments.freeze)
    current = digest(arguments.inventory)
    if current == freeze["sha256"] or any(event["sha256"] == current
                                          for event in freeze.get("regenerations", [])):
        print("inventory_freeze: nothing to record; this inventory is already frozen or recorded")
        return 0
    freeze.setdefault("regenerations", []).append(OrderedDict([
        ("date", datetime.date.today().isoformat()),
        ("sha256", current),
        ("reason", arguments.reason),
        ("counts", counts_of(inventory)),
    ]))
    with open(arguments.freeze, "w", encoding="utf-8") as handle:
        json.dump(freeze, handle, indent=2)
        handle.write("\n")
    print("inventory_freeze: recorded %s" % current)
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("action", choices=("check", "record"))
    parser.add_argument("--inventory", required=True)
    parser.add_argument("--freeze", required=True)
    parser.add_argument("--reason", default="")
    arguments = parser.parse_args(argv[1:])
    if arguments.action == "record" and not arguments.reason:
        parser.error("record needs --reason: an unexplained regeneration is what this prevents")
    return check(arguments) if arguments.action == "check" else record(arguments)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
