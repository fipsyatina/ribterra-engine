# Ribterra 0.25.0 — Online lobby and team

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The main menu was online gaming. The master enters the name, password and number of places 2–6 and presses «Create a shared world». Friends enter the IP:port and password and click «Join». LAN-The addresses of the master are shown in the room. A direct Internet connection uses an external UDP-server port (typically 27020); Steam and the relay are not needed and not on.

By default, each participant receives a separate team. You can select the same team number before the start — this is a common nation with one settlement. Team changes make everyone more prepared. Each presses Ready», after which the master starts the game. At least two connected players are required; The unoccupied seats don't block the start. The world doesn't move until the start.

The teams get independent residents, warehouses, production, ration, discontent, solutions and intelligence. Start sites spread around the world by seed; There's trees, stones, food and water around each camp. Members of one team run the nation together. The transfer of control over the inhabitants is available between the members of their team. You can't change the team after the game.

The decisions within the team may be made by its members; The state of the reviews protects against simultaneous repetition. Changing someone's diet, spending someone else's supplies, or ordering someone else' s food is not allowed. Diplomacy and war are postponed until further notice.

Anyone may pause the world; the host may resume and change speed 1×/2×/4×. The rules apply to buttons and keys. Chat and short-term interconnection work through the available network transport. The late participant gets the actual world; The free team gets its own settlement.

The master can save the game with the button or F5; It's an automatic storage. Promoting the common light» opens up the latest online world. Teams, nations and participant certificates are kept with the world. To return, use the same player profile and server address. The extended world begins on pause: the master can wait for friends and restart the game. Local credentials are stored in the online-profiles.json file in the game settings directory; The password is not written there.

Other server:

```powershell
.\RibterraServer.exe --bind 0.0.0.0 --port 27020 --players 6 --lobby 1 --world .\worlds\online.save
```

The password of each server is transmitted via RIBTERRA_SERVER_PASSWORD. For direct client launch, --connect IP:port is also saved.

All network participants must use 0.25.0 (protocol 18). The old single-scale preservation, particularly 0.24.1, remains compatible. Saved 30 seconds of waiting after completing manual commands and slow walking animations.

Steam lobbying and inviting friends — separate upload after SDK/AppID.
