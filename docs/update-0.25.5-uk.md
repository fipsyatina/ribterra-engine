# Ribterra 0.25.5 — Flat movement online and faster frame building

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Humans and animals are shown in the interim positions between the network images. The client maintains a brief history of the coordinates and displays a delayed motion of approximately 250 ms; local display uses 50 ms. The authoritative coordinates, execution of the commands and simulation speed remain unchanged.

On ×4, normal two-cell movement between packets no longer descends the walking cycle. The animation of the residents uses the displayed displacement; animals  smooth reflection time. The pause freezes the shown positions and the frames of the walk. In the absence of packets, the units stop at the last known position. Loading, re-connecting, teleporting, and crossing the surface and mine leave an inconsistent history.

The clustering and the allocation frame use the indicated positions of the inhabitants. The choice of the animal for hunting also corresponds to its depiction; The command is sent to the server by the target identifier.

The geometry of the unchanging cells of the relief is reused instead of the layers of material and fog of each frame. The camera movement moves the cached tops. Changes in terrain, season, scale or layer update the cache. The order of the painting and the time of day are preserved.

The speed of the online world, like the 0.25.4, is controlled by the host. The storage format and network protocol remain unchanged.
