# Ribterra verification 0.25.7

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 20 September 2026. Windows Release, MSVC, current working copy Desktop/ribterra.

## Changes

Windows addresses are obtained through GetAdaptersAddresses. Active IPv4 gateway adapters are prioritized; loopback and APIPA are rejected. The lobby contains the choice of adapter, copies the selected endpoint, and discharges the confirmation of copying after the choice changes. A host without an accessible address no longer offers its loopback address.

Expecting a host response is separate from loading the world. GNS conveys a structured cause of timeout; The initial failed attempt to connect shows a localized explanation. Other mistakes retain their meaning. Corrections to expect a full picture from 0.25.6 remain.

## Results

After the final round, the 12 target CTest checks were performed for 33,73 c: platform, localization, ui, ui_lifecycle, lobby_client_rendered, host_lobby_flow, online_entry_flow, network_join, network_session, network_lobby_join, client_input, transport.

NetworkJoinTests is connected to a dynamically selected, pre-reserved and released loopback UDP- port. The actual transport timeout completed the test in 10,90 s; The client is not ready to play and returns the network.connect_timeout.

LobbyVisualProbe checks six local UDP- clients, lobbies and world startups. New checks select a second network address with a keyboard, press the copy button, compare the content of the clipboard, and check the download confirmation after the selection change. A separate break in 960×620 has been made; lobby footage reviewed, online forms and timeout messages. On the compact screen, the settings panel is scrolling.

In this PC PlatformTests confirmed the priority of Wi-Fi with gateway over VirtualBox Host-Only without gateway. Final round is successful; the warning C5054 in game_ui.cpp remained unrelated to these changes. The full set of CTest tests didn't start.

## Evidence and limitations

- The set is build/0257-build.log, build/0257-final-build.log.
- The test is build/0257-tests.log, build/0257-compact.log.
- Installation and package: build/0257-install.log, build/0257-package.log.
- The frames: artifacts/online-0.25.7/ and artifacts/online-0.25.7/compact/.

Access via the external Internet is unconfirmed. The Zyxel router USG FLEX 700 displays the input page; The port forwarding rule in this session is not created, the WAN- address is not verified. The system firewall rules haven't changed. The presence of an external IP- connection address does not in itself confirm the absence of an additional NAT. The game update does not replace the router's UDP- port setting.
