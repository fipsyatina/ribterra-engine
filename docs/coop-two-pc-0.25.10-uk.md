# 0.25.10 — Short check on two Windows PCs

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Status: NOT VICKED.** The user replied that the second physical PC is unavailable. Two customers on the same PC don't count. This document is a scenario for future verification, not its outcome.

On both PCs, unpack the same `Ribterra-Windows-x64-0.25.10.zip`. Run `Play Ribterra.cmd`, and then fold 0.25.10 into the title. For the first test, use one local area network or an existing shared VPN. The master copies the address of the adapter from the front; `127.0.0.1` not suitable for a second PC. Don't change the rules of the router for the local scenario.

## Joint team No1

1. On PC A, create a new online room. Please join PC B at A. Both see the command No1 and an explanation of the common settlement.
2. Both press Ready, Master Start. The camp, the residents, and the initial supplies: they're the same facilities.
3. On A, put the log by the camp. On B, track the same delivery of raw materials and completion of construction. Keep the stock expenses down.
4. On A, order two batches of boards through the inspector. On both PCs, verify the worker, recipe, batch count, completed batch in transit, and start of physical delivery. A finished product being carried does not yet count as stockpile inventory.
5. Try to cancel this order on B while A controls the worker. The request must be rejected with player A's name, while the order continues. On A, transfer the resident to player B through control management. After the two batches finish, verify **12 boards in storage** on both PCs. B then starts one new batch with that resident; after delivery, both clients see **18 boards**.
6. The master keeps the world. Both of them are coming out. A continues the saved online game, B returns with his session. The team, the structure and the results remain the same.

## Joint team No2

Create a new room. The master changes command to No2 before the guest enters. The guest automatically receives the No2. Repeat the short steps 2–5: your own Forestry is available to both, production and delivery are running, controller and summary are matching.

## Independent settlements

In the other new room, A will be left in No1, and B will obviously choose No2. The lobby should warn of the settlement. After launching, B builds and operates his own Forest Lawn. The B boards and expenses do not change the A stock. Other structures should not appear on their own production list. The server failure for the ID foreign construct is individually verified by a local automated test.

## Record of the result

Fill in after actual execution:

| Field | The result |
|---|---|
| Date, name/model of two different physical PCs | Not executed |
| Version and SHA256 package on A/B | Not a freak |
| Type of connection: LAN or VPN | Not verified |
| Command No1: construction, cargo, 12 rack | Not verified |
| Team No2: the same results | Not verified |
| Refusal to a foreign controller and explicit transmission | Not verified |
| Conservation and return of both participants | Not verified |
| Independent command No2: own reserves | Not verified |
| Both PCs and detected errors | Not provided |

If any of the steps are not completed, write down the number, time, last shown status and message. Don't label the whole scenario as successful just because you're connected.
