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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <patchmatrix_db.h>
#include <patchmatrix_jack.h>

// client
#ifdef JACK_HAS_METADATA_API
static void
_client_get_or_set_pos_x(app_t *app, client_t *client, const char *property)
{
	char *value = NULL;
	char *type = NULL;
	jack_get_property(client->uuid, property, &value, &type);
	if(value)
	{
		client->pos.x = atof(value);
		jack_free(value);
	}
	else // set, if not already set
	{
		char val [32];
		snprintf(val, 32, "%f", client->pos.x);
		jack_set_property(app->client, client->uuid, property, val, XSD__float);
	}
	if(type)
		jack_free(type);
}

static void
_client_get_or_set_pos_y(app_t *app, client_t *client, const char *property)
{
	char *value = NULL;
	char *type = NULL;
	jack_get_property(client->uuid, property, &value, &type);
	if(value)
	{
		client->pos.y = atof(value);
		jack_free(value);
	}
	else // set, if not already set
	{
		char val [32];
		snprintf(val, 32, "%f", client->pos.y);
		jack_set_property(app->client, client->uuid, property, val, XSD__float);
	}
	if(type)
		jack_free(type);
}
#endif

client_t *
_client_add(app_t *app, const char *client_name, int client_flags)
{
	client_t *client = calloc(1, sizeof(client_t));
	if(client)
	{
		client->name = strdup(client_name);
		client->pretty_name = NULL;
		client->flags = client_flags;

		const float w = 200.f * app->scale;
		const float h = 25.f * app->scale;
		float x;
		float *nxt;
		if(client->flags == JackPortIsOutput)
		{
			x = w/2 + 10;
			nxt = &app->nxt_source;
		}
		else if(client->flags == JackPortIsInput)
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
		client->dim = nk_vec2(w, h);

		char *client_uuid_str = jack_get_uuid_for_client_name(app->client, client_name);
		if(client_uuid_str)
		{
			jack_uuid_parse(client_uuid_str, &client->uuid);

			jack_free(client_uuid_str);
		}

		if(app->root)
		{
			cJSON *clients_node = cJSON_GetObjectItem(app->root, "clients");
			if(clients_node && cJSON_IsArray(clients_node))
			{
				cJSON *client_node;
				cJSON_ArrayForEach(client_node, clients_node)
				{
					cJSON *uuid_node = cJSON_GetObjectItem(client_node, "client_uuid");
					if(uuid_node && cJSON_IsString(uuid_node))
					{
						jack_uuid_t client_uuid;
						jack_uuid_parse(uuid_node->valuestring, &client_uuid);

						int flags = 0;

						cJSON *sinks_node = cJSON_GetObjectItem(client_node, "sinks");
						if(sinks_node && (sinks_node->type == cJSON_True) )
							flags |= JackPortIsInput;

						cJSON *sources_node = cJSON_GetObjectItem(client_node, "sources");
						if(sources_node && (sources_node->type == cJSON_True) )
							flags |= JackPortIsOutput;

						if(!jack_uuid_compare(client_uuid, client->uuid) && (client->flags == flags) )
						{
							cJSON *xpos_node = cJSON_GetObjectItem(client_node, "xpos");
							if(xpos_node && cJSON_IsNumber(xpos_node))
								client->pos.x = xpos_node->valuedouble;

							cJSON *ypos_node = cJSON_GetObjectItem(client_node, "ypos");
							if(ypos_node && cJSON_IsNumber(ypos_node))
								client->pos.y = ypos_node->valuedouble;

							break; // exit loop
						}
					}
				}
			}
		}

#ifdef JACK_HAS_METADATA_API
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(client->uuid, JACK_METADATA_PRETTY_NAME, &value, &type);
			if(value)
			{
				client->pretty_name = strdup(value);
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}

		if(client->flags == (JackPortIsInput | JackPortIsOutput) )
		{
			_client_get_or_set_pos_x(app, client, PATCHMATRIX__mainPositionX);
			_client_get_or_set_pos_y(app, client, PATCHMATRIX__mainPositionY);
		}
		else if(client->flags == JackPortIsInput)
		{
			_client_get_or_set_pos_x(app, client, PATCHMATRIX__sinkPositionX);
			_client_get_or_set_pos_y(app, client, PATCHMATRIX__sinkPositionY);
		}
		else if(client->flags == JackPortIsOutput)
		{
			_client_get_or_set_pos_x(app, client, PATCHMATRIX__sourcePositionX);
			_client_get_or_set_pos_y(app, client, PATCHMATRIX__sourcePositionY);
		}
#endif

		if(!strncmp(client_name, PATCHMATRIX_MONITOR_ID, strlen(PATCHMATRIX_MONITOR_ID)))
			client->monitor_shm = _monitor_add(client_name);
		else if(!strncmp(client_name, PATCHMATRIX_MIXER_ID, strlen(PATCHMATRIX_MIXER_ID)))
			client->mixer_shm = _mixer_add(client_name);

		_hash_add(&app->clients, client);
	}

	return client;
}

void
_client_free(app_t *app, client_t *client)
{
	HASH_FREE(&client->ports, port_ptr)
	{
		port_t *port = port_ptr;

		_port_free(port);
	}

	_hash_free(&client->sources);
	_hash_free(&client->sinks);

	if(client->mixer_shm)
		_mixer_free(client->mixer_shm);
	else if(client->monitor_shm)
		_monitor_free(client->monitor_shm);

	free(client->name);
	free(client->pretty_name);
	free(client);
}

bool
_client_remove_cb(void *node, void *data)
{
	client_conn_t *client_conn = node;
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

#ifdef JACK_HAS_METADATA_API
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
#endif

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
strcasenumcmp(const char *s1, const char *s2)
{
	static const char *digits = "1234567890";
	const char *d1 = strpbrk(s1, digits);
	const char *d2 = strpbrk(s2, digits);

	// do both s1 and s2 contain digits?
	if(d1 && d2)
	{
		const size_t l1 = d1 - s1;
		const size_t l2 = d2 - s2;

		// do both s1 and s2 match up to the first digit?
		if( (l1 == l2) && (strncmp(s1, s2, l1) == 0) )
		{
			char *e1 = NULL;
			char *e2 = NULL;

			const int n1 = strtol(d1, &e1, 10);
			const int n2 = strtol(d2, &e2, 10);

			// do both d1 and d2 contain a valid number?
			if(e1 && e2)
			{
				// are the numbers equal? do the same for the substring
				if(n1 == n2)
				{
					return strcasenumcmp(e1, e2);
				}

				// the numbers differ, e.g. return their ordering
				return (n1 < n2) ? -1 : 1;
			}
		}
	}

	// no digits in either s1 or s2, do normal comparison
	return strcasecmp(s1, s2);
}

static int
_client_port_sort(const void *a, const void *b)
{
	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	if(port_a->order != port_b->order) // order according to metadata
		return port_a->order - port_b->order;

	return strcasenumcmp(port_a->name, port_b->name); // order according to name
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

		if(app->root)
		{
			cJSON *conns_node = cJSON_GetObjectItem(app->root, "conns");
			if(conns_node && cJSON_IsArray(conns_node))
			{
				cJSON *conn_node;
				cJSON_ArrayForEach(conn_node, conns_node)
				{
					cJSON *source_uuid_node = cJSON_GetObjectItem(conn_node, "source_uuid");
					cJSON *sink_uuid_node = cJSON_GetObjectItem(conn_node, "sink_uuid");
					if(  source_uuid_node && cJSON_IsString(source_uuid_node)
						&& sink_uuid_node && cJSON_IsString(sink_uuid_node) )
					{
						jack_uuid_t source_uuid;
						jack_uuid_t sink_uuid;
						jack_uuid_parse(source_uuid_node->valuestring, &source_uuid);
						jack_uuid_parse(sink_uuid_node->valuestring, &sink_uuid);

						if(  !jack_uuid_compare(source_uuid, client_conn->source_client->uuid)
							&& !jack_uuid_compare(sink_uuid, client_conn->sink_client->uuid) )
						{
							cJSON *xpos_node = cJSON_GetObjectItem(conn_node, "xpos");
							if(xpos_node && cJSON_IsNumber(xpos_node))
								client_conn->pos.x = xpos_node->valuedouble;

							cJSON *ypos_node = cJSON_GetObjectItem(conn_node, "ypos");
							if(ypos_node && cJSON_IsNumber(ypos_node))
								client_conn->pos.y = ypos_node->valuedouble;

							break; // exit loop
						}
					}
				}
			}
		}

		_hash_add(&app->conns, client_conn);
	}

	return client_conn;
}

void
_client_conn_free(client_conn_t *client_conn)
{
	HASH_FREE(&client_conn->conns, port_conn_ptr)
	{
		port_conn_t *port_conn = port_conn_ptr;

		_port_conn_free(port_conn);
	}

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

client_conn_t *
_client_conn_find_or_add(app_t *app, client_t *source_client, client_t *sink_client)
{
	client_conn_t *client_conn = _client_conn_find(app, source_client, sink_client);
	if(!client_conn)
		client_conn = _client_conn_add(app, source_client, sink_client);

	return client_conn;
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
		_client_conn_refresh_type(client_conn);
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
_port_conn_remove(app_t *app, client_conn_t *client_conn, port_t *source_port, port_t *sink_port)
{
	port_conn_t port_conn = {
		.source_port = source_port,
		.sink_port = sink_port
	};

	_hash_remove_cb(&client_conn->conns, _port_conn_remove_cb, &port_conn);
	_client_conn_refresh_type(client_conn);

	if(_hash_size(&client_conn->conns) == 0)
		_client_conn_remove(app, client_conn);
}

// port

port_t *
_port_add(app_t *app, jack_port_t *jport)
{
	const int port_flags = jack_port_flags(jport);
	const bool is_physical = port_flags & JackPortIsPhysical;
	const bool is_input = port_flags & JackPortIsInput;
	const int client_flags = is_physical
		? (is_input ? JackPortIsInput : JackPortIsOutput)
		: JackPortIsInput | JackPortIsOutput;
	const port_type_t port_type = !strcmp(jack_port_type(jport), JACK_DEFAULT_AUDIO_TYPE)
		? TYPE_AUDIO
		: TYPE_MIDI;

	const char *port_name = jack_port_name(jport);
	char *sep = strchr(port_name, ':');
	if(!sep)
		return NULL;

	const char *port_short_name = sep + 1;
	char *client_name = strndup(port_name, sep - port_name);
	if(!client_name)
		return NULL;

	client_t *client = _client_find_by_name(app, client_name, client_flags);
	if(!client)
		client = _client_add(app, client_name, client_flags);
	free(client_name);
	if(!client)
		return NULL;

	port_t *port = calloc(1, sizeof(port_t));
	if(port)
	{
		port->body = jport;
		port->client = client;
		port->uuid = jack_port_uuid(jport);
		port->name = strdup(port_name);
		port->short_name = strdup(port_short_name);
		port->type = port_type;
		port->designation = DESIGNATION_NONE;

#ifdef JACK_HAS_METADATA_API
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(port->uuid, JACKEY_SIGNAL_TYPE, &value, &type);
			if(value)
			{
				if(!strcasecmp(value, port_labels[TYPE_CV]))
					port->type = TYPE_CV;
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(port->uuid, JACKEY_EVENT_TYPES, &value, &type);
			if(value)
			{
				if(strcasestr(value, port_labels[TYPE_OSC]))
					port->type = TYPE_OSC; //FIXME |=
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(port->uuid, JACKEY_ORDER, &value, &type);
			if(value)
			{
				port->order = atoi(value);
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(port->uuid, JACKEY_DESIGNATION, &value, &type);
			if(value)
			{
				port->designation = _designation_get(value);
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}
		{
			char *value = NULL;
			char *type = NULL;
			jack_get_property(port->uuid, JACK_METADATA_PRETTY_NAME, &value, &type);
			if(value)
			{
				port->pretty_name = strdup(value);
				jack_free(value);
			}
			if(type)
				jack_free(type);
		}
#endif

		_hash_add(&client->ports, port);
		if(is_input)
			_hash_add(&client->sinks, port);
		else
			_hash_add(&client->sources, port);
		_client_sort(client);
		_client_refresh_type(client);
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
		_client_conn_refresh_type(client_conn);
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

#ifdef JACK_HAS_METADATA_API
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
#endif

port_t *
_port_find_by_body(app_t *app, jack_port_t *body)
{
	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		HASH_FOREACH(&client->ports, port_itr)
		{
			port_t *port = *port_itr;

			if(port->body == body)
			{
				return port;
			}
		}
	}

	return NULL;
}

// mixer
void
_mixer_spawn(app_t *app, unsigned nsinks, unsigned nsources)
{
	pthread_t pid = fork();
	if(pid == 0) // child
	{
		char sink_nums[32];
		snprintf(sink_nums, 32, "%u", nsources);

		char source_nums [32];
		snprintf(source_nums, 32, "%u", nsources);

		char *const argv [] = {
			PATCHMATRIX_MIXER,
			"-t",
			(char *)_port_type_to_string(app->type),
			"-i",
			sink_nums,
			"-o",
			source_nums,
			app->server_name ? "-n" : NULL,
			(char *)app->server_name,
			NULL
		};

		execvp(argv[0], argv);
		exit(-1);
	}
}

mixer_shm_t *
_mixer_add(const char *client_name)
{
	const size_t total_size = sizeof(mixer_shm_t);

	const int fd = shm_open(client_name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd == -1)
		return NULL;

	mixer_shm_t *mixer_shm;
	if(  (ftruncate(fd, total_size) == -1)
		|| ((mixer_shm = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) )
	{
		close(fd);
		return NULL;
	}
	close(fd);

	return mixer_shm;
}

void
_mixer_free(mixer_shm_t *mixer_shm)
{
	const size_t total_size = sizeof(mixer_shm_t);

	munmap(mixer_shm, total_size);
}

// monitor
void
_monitor_spawn(app_t *app, unsigned nsinks)
{
	pthread_t pid = fork();
	if(pid == 0) // child
	{
		char sink_nums [32];
		snprintf(sink_nums, 32, "%u", nsinks);

		char *const argv [] = {
			PATCHMATRIX_MONITOR,
			"-t",
			(char *)_port_type_to_string(app->type),
			"-i",
			sink_nums,
			app->server_name ? "-n" : NULL,
			(char *)app->server_name,
			NULL
		};

		execvp(argv[0], argv);
		exit(-1);
	}
}

monitor_shm_t *
_monitor_add(const char *client_name)
{
	const size_t total_size = sizeof(monitor_shm_t);

	const int fd = shm_open(client_name, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd == -1)
		return NULL;

	monitor_shm_t *monitor_shm;
	if(  (ftruncate(fd, total_size) == -1)
		|| ((monitor_shm = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) )
	{
		close(fd);
		return NULL;
	}
	close(fd);

	return monitor_shm;
}

void
_monitor_free(monitor_shm_t *monitor_shm)
{
	const size_t total_size = sizeof(monitor_shm_t);

	munmap(monitor_shm, total_size);
}
