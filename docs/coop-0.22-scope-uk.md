# 0.22 — Cooperative that can be used

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: specification of the volume and audit of the current code 0.24.1 from 2026-09-13. It's a plan, not a release report. The number 0.22 indicates the line of the road map; The implementation should retain the existing changes 0.23–0.24.1.

## Session for 2–6 players

In the menu: Create a shared world» and Join». The creation specifies the name, password, number of places 2–6, seed, new world, or extension of storage. Connection via LAN or address and port. Password does not get into chat, log or command line.

The room shows the participants connected, their settlements and their willingness. The player chooses to join a joint settlement or to establish his own. The world doesn't simulate until it starts. The master starts the game when everyone is ready; The empty spaces do not block the start. Disconnecting reduces readiness, and changing parameters reduces it for everyone. For late connection, the world already launched does not return to the room.

A different server uses the same rules. LAN doesn't need Steam. The search for LAN-servers must be completed by entering the address manually; automatic port opening and the Internet retransmitter are not allowed.

## Settlements and starting positions

Proposition to user choice: individual settlements have their own inhabitants, resources, production orders, ration, discontent and events. The members of a settlement jointly manage it, maintaining the rules in force for the control of individual residents. A transfer of control over a person does not transfer ownership of a settlement or its resources.

The location is determined by the server's seed and the number of slots to start. A set of suitable terrains is generated with the least distance between them and the edges. For 4–6 points it is impossible to provide the same pairs of distances in the plane. They check the passage, the building site, the wood, the stones, the food and the water. The designation of the pitches to the players is mixed up and reproduced; An unsuitable set is reproduced a limited number of times or the creation ends up in a clear error.

Unoccupied seats are reserved for later joining. The existing settlements are not moving. The location is not re-exposed once the server is turned off or restarted. No hidden areas are disclosed through network data. The rules of joint intelligence need to be determined separately.

## Authority

Proposition to user choice: Each settlement has a leader who can delegate the right to make joint decisions to another member of that settlement. The manager decides on night changes, diet changes, population replenishment and response to events. The other participants see the author and the result. The commander of the convoy shall direct its route, attack and retreat; The night shift permit is subject to settlement rights.

The session master is in charge of the room and the players' exclusion. This role does not provide automatic access to other people's inventories or orders. After the termination of the principal, the principal shall not be transferred by accident to the first client; a clear rule of transfer or a predefined successor is required.

Time is the only time for the whole world. Recommended initial policy: put the game on hold by each participant; The server stores the authors of active breaks. Restoration is possible after all requests for a break are withdrawn. The master has the visible effect of taking a break from the disabled participant. For the speed 1×/2×/ 4× the slowest of the selected active participants is used. The time changes are synchronized by the server and are accessible from the interface.

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
