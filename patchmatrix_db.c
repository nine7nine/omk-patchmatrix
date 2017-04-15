/*
 * Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <patchmatrix_db.h>
#include <patchmatrix_jack.h>

// client

client_t *
_client_add(app_t *app, const char *client_name, int client_flags)
{
	client_t *client = calloc(1, sizeof(client_t));
	if(client)
	{
		char *client_uuid_str = jack_get_uuid_for_client_name(app->client, client_name);
		if(client_uuid_str)
		{
			jack_uuid_t client_uuid;
			jack_uuid_parse(client_uuid_str, &client->uuid);

			jack_free(client_uuid_str);
		}

		client->name = strdup(client_name);
		client->pretty_name = NULL; //FIXME
		client->flags = client_flags;

		const float w = 200;
		const float h = 25;
		float x;
		float *nxt;
		if(client_flags == JackPortIsOutput)
		{
			x = w/2 + 10;
			nxt = &app->nxt_source;
		}
		else if(client_flags == JackPortIsInput)
		{
			x = app->win.cfg.width - w/2 - 10;
			nxt = &app->nxt_sink;
		}
		else
		{
			x = app->win.cfg.width/2;
			nxt = &app->nxt_default;
		}

		*nxt = fmodf(*nxt + 2*h, app->win.cfg.height);

		client->pos = nk_vec2(x, *nxt);
		client->dim = nk_vec2(200.f, 25.f);
		_hash_add(&app->clients, client);
	}

	return client;
}

void
_client_free(client_t *client)
{
	HASH_FREE(&client->ports, port_ptr)
	{
		port_t *port = port_ptr;

		_port_free(port);
	}

	_hash_free(&client->sources);
	_hash_free(&client->sinks);

	free(client->name);
	free(client->pretty_name);
	free(client);
}

bool
_client_remove_cb(void *node, void *data)
{
	client_conn_t *client_conn;
	client_t *client = data;

	if(  (client_conn->source_client == client)
		|| (client_conn->sink_client == client) )
	{
		_client_conn_free(client_conn);
		return false;
	}

	return true;
}

void
_client_remove(app_t *app, client_t *client)
{
	_hash_remove(&app->clients, client);
	_hash_remove_cb(&app->conns, _client_remove_cb, client);
}

client_t *
_client_find_by_name(app_t *app, const char *client_name, int client_flags)
{
	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		if(!strcmp(client->name, client_name) && (client->flags == client_flags))
		{
			return client;
		}
	}

	return NULL;
}

client_t *
_client_find_by_uuid(app_t *app, jack_uuid_t client_uuid, int client_flags)
{
	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		if(!jack_uuid_compare(client->uuid, client_uuid) && (client->flags == client_flags))
		{
			return client;
		}
	}

	return NULL;
}

port_t *
_client_find_port_by_name(client_t *client, const char *port_name)
{
	HASH_FOREACH(&client->ports, port_itr)
	{
		port_t *port = *port_itr;

		if(!strcmp(port->name, port_name))
		{
			return port;
		}
	}

	return NULL;
}

void
_client_refresh_type(client_t *client)
{
	client->source_type = TYPE_NONE;
	client->sink_type = TYPE_NONE;

	HASH_FOREACH(&client->sources, port_itr)
	{
		port_t *port = *port_itr;

		client->source_type |= port->type;
	}

	HASH_FOREACH(&client->sinks, port_itr)
	{
		port_t *port = *port_itr;

		client->sink_type |= port->type;
	}
}

static int
_client_port_sort(const void *a, const void *b)
{
	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	if(port_a->order != port_b->order) // order according to metadata
		return port_a->order - port_b->order;

	return strcasecmp(port_a->name, port_b->name); // order according to name
}

void
_client_sort(client_t *client)
{
	_hash_sort(&client->sources, _client_port_sort);
	_hash_sort(&client->sinks, _client_port_sort);
}

// client connection

client_conn_t *
_client_conn_add(app_t *app, client_t *source_client, client_t *sink_client)
{
	client_conn_t *client_conn = calloc(1, sizeof(client_conn_t));
	if(client_conn)
	{
		client_conn->source_client = source_client;
		client_conn->sink_client = sink_client;
		client_conn->pos = nk_vec2(
			(source_client->pos.x + sink_client->pos.x)/2,
			(source_client->pos.y + sink_client->pos.y)/2);
		client_conn->type = TYPE_NONE;
		_hash_add(&app->conns, client_conn);
	}

	return client_conn;
}

void
_client_conn_free(client_conn_t *client_conn)
{
	_hash_free(&client_conn->conns);
	free(client_conn);
}

void
_client_conn_remove(app_t *app, client_conn_t *client_conn)
{
	_hash_remove(&app->conns, client_conn);
	_client_conn_free(client_conn);
}

client_conn_t *
_client_conn_find(app_t *app, client_t *source_client, client_t *sink_client)
{
	HASH_FOREACH(&app->conns, client_conn_itr)
	{
		client_conn_t *client_conn = *client_conn_itr;

		if(  (client_conn->source_client == source_client)
			&& (client_conn->sink_client == sink_client) )
		{
			return client_conn;
		}
	}

	return NULL;
}

void
_client_conn_refresh_type(client_conn_t *client_conn)
{
	client_conn->type = TYPE_NONE;

	HASH_FOREACH(&client_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		client_conn->type |= port_conn->source_port->type;
		client_conn->type |= port_conn->sink_port->type;
	}
}

// port connection

port_conn_t *
_port_conn_add(client_conn_t *client_conn, port_t *source_port, port_t *sink_port)
{
	port_conn_t *port_conn = calloc(1, sizeof(port_conn_t));
	if(port_conn)
	{
		port_conn->source_port = source_port;
		port_conn->sink_port = sink_port;
		_hash_add(&client_conn->conns, port_conn);

		client_conn->type |= source_port->type | sink_port->type;
	}

	return port_conn;
}

void
_port_conn_free(port_conn_t *port_conn)
{
	free(port_conn);
}

port_conn_t *
_port_conn_find(client_conn_t *client_conn, port_t *source_port, port_t *sink_port)
{
	HASH_FOREACH(&client_conn->conns, port_conn_itr)
	{
		port_conn_t *port_conn = *port_conn_itr;

		if(  (port_conn->source_port == source_port)
			&& (port_conn->sink_port == sink_port) )
		{
			return port_conn;
		}
	}

	return NULL;
}

static bool
_port_conn_remove_cb(void *node, void *data)
{
	port_conn_t *dst = node;
	port_conn_t *ref = data;

	if(  (dst->source_port == ref->source_port)
		&& (dst->sink_port == ref->sink_port) )
	{
		free(dst);
		return false;
	}

	return true;
}

void
_port_conn_remove(client_conn_t *client_conn, port_t *source_port, port_t *sink_port)
{
	port_conn_t port_conn = {
		.source_port = source_port,
		.sink_port = sink_port
	};

	_hash_remove_cb(&client_conn->conns, _port_conn_remove_cb, &port_conn);
	_client_conn_refresh_type(client_conn);
}

// port

port_t *
_port_add(client_t *client, jack_uuid_t port_uuid,
	const char *port_name, const char *port_short_name, port_type_t port_type,
	bool is_input)
{
	port_t *port = calloc(1, sizeof(port_t));
	if(port)
	{
		port->client = client;
		port->uuid = port_uuid;
		port->name = strdup(port_name);
		port->short_name = strdup(port_short_name);
		port->pretty_name = NULL; //FIXME
		port->type = port_type;
		port->designation = DESIGNATION_NONE;
		_hash_add(&client->ports, port);
		if(is_input)
			_hash_add(&client->sinks, port);
		else
			_hash_add(&client->sources, port);
		_client_sort(client);

		if(is_input)
			client->sink_type |= port->type;
		else
			client->source_type |= port->type;
	}

	return port;
}

void
_port_free(port_t *port)
{
	free(port->name);
	free(port->short_name);
	free(port->pretty_name);
	free(port);
}

static bool
_port_remove_cb_cb(void *node, void *data)
{
	port_conn_t *port_conn = node;
	port_t *port = data;

	if(  (port_conn->source_port == port)
		|| (port_conn->sink_port == port) )
	{
		_port_conn_free(port_conn);
		return false;
	}

	return true;
}

static bool
_port_remove_cb(void *node, void *data)
{
	client_conn_t *client_conn = node;
	port_t *port = data;

	if(  (client_conn->source_client == port->client)
		|| (client_conn->sink_client == port->client) )
	{
		_hash_remove_cb(&client_conn->conns, _port_remove_cb_cb, port);
	}

	// free when empty
	if(_hash_size(&client_conn->conns) == 0)
	{
		_client_conn_free(client_conn);
		return false;
	}

	return true;
}

void
_port_remove(app_t *app, port_t *port)
{
	client_t *client = port->client;

	_hash_remove(&client->ports, port);
	_hash_remove(&client->sinks, port);
	_hash_remove(&client->sources, port);
	_hash_remove_cb(&app->conns, _port_remove_cb, port);
	_client_refresh_type(client);
}

port_t *
_port_find_by_name(app_t *app, const char *port_name)
{
	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		HASH_FOREACH(&client->ports, port_itr)
		{
			port_t *port = *port_itr;

			if(!strcmp(port->name, port_name))
			{
				return port;
			}
		}
	}

	return NULL;
}

port_t *
_port_find_by_uuid(app_t *app, jack_uuid_t port_uuid)
{
	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		HASH_FOREACH(&client->ports, port_itr)
		{
			port_t *port = *port_itr;

			if(!jack_uuid_compare(port->uuid, port_uuid))
			{
				return port;
			}
		}
	}

	return NULL;
}

// mixer

mixer_t *
_mixer_add(app_t *app, unsigned nsources, unsigned nsinks)
{
	mixer_t *mixer = calloc(1, sizeof(mixer_t));
	if(mixer)
	{
		mixer->nsources = nsources;
		mixer->nsinks = nsinks;

		for(unsigned j = 0; j < nsinks; j++)
		{
			for(unsigned i = 0; i < nsources; i++)
			{
				atomic_init(&mixer->jgains[i][j], (i == j) ? 0 : -36);
			}
		}

		const jack_options_t opts = JackNullOption;
		jack_status_t status;
		mixer->client = jack_client_open("mixer", opts, &status);

		for(unsigned j = 0; j < nsinks; j++)
		{
			char name [32];
			snprintf(name, 32, "sink_%u", j);
			jack_port_t *jsink = jack_port_register(mixer->client, name,
				JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
			mixer->jsinks[j] = jsink;
		}

		for(unsigned i = 0; i < nsources; i++)
		{
			char name [32];
			snprintf(name, 32, "source_%u", i);
			jack_port_t *jsource = jack_port_register(mixer->client, name,
				JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			mixer->jsources[i] = jsource;
		}

		jack_set_process_callback(mixer->client, _audio_mixer_process, mixer);
		jack_activate(mixer->client);
		_hash_add(&app->mixers, mixer);

		const char *client_name = jack_get_client_name(mixer->client);
		client_t *client = _client_add(app, client_name, JackPortIsInput | JackPortIsOutput);
		if(client)
			client->mixer = mixer;
	}

	return mixer;
}

void
_mixer_free(mixer_t *mixer)
{
	if(mixer->client)
	{
		jack_deactivate(mixer->client);

		for(unsigned j = 0; j < PORT_MAX; j++)
		{
			jack_port_t *jsink = mixer->jsinks[j];
			jack_port_t *jsource = mixer->jsources[j];

			if(jsink)
				jack_port_unregister(mixer->client, jsink);
			if(jsource)
				jack_port_unregister(mixer->client, jsource);
		}

		jack_client_close(mixer->client);
	}

	free(mixer);
}

void
_mixer_remove(app_t *app, mixer_t *mixer)
{
	_hash_remove(&app->mixers, mixer);
}
