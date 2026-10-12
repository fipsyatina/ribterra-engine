# Ribterra 0.23.0 — Enterprise and the basic economy

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

## How to Use

Build a business through all the buildings. Select the occupant on the map, open the «Entrepreneurship» button on the bottom left, select 1, 5, 10 or 20 parties and the recipe for the building you want. One building is executing one order at a time with one worker. The employee is required to complete or cancel another job; You can set a free pass during a walk.

The panel shows the worker, the recipe, the number of batches completed, the materials needed, the reserves, the stage and the remaining working time for the entire order. Road time, waiting materials and nighttime breaks are not part of this calculator and depend on the world.

The employee takes the materials from the central warehouse or camp and physically relates them to the enterprise. The materials are consumed at the beginning of the batch only after delivery. The product is created after working near the building and is placed in the warehouse. After a certain number of parties, a person returns to his home. No materials or spare parts waiting for the order.

Fish stations no longer produce food passively. The old farmer's assignment to a particular farm continues to operate; The new orders from the farm are clearly allowing you to choose grain, vegetables or meat. A profession without a job does not start production on its own.

## Recipes

The numbers are all on one party. Duration measures work on 1× without road and waiting.

| The enterprise | Materials | Production | Time is now |
|---|---|---|---|
| Farm | Work | Grain ×8 or vegetables ×8 | 20 c |
| Farm, livestock | The grain is ×6 | Meat ×4 | 30 c |
| Fishery station | Work | Fish ×6 | 20 c |
| The Forest | The tree ×3 | The tablets ×6 | 15 c |
| The Forest | The tree ×2 | The wood ×6 | 10 c |
| Mill | The grain is ×4 | It's a mess. ×4 | 15 c |
| The kitchen | Boron ×3 and wood ×1 | Ready food ×8 | 15 c |
| The kitchen | Fish ×3 or meat ×3, and wood ×1 | Ready food ×8 | 15 c |
| The kitchen | Fruits and vegetables ×4 and wood ×1 | Ready food ×8 | 15 c |
| Workshop | Clay ×4 and wood ×1 | The brick ×4 | 20 c |
| Workshop | Boards ×2 and stones ×2 | Instruments ×2 | 20 c |
| Clay quarry | Work | The clay ×6 | 20 c |

Manufacturing here — production operation of a farm with feed consumption; The individual animals, the herd and the hunt remain the next mechanism. The instruments are already physical products; The individual equipment of the residents has not yet been added. The quarry is not currently depleted.

Wood and stone retain limited manual instructions. The mine must be built for new underground access; The extraction is physically delivered. The old open passageways of the preservation remain compatible.

## Roads and improvements

The road costs 1 wood and 2 stone per cell, is built through normal material delivery, and speeds up travel on the finished cell by 35%.

Increasing to II level costs 6 a plate and 4 bricks, requires an employee and 40 seconds of work after delivery:

- the house: 10 places instead of 6;
- composition: 10 000 units of capacity instead of 5 000;
- Workshop: production time is twice as short.

Repeated improvements to II levels are neither paid for nor performed. The new buildings use an existing prepared atlas: the log has its own image, the mill and quarry until the images of existing buildings are reused. Roads are reflected by material on the surface.

## Preservation of Settlement Rules

Night stops normal production and the way to it. The cargo is already completed. The forced night shift from 0.21.1 allows you to continue working only that night. Young people need a separate work permit. There's no fatigue and no penalty the next day.

Cancellation is available upon completion of the current delivery or route search. The materials of the unfinished batch are returned with physical items; materials at the workplace become available to be returned to storage. The finished product is stored.

Network solutions have audit verification and inhabitant control: simultaneous re-ordering does not create secondary costs. Parts, progress, materials, supplies, supplies and level of building are kept. The format of the saves 8; network protocol 16. The client and server need to be updated together. Save load 0.21.2 is supported.
