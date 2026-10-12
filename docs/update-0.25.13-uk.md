# Ribterra 0.25.13 — Correction of the takeoff after action

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Fixed the game ending with `Missing localization key: Мешканця обрано` and similar errors during the selection of the occupant, start of construction, cancellation and orders on the map.

The message channel contained both the finished text and the translation keys. The main cycle mistakenly passed each message as a key. Now the card message returns as ready text: the known key is translated, the normal text is saved. The same protection is applied to the reasons for the rejection of orders and messages HUD. A rigorous key verification of the usual signature interface remains.

A check of the choice of tenant, building regime, actual location of the house, cancellation, movement and server messages via real SDL/RmlUi is added in five languages. The inspector's previous inspection failed to process the message in the main cycle. Now she's using a game-compatible processor.

The rules 17, protocol 19, storage format 8 and compatibility of worlds remain unchanged. The player's savings and profiles are not deleted. This version is a timely fix after 0.25.12; the next plan version is 0.26.0.
