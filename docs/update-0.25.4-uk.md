# Ribterra 0.25.4 — Speed of the world and recovery of the host

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The speed ×1, ×2 and ×4 changes only the host. The guests have no buttons and the 1/2/3 keys do not send the speed change commands. The server also checks the host's rights, particularly for members of its team.

“Continue shared world” restores the saved host regardless of the name or port currently entered in the form. The owner returns to their nation without creating an extra player or settlement. Guests continue to connect with their session credentials.

Once downloaded, the online world is waiting for a pause. Now the button shows correctly Continu»: the speed synchronization no longer hides the pause. The host presses it to run the simulation. This fixes a faked case where orders were being taken, but the residents and the clock were standing, and the interface indicated the game was working.

The pause, as yet, may be triggered by any participant; It's a game that's resurrecting the host. The speed change of the online world does not take a break in itself.

Network protocol and storage format remain unchanged. The worlds that exist are compatible.

Fixed Ctrl+A/Ctrl+V in input fields when using the Ukrainian keyboard layout, and fixed the “Online game” label in the main menu.

Verified by computer control in two connected windows on one PC: restore the existing world, connect a guest, share time, command a guest's movement, change the host speed and block it for the guest. After the break, both clients showed the same time 10:44. A backup copy is used for verification.
