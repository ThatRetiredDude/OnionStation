/* OnionStation5: what the start code needs from each platform.
 *
 * Copyright (c) 2026, ThatRetiredDude. BSD-3-Clause, see LICENSE. */

#ifndef ONIONSTATION_START_H
#define ONIONSTATION_START_H

/* A short message for the user: a PS5 notification, or a log line elsewhere. */
void os_notify(const char *message);

/* Anything the platform needs before Tor starts. */
void os_platform_init(void);

#endif
