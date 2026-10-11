#include "exception-module-inventory.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures, closes;
static uintptr_t last_closed;

static void check(int result, const char *name)
{
    ++checks;
    if (!result) { ++failures; fprintf(stderr, "FAIL %s\n", name); }
}

static int close_file(void *context, uintptr_t file)
{
    int *success = context;
    ++closes;
    last_closed = file;
    return *success;
}

int main(void)
{
    struct exception_inventory inventory = {0};
    struct exception_module snapshot[EXCEPTION_MODULE_LIMIT];
    unsigned i;
    int success = 1;
    for (i = 0; i < 1024; ++i)
    {
        check(inventory_load(&inventory, 0x1000, 0x2000, L"repeated.dll") == INVENTORY_OK, "cycle load");
        check(inventory_unload(&inventory, 0x1000) == INVENTORY_OK, "cycle matched unload");
    }
    for (i = 0; i < EXCEPTION_MODULE_LIMIT; ++i)
        check(inventory_load(&inventory, 0x1000 + i * 0x10000, 0x2000, L"live.dll") == INVENTORY_OK,
              "exact128 live admission");
    check(inventory_snapshot(&inventory, snapshot) == EXCEPTION_MODULE_LIMIT, "snapshot exact128");
    check(inventory_load(&inventory, 0x9000000, 0x1000, L"overflow.dll") == INVENTORY_FULL, "129th live overflow");
    check(inventory_load(&inventory, 0x1000, 0x3000, L"duplicate.dll") == INVENTORY_DUPLICATE, "duplicate live base");
    check(inventory_unload(&inventory, 0xdead) == INVENTORY_UNKNOWN, "unknown unload");
    check(inventory_unload(&inventory, 0x1000) == INVENTORY_OK, "reuse matched unload");
    check(inventory_load(&inventory, 0x1000, 0x7000, L"replacement.dll") == INVENTORY_OK, "same base changed identity size");
    check(snapshot[0].base == 0x1000 && snapshot[0].size == 0x2000 &&
          !wcscmp(snapshot[0].name, L"live.dll"), "old snapshot immutable after reuse");
    check(inventory.entries[0].size == 0x7000 && !wcscmp(inventory.entries[0].name, L"replacement.dll"),
          "new identity not retroactive");
    check(inventory_unload(&inventory, 0x1000) == INVENTORY_OK, "snapshot boundary unload");
    check(inventory_snapshot(&inventory, snapshot) == 127, "unloaded identity absent from next snapshot");
    check(inventory_unload(&inventory, 0x1000) == INVENTORY_UNKNOWN, "double unload rejected");
    check(inventory_load(&inventory, 0, 1, L"invalid.dll") == INVENTORY_INVALID, "invalid base rejected");
    check(inventory_finish_load(1, 42, close_file, &success) && closes == 1 && last_closed == 42,
          "accepted loader file closed exactly once");
    check(!inventory_finish_load(0, 43, close_file, &success) && closes == 2 && last_closed == 43,
          "failed loader file closed exactly once");
    success = 0;
    check(!inventory_finish_load(1, 44, close_file, &success) && closes == 3, "close failure fails admission");
    check(inventory_finish_load(1, 0, close_file, &success) && closes == 3, "null loader file not closed");
    check(inventory_finish_load(1, UINTPTR_MAX, close_file, &success) && closes == 3, "invalid loader file not closed");
    printf("INVENTORY_TEST checks=%u failures=%u closes=%u\n", checks, failures, closes);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
