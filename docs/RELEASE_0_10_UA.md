# Ribterra 0.10.1 — completion of the 0.10 version

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

9 September 2026 of the year. Windows x64. Private collection of the development.

The 0.10 version adds manual rows Move/Gather, collecting one delivery or to deplete the resource, cancelling a separate order, and transferring control to the tenant. Shift + right button adds the command; The **«Instructions»** panel shows the status of the line and the actions available. The cargo first completes the physical delivery. The professions, skills, mining, construction and pre-construction work continue.

The final collection 0.10.1 corrects the outdated state of the client on pause after delayed photo confirmation. When the connection is lost, the client automatically tries to restore the same session. Unconfirmed commands are repeated with the same ID only within 30 seconds and for unchanged session rights; It prevents a repeat effect. If a safe repeat is not possible, the interface will ask you to check the commands. Chat after recovery is not repeated. The messages are available in five languages.

## Launch

In the project folder on the desktop, open **Play Ribterra.cmd**. To move to another Windows x64 PC, unpack `dist/Ribterra-Windows-x64-0.10.1.zip` completely and open `Ribterra/Ribterra.exe`. The control, storage, and direct IP are given in [README](../README.md).

The storage format — 8; protocol/rules — 7; generator — 2. The 0.10.1 collection does not change these formats. Previous versions and backups of the backups are preserved.

## Acceptance

- Local collection: 52/52 CTest; The final network profiles 1% and 3% were also passed after the scraping test was tightened.
- [CI sources `13ed5c2`](https://github.com/fipsyatina/ribterra/actions/runs/34285248451): Windows 52/52, Linux 35/35, UBSan 13/13; C++ warnings/errors — 0.
- Two customers through the real UDP: 150 MS base RTT, losses, duplication, delayed packets, cracking to automatic recovery, one result of conflict, precise cargo and final status.
- D3D12 / RTX 3050 Laptop: 36 frames, 16 of which byte-identical previously reviewed; 8 new messages are reviewed in five languages at 1100×700.
- Unpacked package: 4/4 checks — menu, mine/save-resume, manual commands, and server with clients. The menu, mine, and network game were reviewed separately.

The exact results, hashes and boundaries — in [VALIDATION](VALIDATION.md), including with [ the vptr exception for the adapter GNS](SANITIZERS.md). The packet has runtime `27da153`; CI `13ed5c2` adds only this setting of the sanitizer and its documentation. Network profiles are executed on one PC; The testing of 2–4 physical PCs and the full scale gate remain the next steps. Full motion, finite art concepts and Steam releases have specific requirements. The 0.11 implementation in this completion did not begin.
