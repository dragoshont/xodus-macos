/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef XODUS_RPS_H
#define XODUS_RPS_H

enum xodus_rps_result
{
    XODUS_RPS_OK,
    XODUS_RPS_CONFIGURATION,
    XODUS_RPS_TRANSPORT,
    XODUS_RPS_TIMEOUT,
    XODUS_RPS_PROTOCOL,
    XODUS_RPS_MEMORY
};

/* Returned tickets use malloc ownership. Failure leaves both outputs NULL. */
enum xodus_rps_result xodus_rps_get(const char *endpoint, const char *client_id,
    int allow_ui, int full_trust, unsigned int timeout_ms,
    char **user_ticket, char **device_ticket);

#endif
