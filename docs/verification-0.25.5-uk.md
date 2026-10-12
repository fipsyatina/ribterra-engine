# Ribterra verification 0.25.5

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 15 September 2026. Windows, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU.

## Result

The network images now change the position of the inhabitants and animals. Fixed walking drop for normal movement to ×4. Reusing relief geometry reduces the processor's performance during frame construction.

| The control size ×1 | Before | After |
| --- | ---: | ---: |
| The average of the frame work | 12,54 ms | 8,93 ms |
| Construction of two stages, average | 6,85 ms | 3,37 ms |
| The biggest move of a resident between the cadres, client 1 | 22,81 px | 2,27 px |
| The images moving between packets, client 1 | 0 | 315 |
| The images moving between packets, client 2 | 0 | 122 |

The frame rate decreased by 28,8%, the stage build by — by 50,8%. Both launches are limited to 60 FPS and reached approximately 60 FPS: this measurement confirms a reduction in frame costs, but does not measure the increase in unlimited FPS.

The control option uses a previous way of showing the coordinates and relief structures with the same simulation, network, and measurement scenario. It's a comparison of algorithms, not a continuous output 0.25.4. By 360 personnel, two real UDP- clients, two nations, 20 residents, 96 animals, resource collection and movement orders. One graphic device sequentially paints both scenes at the size of 1280×720. Separate checks ×2 and ×4 have also been carried out; the largest first inhabitant step — respectively 4,51 and 8,92 px.

Network motion is shown with a buffer approximately 250 ms. If the packages stop arriving, the customer comes to the last known position and waits. The permanent loss of communication is not eliminated by smoothing.

## Checks

- The release collection and installation of the client and individual server were completed successfully.
- Eight targeted tests were performed: movement, input, frame, network sessions, lobby, atmosphere, routes and client launch.
- After the last time the display time was restored, four tests were repeated: unit_motion, client_input, lobby_client_rendered, network_client_rendered.
- Automatic checks include ×1/×2/×4, uneven packets arrival, pause, absence of packets, routes, shifting of the ball, teleportation, new session, and a large time lapse.
- The cascading relief compared to the fresh build, particularly after the camera moved and another world loaded.
- In the usual installed 0.25.5 window, the host continued a separate copy of the saved world, and the guest joined it. The speed buttons are not available to guests; The inhabitant's order, controlled by the host, is rejected with an explanation. Both windows show the world alive. The screen shall confirm the appearance and interaction; The numerical estimates of the movement are taken from a graphical measurement scenario.
- git diff --check was completed without error.

The journals and JSON- results are placed in `artifacts/online-0.25.5/` and `build/0255-*.log`. The guest's photo is: `artifacts/online-0.25.5/live/guest-connected.png`. The search was not done via the external Internet and another computer.
