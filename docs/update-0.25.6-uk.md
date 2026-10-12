# Ribterra 0.25.6 — Connecting to the online world

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The client no longer opens the game screen immediately after clicking «Join». The network used to be still taking pictures, and the interface was already showing an empty world with zero resources. For the initial download, there is now a menu with a connection message.

After verifying the versions and confirming the connection, the client expects all parts of the original image to be complete. Only after its application can it go to the lobby or the world. An incremental packet without the original full image is rejected with a request for re-sync.

- If the host has not started the game yet, the host sees the lobby, chooses the team and confirms readiness. The host is running the start.
- After launching, the client discovers the world when the game status and cells of its territory are obtained. The camera is moving to the people of his nation.
- If the game has already started or the host has continued to save, the late guest enters the current world after loading their full state. There is no repeat lobby for all participants.
- The world and the storage remain on the server. The client receives a projection of the state of his nation; Hidden in the fog, the territory of other nations is not revealed.

To check on two computers, unpack the entire `Ribterra-Windows-x64-0.25.6.zip` on both and launch the game from a new folder. The window title should indicate **0.25.6**. The format of the storage and the version of the protocol remain unchanged.

Local control results and their limits  in `verification-0.25.6-uk.md`.
