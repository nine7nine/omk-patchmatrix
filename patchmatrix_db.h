/*
 * Copyright (c) 2016-2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the iapplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _PATCHMATRIX_DB_H
#define _PATCHMATRIX_DB_H

#include <patchmatrix.h>

// client
client_t *
_client_add(app_t *app, const char *client_name, int client_flags);

void
_client_free(app_t *app, client_t *client);

bool
_client_remove_cb(void *node, void *data);

void
_client_remove(app_t *app, client_t *client);

client_t *
_client_find_by_name(app_t *app, const char *client_name, int client_flags);

#ifdef JACK_HAS_METADATA_API
client_t *
_client_find_by_uuid(app_t *app, jack_uuid_t client_uuid, int client_flags);
#endif

port_t *
_client_find_port_by_name(client_t *client, const char *port_name);

void
_client_refresh_type(client_t *client);

void
_client_sort(client_t *client);

// client connection
client_conn_t *
_client_conn_add(app_t *app, client_t *source_client, client_t *sink_client);

void
_client_conn_free(client_conn_t *client_conn);

void
_client_conn_remove(app_t *app, client_conn_t *client_conn);

client_conn_t *
_client_conn_find(app_t *app, client_t *source_client, client_t *sink_client);

client_conn_t *
_client_conn_find_or_add(app_t *app, client_t *source_client, client_t *sink_client);

void
_client_conn_refresh_type(client_conn_t *client_conn);

// port connection
port_conn_t *
_port_conn_add(client_conn_t *client_conn, port_t *source_port, port_t *sink_port);

void
_port_conn_free(port_conn_t *port_conn);

port_conn_t *
_port_conn_find(client_conn_t *client_conn, port_t *source_port, port_t *sink_port);

void
_port_conn_remove(app_t *app, client_conn_t *client_conn, port_t *source_port, port_t *sink_port);

// port
port_t *
_port_add(app_t *app, jack_port_t *jport);

void
_port_free(port_t *port);

void
_port_remove(app_t *app, port_t *port);

port_t *
_port_find_by_name(app_t *app, const char *port_name);

#ifdef JACK_HAS_METADATA_API
port_t *
_port_find_by_uuid(app_t *app, jack_uuid_t port_uuid);
#endif

port_t *
_port_find_by_body(app_t *app, jack_port_t *body);

// mixer
mixer_t *
_mixer_add(app_t *app, unsigned nsources, unsigned nsinks);

void
_mixer_free(mixer_t *mixer);

// monitor
void
_monitor_spawn(app_t *app, unsigned nsources);

monitor_shm_t *
_monitor_add(const char *client_name);

void
_monitor_free(monitor_shm_t *monitor);

#endif
