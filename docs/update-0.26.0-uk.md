# Ribterra 0.26.0  Boards and bricks provide useful improvements

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The completed House and Workshop provide **“Upgrade to level II.”** No research building is required. Each upgrade still costs **6 boards and 4 bricks**; a worker carries them to the building and performs the work.

The inspector shows the level, the price, the materials needed, the worker, the progress and the specific effect. Automatic selection takes the free adult without constant work. The employee remains explicit and moves on to management. The lack of materials is waiting for the order, and the panel calls the cause. When another order is placed, it must either wait for completion or be cancelled.

- **House II:** 10 housing places instead of 6, adding +4 to the existing housing-capacity index. Changes to resident-admission rules belong to the separate 0.26.1 update.
- **Workshop II:** the working phase of production is twice as short. For a batch of bricks, this is 200 times 400 (10 instead of 20 seconds at 1 speed). The journey and delivery time is not reduced.
- **Stockpile II:** its button is hidden; the code for the existing upgrade is preserved. No new bonus was added.

The logs turn the wood into boards and wood. The clay quarry yields clay; The workshop turns clay from wood into bricks. The boards and bricks are being used for improvement. Wood is also needed for cooking. These roles are explained in the inspectors.

Cancellation uses an existing production order contract: the cargo in hand completes the delivery first, the unused materials and the return of unfinished work are stored as physical items near the structure. The completed improvement is not reversed or repaid. Storage in the middle of the job keeps progress and materials already spent.

The rules **17**, protocol **19**, format of the save **8**, generator **5** are unchanged: an existing upgrade command is used without a new order type or price change. [Checks](verification-0.26.0-uk.md).
