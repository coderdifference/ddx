# Items and saved data

DDX stores custom integer fields through native save adapters. Register them before loading a campaign so restoration knows which fields to read.

## A saved counter

The native example registers `exm_kills` with `field_register` for each hero and `campaigndata_field` for the campaign total. On a hero kill it obtains the actor's guid, reads the old value and writes the incremented value. The overlay reads the campaign total.

Follow the same pattern for a class resource, progression counter or campaign flag. Prefix every key with your mod's namespace. Keep a stable meaning for each saved key; changing its interpretation requires a migration strategy. A hero guid is an identifier, not a pointer that keeps the hero alive.

Use the monster and item field services for data owned by those objects. Item identity and native storage matter: copying an item, splitting stacks, selling it and leaving an expedition can follow different paths. Inspect the API contract and test each path your mechanic supports.

## Custom item types

Register a type during client initialization using the item-type service. Implement the callbacks your item needs: identity and tags, icon/drawing, tooltip content, price, sale or salvage, use rules and expedition settlement. [The C API reference](14-api-reference.md) and [UI library](11-ui-library.md) describe the registry and inventory integration.

Keep vanilla object access inside DDX's native adapters. C# clients use `Api.Objects` and its item views. Adding a managed subclass does not add memory to the game's object; persist your extra values through registered fields or your own explicitly managed records.

## Equipment and inventory UI

Use shared equipment-slot and stash services rather than replacing native storage. A temporary equipment-row override must be restored even when description generation fails. In C#, dispose owned `EquipmentRow` scopes on the game thread; nested scopes unwind in reverse order.

Retain stable IDs and reacquire object views after inventory mutations. A drag, purchase or settlement operation can invalidate an item address.

## Save checks

With a disposable campaign, create data, save, exit the process and reload. Repeat after unequipping, dismissing a hero, moving an item, selling, splitting stacks where supported and returning from an expedition. Test mod deactivation and reactivation. Back up the campaign before changing a field's format or removing an item type.
