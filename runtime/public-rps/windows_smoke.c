/* SPDX-License-Identifier: GPL-3.0-only */
#include "xodus_rps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *user = NULL, *device = NULL;
    enum xodus_rps_result expected, result;

    if (argc == 2 && !strcmp(argv[1], "--bootstrap"))
    {
        puts("Isolated Windows check started; no credentials requested.");
        return 0;
    }
    if (argc != 3) return 2;
    if (!strcmp(argv[2], "success")) expected = XODUS_RPS_OK;
    else if (!strcmp(argv[2], "protocol")) expected = XODUS_RPS_PROTOCOL;
    else if (!strcmp(argv[2], "timeout")) expected = XODUS_RPS_TIMEOUT;
    else return 2;

    result = xodus_rps_get(argv[1], "0011223344556677", 0, 1, 1500, &user, &device);
    if (result != expected)
    {
        fprintf(stderr, "Unexpected result %d (expected %d).\n", result, expected);
        free(user);
        free(device);
        return 1;
    }
    if (result == XODUS_RPS_OK)
    {
        if (!user || !device || strcmp(user, "syntheticUser") || strcmp(device, "syntheticDevice"))
        {
            fputs("Synthetic ticket outputs did not match.\n", stderr);
            free(user);
            free(device);
            return 1;
        }
    }
    else if (user || device)
    {
        fputs("A failed request retained ticket outputs.\n", stderr);
        free(user);
        free(device);
        return 1;
    }
    free(user);
    free(device);
    puts("Isolated Windows RPS outcome passed.");
    return 0;
}
