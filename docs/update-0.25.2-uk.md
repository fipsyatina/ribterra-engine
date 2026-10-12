# Ribterra 0.25.2 — Initial online screen and room creation

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The new hall, location maps and valley are now appearing immediately after clicking Online play». It's names, addresses, passwords and the number of places. So four were chosen initially. The Create Room button opens the current lobby where you can add bots, clear seats, select commands, and launch a world.

Fixed the “Token size outside bounds” error that blocked creation of a new world through the menu: the file identifier now uses a supported random-value size. Joining and continuing a save remain available.

The original online screen and lobby translations are added. A verification of all the transitions through the actual game file, with a separate test profile, is added. The protocol remains 19; The conservation of 0.25.1 is compatible.
