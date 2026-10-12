# Ribterra 0.25.12 — Safe loading of old worlds

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Supported safes **0.25.7–0.25.10** (formatting 8, rules 14–16, generator 5) are transferred to a separate file next to the source: `name.alpha-0.25.12-<identifier>.save` or with the original extension. The original is unchanged. The copy shall be verified before opening; The file with the same name is not replaced.

The game reports the source version and the name of the copy. F5, save button, single game continuation, host saving, and server completion use copy. The separate server shows its path to `save_path` when running; Next time, go this way to `--world`. The new world after rejection has a separate file, so it doesn't delete the rejected save.

In the old worlds, the reduced portions are returning to normal for all nations. The stock is unchanged. The old Attacking and delayed attack timers are being dropped without changing the health of residents and goods. The message directly calls for changes made.

All phases of the active expedition, including departure, hunting, return, and self-defense, ** deviate**. The original file and the game already open remain unchanged. Also, a corrupted or unknown format, inconsistent rules/game data, broken item links and delivery are rejected. Forced labor, active production of deferred tools/lists and unfinished construction of the hidden structure are not yet carried over.

The completed historic buildings, their owners, levels II, residents and goods are preserved, including tools, inscriptions and leather. The normal production load continues delivery once. Loading the network world restores the owner and his team; The room error does not replace the open session.

Limitation: the rule number 14 is common to 0.25.7 and 0.25.8, so the interface honestly displays the version range. New safes 0.25.11/0.25.12 are opened regularly; The false unacceptable state in the current format does not correct the inference. Previously available old format converters with a backup copy are left; The new warranty is tested for the described states 0.25.7 and does not imply universal compatibility of all historical worlds.

The rules **17**, protocol **19**, format **8**, generator **5** are not changed. The new periodic car park does not add to this update. [Results of the tests](verification-0.25.12-uk.md).
