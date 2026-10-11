#ifndef EXCEPTION_MODULE_INVENTORY_H
#define EXCEPTION_MODULE_INVENTORY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>

#define EXCEPTION_MODULE_LIMIT 128

struct exception_module
{
    uintptr_t base;
    uint32_t size;
    wchar_t name[260];
    int loaded;
};

struct exception_inventory
{
    struct exception_module entries[EXCEPTION_MODULE_LIMIT];
};

enum inventory_result { INVENTORY_OK, INVENTORY_DUPLICATE, INVENTORY_FULL, INVENTORY_UNKNOWN, INVENTORY_INVALID };

static enum inventory_result inventory_load(struct exception_inventory *inventory,
                                            uintptr_t base, uint32_t size, const wchar_t *name)
{
    size_t i, free_slot = EXCEPTION_MODULE_LIMIT;
    if (!base || !size || !name || wcslen(name) >= 260) return INVENTORY_INVALID;
    for (i = 0; i < EXCEPTION_MODULE_LIMIT; ++i)
    {
        if (inventory->entries[i].loaded && inventory->entries[i].base == base)
            return INVENTORY_DUPLICATE;
        if (!inventory->entries[i].loaded && free_slot == EXCEPTION_MODULE_LIMIT) free_slot = i;
    }
    if (free_slot == EXCEPTION_MODULE_LIMIT) return INVENTORY_FULL;
    inventory->entries[free_slot].base = base;
    inventory->entries[free_slot].size = size;
    wcscpy(inventory->entries[free_slot].name, name);
    inventory->entries[free_slot].loaded = 1;
    return INVENTORY_OK;
}

static enum inventory_result inventory_unload(struct exception_inventory *inventory, uintptr_t base)
{
    size_t i;
    for (i = 0; i < EXCEPTION_MODULE_LIMIT; ++i)
        if (inventory->entries[i].loaded && inventory->entries[i].base == base)
        {
            inventory->entries[i].loaded = 0;
            return INVENTORY_OK;
        }
    return INVENTORY_UNKNOWN;
}

static size_t inventory_snapshot(const struct exception_inventory *inventory,
                                 struct exception_module out[EXCEPTION_MODULE_LIMIT])
{
    size_t i, count = 0;
    for (i = 0; i < EXCEPTION_MODULE_LIMIT; ++i)
        if (inventory->entries[i].loaded) out[count++] = inventory->entries[i];
    return count;
}

typedef int (*inventory_close_file)(void *, uintptr_t);

static int inventory_finish_load(int accepted, uintptr_t file, inventory_close_file close_file, void *context)
{
    int closed = !file || file == UINTPTR_MAX || close_file(context, file);
    return accepted && closed;
}

#endif
