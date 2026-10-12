# Ribterra 0.25.8 — building explains its work

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Press the left button on the structure. The inspector shows its name, destination, owner's nation, level, construction status, and employee. For the enterprise there is an active recipe, raw material per batch, batch output, manufactured items and batch residue.

The quantity in the form represents **object**. The panel shows how many whole batches are needed: for example, ordering 7 the board is rounded to 2 the batches, that is, 12 the board. An active order retains its own recipe even if there was another choice in the form before it was started.

The explanation of the condition is based on the characteristics:

- **No order:** select a recipe and start ordering.
- **No worker:** fire an adult resident and repeat the order. The unavailable worker's order will be stopped first.
- **Expect raw materials:** deliver the materials listed to the Warehouse or Warehouse.
- **Carries raw materials/goes to the building:** wait for arrival.
- **No way:** check access to the enterprise and the warehouse.
- **No delivery location:** release or add the available Warehouse.
- **Night rest:** wait for day or allow night shift. The cargo in hand completes the delivery at night.
- **Made; Delivery is ongoing:** the batch is ready, but it's still in the bag. It is not counted in the available stock for delivery.
- **Shipment on land:** manufacture completed, delivery failed; We need to move it to the storage room.

The Enterprise panel uses the same explanations. Group order errors are attached to the respective structure and do not change after a change in choice. Selecting another structure reduces the field of action of the new command to this structure. The message from the card is transmitted to the current RmlUi HUD.

The new texts are available in Ukrainian, English, Polish, Spanish and Russian. The existing layout is saved, the lines are moved and the waiting color is added. The long panel is rotating; The keypad focus spins it to the button.

The recipes, balance, protocol and storage format remain unchanged. The reorganization of employee management belongs to 0.25.9, the common ownership in the cooperative — to 0.25.10. This version does not confirm the work via the external Internet.
