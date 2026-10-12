# 0.25 — Cooperative that can be used

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Current implementation: [0.25.0 — Online lobby and teams](update-0.25.0-uk.md). Under the user's latest clarification, each player starts on a separate team; choosing the same team gives players shared control of one nation. Implemented permissions: nation decisions are available to its team; pause is available to everyone, while resume and speed controls belong to the host. Automatic LAN discovery, diplomacy, war, and Steam remain outside this release. The original specification is preserved below; it is not a list of completed features.

Historical status: volume 0.25 at the user's request and re-audit of the code 0.24.1 from 2026-09-13. It's a specification, not a real release. It moves the cooperative from the previous plan 0.22 to 0.25 and retains the existing changes 0.23–0.24.1. The user confirmed: individual settlements are individual nations, each with its own; The possibility of diplomacy and war. The user also confirmed: initially a cooperative and independent nations; Diplomacy and war are the next step. The rules of authority are waiting for an answer.

## Session for 2–6 players

In the menu: Create a shared world» and Join». The creation specifies the name, password, number of places 2–6, seed, new world, or extension of storage. Connection via LAN or address and port. Password does not get into chat, log or command line.

The room shows the participants connected, their settlements and their willingness. The player chooses to join a joint settlement or to establish his own. The world doesn't simulate until it starts. The master starts the game when everyone is ready; The empty spaces do not block the start. Disconnecting reduces readiness, and changing parameters reduces it for everyone. For late connection, the world already launched does not return to the room.

A different server uses the same rules. LAN doesn't need Steam. The search for LAN-servers must be completed by entering the address manually; automatic port opening and the Internet retransmitter are not allowed.

## Settlements and starting positions

User confirmed: individual settlements — are separate nations. They have their own people, their own resources, their own production orders, their own ration, their own discontent and their own events. Property and intelligence are also divided by nation. The members of a settlement jointly manage it, maintaining the rules in force for the control of individual residents. A transfer of control over a person does not transfer ownership of a settlement or its resources.

The location is determined by the server's seed and the number of slots to start. A set of suitable terrains is generated with the least distance between them and the edges. For 4–6 points it is impossible to provide the same pairs of distances in the plane. They check the passage, the building site, the wood, the stones, the food and the water. The designation of the pitches to the players is mixed up and reproduced; An unsuitable set is reproduced a limited number of times or the creation ends up in a clear error.

Unoccupied seats are reserved for later joining. The existing settlements are not moving. The location is not re-exposed once the server is turned off or restarted. No hidden areas are disclosed through network data. The rules of joint intelligence need to be determined separately.

## Authority

User choice proposition: Each settlement has an owner who can appoint and withdraw co-directors from its settlement members. The owner and co-owners decide on night shift, diet change, population replenishment and response to events. Ownership of individual residents does not in itself confer these powers. The other participants see the author and the result. The commander of the convoy shall direct its route, attack and retreat; The night shift permit is subject to settlement rights.

The session master is in charge of the room and the players' exclusion. This role does not provide automatic access to other people's inventories or orders. After the termination of the principal, the principal shall not be transferred by accident to the first client; a clear rule of transfer or a predefined successor is required.

Time is shared across the whole world. The current proposal, pending the user’s choice, allows any participant to pause; the host may resume and select speed 1×/2×/4×. Everyone sees who paused and the current speed. The server applies time changes, including commands received during a pause. The earlier proposal for individual pause requests and the slowest requested speed has not been accepted.

## A decision is applied once

The team contains a stable identifier of the decision or event, settlement, expected revision, and specific response. The server checks the authenticated author, membership, decision-making rights and status. Checking, writing down resources, changing status, and recording results are done in one orderly operation.

For two answers to one event, only the first is acceptable. Repeat returns a known result or message Solution already adopted», without re-writing, increasing discontent or creating a drive. The new request_id of the other player does not allow the defense to bypass: the key is connected to the solution, not just the network request. Results and effects are kept together with the world, including the boundary before/after client confirmation.

Night leave maintains a valid semantics: the current night and the current task, the one-time dissatisfaction. Reopening the message is not a new solution.

## Chat, reconnect, and saves

The chat has a length and frequency limit and a secure display of text. The late participant receives a coordinated shot, then changes from its revision. Until we get a full picture, orders are blocked.

A short disconnect retains identity and control for a limited time. Continuing after the server restarts restores the settlement and role through the participant's saved credentials, not just a name match. Confidential certificates are not included in a public image. A world backup contains settlement links, occupied starting points, and decisions already made.

## What source inspection confirms

- There is a separate server, direct connection, password verification, chat, late image capture, and automatic short-term interconnection: `network_session.hpp`, `src/net/network_session.cpp`, `src/client/client_session.cpp`.
- There is inhabitant control and its transmission, ordered commands and cache of repeats request_id: `game_server.hpp`, `src/server/game_server.cpp`.
- The limit is now 4, not 6: the constructor `GameServer`.
- The menu does not have session creation, connection form and standby room: `assets/ui/menu.rml`.
- The remote pause and speed are now blocked by the client: `src/client/main.cpp`.
- Population and rationing are global; server projections and some of the checks are tied to nation 1. The presence of a nation field does not in itself mean a working-class individual settlement.
- The revisions already protect current population decisions, overnight change, and diet from outdated repetition. Separate settlement rights and sustainable event keys are still needed.
- The GameServer download clears the player sessions and control rights. Restoring the world is not the same as restoring membership after a reboot.

This is code audit; The new features were not launched or tested with this test.

## Implementation and acceptance order

1. To uphold the status of settlements, membership and rights; Add the migration of the old single settlement. Check the insulation of stocks, population, orders and decisions.
2. Add 2–6 slots and generate starting places. Verify playback, distance, available resources and reserve for later connection.
3. Add the creation/connection menu, the standby room and the server timeline. Verify the readiness, disconnection, overload and wrong password by real customers.
4. Complete membership retention, reset after rebooting and one-off decision. Verify the simultaneous responses of two players, repeat the packet, break to confirmation, save and re-load.
5. Go through a six-client scenario: at least two settlements, co-management, residence transfer, chat, late entry, re-connection, pause/speed, retention and extension. Check the interface on the screen and the regressions 0.24.1.

Steam lobby and invite friends — separate upload after SDK/AppID. They're supposed to use the same roles, rights and server simulation.

## Open solutions to changing world models

- The independence of nations is confirmed: there are no common reserves between them. Several players can rule a nation together.
- Who makes the settlement decision: owner and designated co-directors (recommended), only owner or all participants.
- Confirmed: diplomacy and war are the next step after the co-operative and independent nations. The 0.25 base does not have any ethnic attacks, property exchanges or common stock.
- Intelligence is shared by members of one nation. The rules for voluntary exchange of intelligence between nations have not yet been determined.

The installed game number remains 0.24.1 until implementation and passing acceptance checks 0.25.

## Nations, Diplomacy and War

New user requirement: other settlements are other nations; You can have diplomacy or you can have war. These are not just extra camps with a common setup.

The server checks the author's nation for orders to residents, construction, costs, production, rent, nighttime decisions and response to events. The transfer of control of the inhabitant within the cooperative must not give a foreign nation possession over it. The transfer of persons or property between nations requires separate, clearly agreed action.

Preservation and reconnection restore the nation and the player's role. A player's name is not a membership document. A free space in a room does not mean the right to join a foreign nation without its permission.

Future diplomacy requires a relationship state between each pair of nations and explicit authority to negotiate. Peace, alliance, trade, territorial access, and intelligence sharing are different agreements; none should be treated as an automatic result of a cooperative connection.

Diplomacy and war are postponed by the user for the next upgrade. It is defined separately by declarations of war, the permissible targets of attacks, the attack and retreat of real residents, the loss and cessation of hostilities. The lack of self-defense against animals does not provide this. The seizure of buildings, the siege and the complete destruction of the nation have not yet been agreed upon.
