# Ribterra 0.21.1 + 0.21.2

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Both updates are in the local version 0.21.2. The settlement panel opens by clicking on the population in the upper band.

## 0.21.1 — Settlement settlement and settlement decision

- Day goes from 06:00 to 18:00, night — from 18:00 to 06:00; Each part takes 12 minutes at the rate of 1 times.
- The usual work and the path to its completion is stopped at night and continued during the day. The cargo already taken is delivered to the warehouse; The assignments are not lost. The house movements and feeding continue.
- Night shift allows you to work until the next morning. The solution is available only at night, adds +5 dissatisfaction once a night; The next night, we need a new solution. There's no fatigue and no productivity penalty the next day.
- The panel shows the overall dissatisfaction 0–100 and the contribution of each cause. In this version, the causes don't go away automatically; conflicts and other consequences have not yet been added.
- The name of a profession does not initiate a job search: it is a job assignment or a job assignments. The collection of wood, stone and wild food remains a limited manual task with 0.21.0.
- For ready-made pubs and religious houses, night work is allowed. Their productive and social functions have not yet been realized; It's not a new source of resources or a decrease in dissatisfaction.

## 0.21.2 New residents and youth

- The invitation immediately lists 50 free food from the warehouse. One resident is expected at a time; for food shortages or reaching the population threshold 1500 solution is not available.
- In 2 minutes on 1× a young resident arrives. In 3 minutes, he becomes an adult.
- The young eat and play, are smaller in size and have an age mark. The usual work assignments and assignments are blocked until adulthood.
- A specific button allows the work of a particular young resident: +10 dissatisfaction once in his youth. The night shift continues to operate. Permission does not create automatic mining.
- Both timers use simulation time: they stand at a pause, accelerate at 2×/4×. The waiting period, age, decision and reasons for dissatisfaction are stored and passed on to other players. The same re-decisions do not charge twice.

The quantities +5 and +10 — are the initial balance of this realization. Changes in food and winter farm work from 0.21.3 are not included in this package.

The format of the stored data is 8, rules 14, network protocol 15. Reserves 0.21.0 are compatible; For a joint game, the client and server must be updated.
