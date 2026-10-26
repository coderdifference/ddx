using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Enumerates borrowed items and exposes native inventory slot and mutation operations. Inventory changes may invalidate item views and enumeration positions.</summary>
public class Inventory : GameObject, IEnumerable<Item>
{
    /// <summary>Creates a Inventory using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Inventory(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the native inventory item-count value exposed by the slot adapter.</summary>
    public int ItemCount => Api.Objects.Int("game_inventory_item_count", Handle);

    /// <summary>Gets the number of native inventory slots, including empty slots.</summary>
    public int SlotCount => Api.Objects.Int("game_inventory_slot_count", Handle);

    /// <summary>Gets a borrowed item view for a native slot index, or null when that slot contains no item.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    public Item? Slot(int index) =>
        Api.Objects.Item(Api.Objects.At("game_inventory_slot", Handle, index));

    /// <summary>Removes the specified native inventory slot. This mutation can invalidate existing item views and indices.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    public void RemoveSlot(uint index) => Api.Objects.RemoveSlot(Handle, index);

    /// <summary>Gets the number of items available through indexed native item enumeration.</summary>
    public int Count => Api.Objects.Int("game_inventory_count", Handle);

    /// <summary>Gets the borrowed inventory item at the specified item index, or null when no native item is present.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    public Item? this[int index] =>
        Api.Objects.Item(Api.Objects.At("game_inventory_item", Handle, index));

    /// <summary>Removes the supplied native item from this inventory. Reacquire item views after the mutation.</summary>
    /// <param name="item">Item or portable gear record used by the operation; native item views remain borrowed.</param>
    public void Remove(Item item) => Api.Objects.Remove(Handle, item.Handle);

    /// <summary>Requests a native inventory copy of the supplied item. The source view remains borrowed; returns whether the copy succeeds.</summary>
    /// <param name="item">Item or portable gear record used by the operation; native item views remain borrowed.</param>
    public bool AddCopy(Item item) => Api.InventoryCopy(Handle, item.Handle);

    /// <summary>Enumerates non-null borrowed item views in native item order. Do not mutate the inventory while enumerating it.</summary>
    public IEnumerator<Item> GetEnumerator()
    {
        for (int i = 0; i < Count; i++)
            if (this[i] is { } item)
                yield return item;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
