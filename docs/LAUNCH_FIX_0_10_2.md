# Fixing the run 0.10.2

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

9 September 2026 of the year.

In the 0.10.1 header SDL- window, it remained tightly written as «pre-alpha 0.4», although engine_version and runtime were new. It was misleading. In addition, Play Ribterra only worked at the checkout from dist/current-build.txt; GitHub source ZIP did not contain EXE, and the finished Windows ZIP did not contain this launcher at all. The README packet was left over from 0.8. A previous review of framebuffer did not verify the OS- window title and launch via CMD.

0.10.2 takes the title from engine_version; renderer-info records the actual SDL window title and engine version. The launcher enters the ready ZIP, runs from its own folder, passes arguments, and leaves the error visible. The source checkout checks the vcpkg.json version itself: in the absence of a local file, a private GitHub release ZIP + SHA-256 is downloaded through an authorized gh. If gh is unavailable, a ready-to-use packet page is displayed. Assumption of the old dist/Ribterra withdrawn; the installed previous versions are saved.

It was launched via the Play Ribterra repository with an old pointer on 0.10.1 and through Play Ribterra with a unpacked ZIP in a blank folder, with another cwd. Both gave an exit 0, D3D12/RTX 3050 Laptop, engine_version=0.10.2 and actual window_title=Ribterra 0.10.2. The new package went through a dedicated server + two probes + graphical client. [Results](evidence/launch0102-results.json), [metadata ZIP](evidence/launch0102-package.json).

The game rules, simulation, and network code remained the same in this patch. Save 8 / protocol 7 / rules 7 / generator 2 is saved. Previous Windows 52/52, Linux 35/35 and UBSan 13/13 in VALIDATION are related to 0.10.1; This document does not replace them with the results of the new complete CI- collection. A separate CI new revision is launched during push.

Additionally verified the actual GitHub source ZIP: the launcher itself downloaded the private release, verified SHA-256, installed the package, and opened 0.10.2. The first such break detected a incompatibility Get-FileHash/Expand-Archive with the inherited PowerShell 7 module path in Windows PowerShell 5.1; installer is translated into standard .NET SHA256 and ZipFile, the final plug-in is successful. Also corrected and verified Play Ribterra in the downloads folder already downloaded by the user; The original launcher is saved as .bak.
