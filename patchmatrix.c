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

#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h> // mkdir
#include <errno.h> // mkdir
#include <signal.h>

#include <jack/jack.h>
#include <jack/session.h>
#include <jack/midiport.h>
#ifdef JACK_HAS_METADATA_API
#	include <jack/uuid.h>
#	include <jack/metadata.h>
#	include <jackey.h>
#endif

#include <lv2/lv2plug.in/ns/ext/port-groups/port-groups.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <varchunk.h>

#define NK_PUGL_IMPLEMENTATION
#include <nk_pugl/nk_pugl.h>

#define PORT_MAX 8 //FIXME

typedef enum _event_type_t event_type_t;
typedef enum _port_type_t port_type_t;
typedef enum _port_designation_t port_designation_t;

typedef struct _hash_t hash_t;
typedef struct _port_conn_t port_conn_t;
typedef struct _client_conn_t client_conn_t;
typedef struct _port_t port_t;
typedef struct _mixer_t mixer_t;
typedef struct _monitor_t monitor_t;
typedef struct _client_t client_t;
typedef struct _app_t app_t;
typedef struct _event_t event_t;

enum _event_type_t {
	EVENT_CLIENT_REGISTER,
	EVENT_PORT_REGISTER,
	EVENT_PORT_CONNECT,
	EVENT_ON_INFO_SHUTDOWN,
	EVENT_GRAPH_ORDER,
	EVENT_SESSION,
	EVENT_FREEWHEEL,
	EVENT_BUFFER_SIZE,
	EVENT_SAMPLE_RATE,
	EVENT_XRUN,
#ifdef JACK_HAS_PORT_RENAME_CALLBACK
	EVENT_PORT_RENAME,
#endif
#ifdef JACK_HAS_METADATA_API
	EVENT_PROPERTY_CHANGE,
#endif
};

enum _port_type_t {
	TYPE_NONE   = (0 << 0),
	TYPE_AUDIO	= (1 << 0),
	TYPE_MIDI		= (1 << 1),
#ifdef JACK_HAS_METADATA_API
	TYPE_OSC		= (1 << 2),
	TYPE_CV			= (1 << 3)
#endif
};

enum _port_designation_t {
	DESIGNATION_NONE	= 0,
	DESIGNATION_LEFT,
	DESIGNATION_RIGHT,
	DESIGNATION_CENTER,
	DESIGNATION_SIDE,
	DESIGNATION_CENTER_LEFT,
	DESIGNATION_CENTER_RIGHT,
	DESIGNATION_SIDE_LEFT,
	DESIGNATION_SIDE_RIGHT,
	DESIGNATION_REAR_LEFT,
	DESIGNATION_REAR_RIGHT,
	DESIGNATION_REAR_CENTER,
	DESIGNATION_LOW_FREQUENCY_EFFECTS,

	DESIGNATION_MAX
};

struct _hash_t {
	void **nodes;
	unsigned size;
};

struct _port_conn_t {
	port_t *source_port;
	port_t *sink_port;
};

struct _client_conn_t {
	client_t *source_client;
	client_t *sink_client;
	hash_t conns;
	port_type_t type;

	struct nk_vec2 pos;
	int moving;
};

struct _mixer_t {
	jack_client_t *client;
	unsigned nsinks;
	unsigned nsources;
	jack_port_t *jsinks [PORT_MAX];
	jack_port_t *jsources [PORT_MAX];
	atomic_int jgains [PORT_MAX][PORT_MAX];
};

struct _monitor_t {
	jack_client_t *client;
	unsigned nsources;
	jack_port_t *jsources [PORT_MAX];
	atomic_int jgains [PORT_MAX];
};

struct _port_t {
	client_t *client;
	jack_uuid_t uuid;
	char *name;
	char *short_name;
	char *pretty_name;
	int order;
	port_type_t type;
	port_designation_t designation;
};

struct node_linking {
	client_t *source_client;
	int active;
};

struct node_editor {
	struct nk_rect bounds;
	struct node *selected;
	struct nk_vec2 scrolling;
	struct node_linking linking;
};

struct _client_t {
	jack_uuid_t uuid;
	char *name;
	char *pretty_name;
	hash_t ports;
	hash_t sources;
	hash_t sinks;

	int flags;
	struct nk_vec2 pos;
	struct nk_vec2 dim;
	int moving;

	mixer_t *mixer;
	monitor_t *monitor; //FIXME use
	port_type_t sink_type;
	port_type_t source_type;
};

struct _event_t {
	event_type_t type;

	union {
		struct {
			char *name;
			int state;
		} client_register;

		struct {
			jack_port_id_t id;
			int state;
		} port_register;

		struct {
			jack_port_id_t id_source;
			jack_port_id_t id_sink;
			int state;
		} port_connect;

#ifdef JACK_HAS_METADATA_API
		struct {
			jack_uuid_t uuid;
			char *key;
			jack_property_change_t state;
		} property_change;
#endif

		struct {
			jack_status_t code;
			char *reason;
		} on_info_shutdown;

		struct {
			jack_session_event_t *event;
		} session;

		struct {
			int starting;
		} freewheel;

		struct {
			jack_nframes_t nframes;
		} buffer_size;

		struct {
			jack_nframes_t nframes;
		} sample_rate;

		struct {
			char *old_name;
			char *new_name;
		} port_rename;
	};
};

struct _app_t {
	// UI
	port_type_t type;
	port_designation_t designation;
	bool freewheel;
	bool realtime;
	int32_t buffer_size;
	int32_t sample_rate;
	int32_t xruns;

	// JACK
	jack_client_t *client;
#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid;
#endif

	// varchunk
	varchunk_t *from_jack;

	bool populating;
	const char *server_name;
	const char *session_id;

	bool needs_refresh;

	nk_pugl_window_t win;

	int source_n;
	int sink_n;

	float dy;

	float nxt_source;
	float nxt_sink;
	float nxt_default;
	hash_t clients;
	hash_t conns;
	hash_t mixers;

	struct node_editor nodedit;

	struct {
		struct nk_image audio;
		struct nk_image midi;
#ifdef JACK_HAS_METADATA_API
		struct nk_image cv;
		struct nk_image osc;
#endif
	} icons;
};

static atomic_bool done = ATOMIC_VAR_INIT(false);
static void _ui_signal(app_t *app);
static void _ui_refresh(app_t *app);

static const char *designations [DESIGNATION_MAX] = {
	[DESIGNATION_NONE] = NULL,
	[DESIGNATION_LEFT] = LV2_PORT_GROUPS__left,
	[DESIGNATION_RIGHT] = LV2_PORT_GROUPS__right,
	[DESIGNATION_CENTER] = LV2_PORT_GROUPS__center,
	[DESIGNATION_SIDE] = LV2_PORT_GROUPS__side,
	[DESIGNATION_CENTER_LEFT] = LV2_PORT_GROUPS__centerLeft,
	[DESIGNATION_CENTER_RIGHT] = LV2_PORT_GROUPS__centerRight,
	[DESIGNATION_SIDE_LEFT] = LV2_PORT_GROUPS__sideLeft,
	[DESIGNATION_SIDE_RIGHT] = LV2_PORT_GROUPS__sideRight,
	[DESIGNATION_REAR_LEFT] = LV2_PORT_GROUPS__rearLeft,
	[DESIGNATION_REAR_RIGHT] = LV2_PORT_GROUPS__rearRight,
	[DESIGNATION_REAR_CENTER] = LV2_PORT_GROUPS__rearCenter,
	[DESIGNATION_LOW_FREQUENCY_EFFECTS] = LV2_PORT_GROUPS__lowFrequencyEffects
};

#define LEN(x) sizeof(x)

#define HASH_FOREACH(hash, itr) \
	for(void **(itr) = (hash)->nodes; (itr) - (hash)->nodes < (hash)->size; (itr)++)

#define HASH_FREE(hash, ptr) \
	for(void *(ptr) = _hash_pop((hash)); (ptr); (ptr) = _hash_pop((hash)))

static bool
_hash_empty(hash_t *hash)
{
	return hash->size == 0;
}

static size_t
_hash_size(hash_t *hash)
{
	return hash->size;
}

static void
_hash_add(hash_t *hash, void *node)
{
	hash->nodes = realloc(hash->nodes, (hash->size + 1)*sizeof(void *)); //TODO check
	hash->nodes[hash->size] = node;
	hash->size++;
}

static void
_hash_remove(hash_t *hash, void *node)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(node_ptr != node)
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *)); //TODO check
			nodes[size] = node_ptr;
			size++;
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_remove_cb(hash_t *hash, bool (*cb)(void *node, void *data), void *data)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(cb(node_ptr, data))
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *)); //TODO check
			nodes[size] = node_ptr;
			size++;
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_free(hash_t *hash)
{
	free(hash->nodes);
	hash->nodes = NULL;
	hash->size = 0;
}

static void *
_hash_pop(hash_t *hash)
{
	if(hash->size)
	{
		void *node = hash->nodes[--hash->size];

		if(!hash->size)
			_hash_free(hash);

		return node;
	}

	return NULL;
}

static void
_hash_sort(hash_t *hash, int (*cmp)(const void *a, const void *b))
{
	if(hash->size)
		qsort(hash->nodes, hash->size, sizeof(void *), cmp);
}

static void
_hash_sort_r(hash_t *hash, int (*cmp)(const void *a, const void *b, void *data),
	void *data)
{
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), cmp, data);
}

static inline int
_designation_get(const char *uri)
{
	for(int i=1; i<DESIGNATION_MAX; i++)
	{
		if(!strcmp(uri, designations[i]))
			return i; // found a match
	}

	return DESIGNATION_NONE; // found no match
}

#define COLOR_N 20
static const struct nk_color colors [COLOR_N] = {
	{ 255,  179,    0,  255 }, // Vivid Yellow
	{ 128,   62,  117,  255 }, // Strong Purple
	{ 255,  104,    0,  255 }, // Vivid Orange
	{ 166,  189,  215,  255 }, // Very Light Blue
	{ 193,    0,   32,  255 }, // Vivid Red
	{ 206,  162,   98,  255 }, // Grayish Yellow
	{ 129,  112,  102,  255 }, // Medium Gray
	{   0,  125,   52,  255 }, // Vivid Green
	{ 246,  118,  142,  255 }, // Strong Purplish Pink
	{   0,   83,  138,  255 }, // Strong Blue
	{ 255,  122,   92,  255 }, // Strong Yellowish Pink
	{  83,   55,  122,  255 }, // Strong Violet
	{ 255,  142,    0,  255 }, // Vivid Orange Yellow
	{ 179,   40,   81,  255 }, // Strong Purplish Red
	{ 244,  200,    0,  255 }, // Vivid Greenish Yellow
	{ 127,   24,   13,  255 }, // Strong Reddish Brown
	{ 147,  170,    0,  255 }, // Vivid Yellowish Green
	{  89,   51,   21,  255 }, // Deep Yellowish Brown
	{ 241,   58,   19,  255 }, // Vivid Reddish Orange
	{  35,   44,   22,  255 }  // Dark Olive Green
};

static inline struct nk_color
_color_get(int n)
{
	return colors[ (n-1) % COLOR_N];
}

static int
mkdirp(const char* path, mode_t mode)
{
	int ret = 0;

	// const cast for hack
	char *p = strdup(path);
	if(!p)
		return -1;

	char cwd [1024];
	getcwd(cwd, 1024);

	chdir("/");

	const char *pattern = "/";
	for(char *sub = strtok(p, pattern); sub; sub = strtok(NULL, pattern))
	{
		mkdir(sub, mode);
		chdir(sub);
	}

	chdir(cwd);

	free(p);

	return ret;
}

static port_t *
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

static port_t *
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

static port_conn_t *
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

static client_t *
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

static client_t *
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

static port_t *
_client_port_find_by_name(client_t *client, const char *port_name)
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

static void
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

static void
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

static client_conn_t *
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

static client_conn_t *
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

static port_conn_t *
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

static void
_port_conn_remove(client_conn_t *client_conn, port_t *source_port, port_t *sink_port)
{
	port_conn_t port_conn = {
		.source_port = source_port,
		.sink_port = sink_port
	};

	_hash_remove_cb(&client_conn->conns, _port_conn_remove_cb, &port_conn);
	_client_conn_refresh_type(client_conn);
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

static port_t *
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
		{
			_hash_add(&client->sinks, port);
			_hash_sort(&client->sinks, _client_port_sort);
		}
		else
		{
			_hash_add(&client->sources, port);
			_hash_sort(&client->sources, _client_port_sort);
		}

		if(is_input)
			client->sink_type |= port->type;
		else
			client->source_type |= port->type;
	}

	return port;
}

static void
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
		free(port_conn);
		return false;
	}

	return true;
}

static void
_client_conn_free(client_conn_t *client_conn)
{
	_hash_free(&client_conn->conns);
	free(client_conn);
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
		_client_conn_free(client_conn);

	return true;
}

static void
_port_remove(app_t *app, port_t *port)
{
	client_t *client = port->client;

	_hash_remove(&client->ports, port);
	_hash_remove(&client->sinks, port);
	_hash_remove(&client->sources, port);
	_hash_remove_cb(&app->conns, _port_remove_cb, port);
	_client_refresh_type(client);
}

static client_t *
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

static void
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

static bool
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

static void
_client_remove(app_t *app, client_t *client)
{
	_hash_remove(&app->clients, client);
	_hash_remove_cb(&app->conns, _client_remove_cb, client);
}

static int
_audio_monitor_process(jack_nframes_t nframes, void *arg)
{
	monitor_t *monitor = arg;

	const float *psources [PORT_MAX];

	const unsigned nsources = monitor->nsources;

	for(unsigned i = 0; i < nsources; i++)
	{
		jack_port_t *jsource = monitor->jsources[i];
		psources[i] = jack_port_get_buffer(jsource, nframes);

		float peak = 0.f;
		for(unsigned k = 0; k < nframes; k++)
		{
			const float sample = fabsf(psources[i][k]);
			if(sample > peak)
				peak = sample;
		}

		const int32_t dBFS = 20.f*log10f(peak); //FIXME use dBFS+6 instead
		atomic_store_explicit(&monitor->jgains[i], dBFS, memory_order_relaxed);
	}

	return 0;
}

static int
_audio_mixer_process(jack_nframes_t nframes, void *arg)
{
	mixer_t *mixer = arg;

	float *psinks [PORT_MAX];
	const float *psources [PORT_MAX];

	const unsigned nsources = mixer->nsources;
	const unsigned nsinks = mixer->nsinks;

	for(unsigned i = 0; i < nsources; i++)
	{
		jack_port_t *jsource = mixer->jsources[i];
		psources[i] = jack_port_get_buffer(jsource, nframes);
	}

	for(unsigned j = 0; j < nsinks; j++)
	{
		jack_port_t *jsink = mixer->jsinks[j];
		psinks[j] = jack_port_get_buffer(jsink, nframes);

		// clear
		for(unsigned k = 0; k < nframes; k++)
		{
			psinks[j][k] = 0.f;
		}
	}

	for(unsigned j = 0; j < nsources; j++)
	{
		for(unsigned i = 0; i < nsinks; i++)
		{
			const int32_t jgain = atomic_load_explicit(&mixer->jgains[i][j], memory_order_relaxed);
			if(jgain == 0) // just add
			{
				for(unsigned k = 0; k < nframes; k++)
				{
					psinks[j][k] += psources[i][k];
				}
			}
			else if(jgain > -36) // multiply-add
			{
				const float gain = exp10f(jgain/20.f); // jgain = 20.f*log10f(gain);

				for(unsigned k = 0; k < nframes; k++)
				{
					psinks[j][k] += gain * psources[i][k];
				}
			}
			// else connection not to be mixed
		}
	}

	return 0;
}

static int
_midi_mixer_process(jack_nframes_t nframes, void *arg)
{
	mixer_t *mixer = arg;

	void *psinks [PORT_MAX];
	void *psources [PORT_MAX];

	const unsigned nsources = mixer->nsources;
	const unsigned nsinks = mixer->nsinks;

	int count [PORT_MAX];
	int pos [PORT_MAX];

	for(unsigned i = 0; i < nsources; i++)
	{
		jack_port_t *jsource = mixer->jsources[i];
		psources[i] = jack_port_get_buffer(jsource, nframes);

		count[i] = jack_midi_get_event_count(psources[i]);
		pos[i] = 0;
	}

	for(unsigned j = 0; j < nsinks; j++)
	{
		jack_port_t *jsink = mixer->jsinks[j];
		psinks[j] = jack_port_get_buffer(jsink, nframes);

		// clear
		jack_midi_clear_buffer(psinks[j]);
	}

	while(true)
	{
		uint32_t T = UINT32_MAX;
		int J = -1;

		for(unsigned j = 0; j < nsources; j++)
		{
			if(pos[j] >= count[j]) // no more events to process on this source
				continue;

			jack_midi_event_t ev;
			jack_midi_event_get(&ev, psources[j], pos[j]);

			if(ev.time <= T)
			{
				T = ev.time;
				J = j;
			}
		}

		if(J == -1) // no more events to process from all sources
			break;

		jack_midi_event_t ev;
		jack_midi_event_get(&ev, psources[J], pos[J]);

		for(unsigned i = 0; i < nsinks; i++)
		{
			const int32_t jgain = atomic_load_explicit(&mixer->jgains[i][J], memory_order_relaxed);

			if(jgain > -36) // connection to be mixed
			{
				uint8_t *msg = jack_midi_event_reserve(psinks[i], ev.time, ev.size);
				if(!msg)
					continue;

				memcpy(msg, ev.buffer, ev.size);

				if( (jgain != 0) && (ev.size == 3) ) // multiply-add
				{
					const uint8_t cmd = msg[0] & 0xf0;
					if( (cmd == 0x90) || (cmd == 0x80) ) // noteOn or noteOff
					{
						const float gain = exp10f(jgain/20.f); // jgain = 20*log10(gain/1);

						const float vel = msg[2] * gain; // velocity
						msg[2] = vel < 0 ? 0 : (vel > 0x7f ? 0x7f : vel);
					}
				}
			}
			// else connection not to be mixed
		}

		pos[J] += 1; // advance event pointer from this source
	}

	return 0;
}

static mixer_t *
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

static void
_mixer_remove(app_t *app, mixer_t *mixer)
{
	_hash_remove(&app->mixers, mixer);
}

static void
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

static bool
_jack_anim(app_t *app)
{
	bool refresh = false;
	bool realize = false;
	bool quit = false;

	const event_t *ev;
	size_t len;
	while((ev = varchunk_read_request(app->from_jack, &len)))
	{
		switch(ev->type)
		{
			case EVENT_CLIENT_REGISTER:
			{
				if(ev->client_register.state)
				{
					// we create clients upon first port registering
				}
				else
				{
					client_t *client = _client_find_by_name(app, ev->client_register.name, JackPortIsInput);
					if(client)
					{
						_client_remove(app, client);
						_client_free(client);
					}
					client = _client_find_by_name(app, ev->client_register.name, JackPortIsOutput);
					if(client)
					{
						_client_remove(app, client);
						_client_free(client);
					}
				}

				if(ev->client_register.name)
					free(ev->client_register.name); // strdup

				refresh = true;
			} break;

			case EVENT_PORT_REGISTER:
			{
				const jack_port_t *jport = jack_port_by_id(app->client, ev->port_register.id);
				if(jport)
				{
					const char *port_name = jack_port_name(jport);
					char *sep = strchr(port_name, ':');
					char *client_name = strndup(port_name, sep - port_name);
					const char *port_short_name = sep + 1;

					const int port_flags = jack_port_flags(jport);
					const bool is_physical = port_flags & JackPortIsPhysical;
					const bool is_input = port_flags & JackPortIsInput;
					const int client_flags = is_physical
						? (is_input ? JackPortIsInput : JackPortIsOutput)
						: JackPortIsInput | JackPortIsOutput;

					if(client_name)
					{
						client_t *client = _client_find_by_name(app, client_name, client_flags);
						if(!client)
							client = _client_add(app, client_name, client_flags);
						if(client)
						{
							if(ev->port_register.state)
							{
								port_t *port = _port_find_by_name(app, port_name); // e.g. MPlayer reregisters ports without first unregistering them
								if(!port)
								{
									jack_uuid_t port_uuid = jack_port_uuid(jport);
									port_type_t port_type = !strcmp(jack_port_type(jport), JACK_DEFAULT_AUDIO_TYPE)
										? TYPE_AUDIO
										: TYPE_MIDI;

									port = _port_add(client, port_uuid, port_name, port_short_name, port_type, is_input);
								}
							}
							else
							{
								port_t *port = _port_find_by_name(app, port_name);
								if(port)
								{
									_port_remove(app, port);
									_port_free(port);
								}
							}
						}

						free(client_name); // strdup
					}
				}

				refresh = true;
			} break;

			case EVENT_PORT_CONNECT:
			{
				const jack_port_t *jport_source = jack_port_by_id(app->client, ev->port_connect.id_source);
				const jack_port_t *jport_sink = jack_port_by_id(app->client, ev->port_connect.id_sink);
				if(jport_source && jport_sink)
				{
					const char *name_source = jack_port_name(jport_source);
					const char *name_sink = jack_port_name(jport_sink);

					port_t *port_source = _port_find_by_name(app, name_source);
					port_t *port_sink = _port_find_by_name(app, name_sink);

					client_t *client_source = port_source->client;
					client_t *client_sink = port_sink->client;

					const char *port_type = jack_port_type(jport_source);

					if(port_source && port_sink && client_source && client_sink)
					{
						client_conn_t *client_conn = _client_conn_find(app, client_source, client_sink);
						if(!client_conn)
							client_conn = _client_conn_add(app, client_source, client_sink);
						if(client_conn)
						{
							if(!strcmp(port_type, JACK_DEFAULT_AUDIO_TYPE))
								client_conn->type |= TYPE_AUDIO;
							else if(!strcmp(port_type, JACK_DEFAULT_MIDI_TYPE))
								client_conn->type |= TYPE_MIDI;
							//FIXME more types

							if(ev->port_connect.state)
								_port_conn_add(client_conn, port_source, port_sink);
							else
								_port_conn_remove(client_conn, port_source, port_sink);
						}
					}
				}

				refresh = true;
			} break;

#ifdef JACK_HAS_METADATA_API
			case EVENT_PROPERTY_CHANGE:
			{
				switch(ev->property_change.state)
				{
					case PropertyCreated:
					{
						// fall-through
					}
					case PropertyChanged:
					{
						char *value = NULL;
						char *type = NULL;
						if(!jack_uuid_empty(ev->property_change.uuid) && ev->property_change.key)
						{
							jack_get_property(ev->property_change.uuid,
								ev->property_change.key, &value, &type);

							if(value)
							{
								if(!strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									port_t *port = NULL;
									client_t *client = NULL;
									if((port = _port_find_by_uuid(app, ev->property_change.uuid)))
									{
										free(port->pretty_name);
										port->pretty_name = strdup(value);
									}
									else if((client = _client_find_by_uuid(app, ev->property_change.uuid, JackPortIsInput | JackPortIsOutput))) //FIXME
									{
										free(client->pretty_name);
										client->pretty_name = strdup(value);
									}
								}
								else if(!strcmp(ev->property_change.key, JACKEY_EVENT_TYPES))
								{
									port_t *port = _port_find_by_uuid(app, ev->property_change.uuid);
									if(port)
									{
										port->type = strstr(value, "OSC") ? TYPE_OSC : TYPE_MIDI;
										_client_refresh_type(port->client);
									}
								}
								else if(!strcmp(ev->property_change.key, JACKEY_SIGNAL_TYPE))
								{
									port_t *port = _port_find_by_uuid(app, ev->property_change.uuid);
									if(port)
									{
										port->type = !strcmp(value, "CV") ? TYPE_CV : TYPE_AUDIO;
										_client_refresh_type(port->client);
									}
								}
								else if(!strcmp(ev->property_change.key, JACKEY_ORDER))
								{
									port_t *port = _port_find_by_uuid(app, ev->property_change.uuid);
									if(port)
									{
										port->order = atoi(value);

										client_t *client = port->client;
										_hash_sort(&client->sources, _client_port_sort);
										_hash_sort(&client->sinks, _client_port_sort);
									}
								}
								else if(!strcmp(ev->property_change.key, JACKEY_DESIGNATION))
								{
									port_t *port = _port_find_by_uuid(app, ev->property_change.uuid);
									if(port)
									{
										port->designation = _designation_get(value);
										//FIXME do something?
									}
								}

								free(value);
							}

							if(type)
								free(type);
						}

						break;
					}
					case PropertyDeleted:
					{
						if(!jack_uuid_empty(ev->property_change.uuid))
						{
							port_t *port = NULL;
							client_t *client = NULL;

							if((port = _port_find_by_uuid(app, ev->property_change.uuid)))
							{
								bool needs_port_update = false;
								bool needs_pretty_update = false;
								bool needs_position_update = false;
								bool needs_designation_update = false;

								if(  ev->property_change.key
									&& ( !strcmp(ev->property_change.key, JACKEY_SIGNAL_TYPE)
										|| !strcmp(ev->property_change.key, JACKEY_EVENT_TYPES) ) )
								{
									needs_port_update = true;
								}
								else if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACKEY_ORDER))
								{
									needs_position_update = true;
								}
								else if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACKEY_DESIGNATION))
								{
									needs_designation_update = true;
								}
								else if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									needs_pretty_update = true;
								}
								else // all keys removed
								{
									needs_port_update = true;
									needs_pretty_update = true;
									needs_position_update = true;
									needs_designation_update = true;
								}

								if(needs_port_update)
								{
									jack_port_t *jport = jack_port_by_name(app->client, port->name);
									bool midi = 0;

									if(jport)
										midi = !strcmp(jack_port_type(jport), JACK_DEFAULT_MIDI_TYPE) ? true : false;

									port->type = midi ? TYPE_MIDI : TYPE_AUDIO;
									//FIXME adjust types of client, client_conn
								}

								if(needs_pretty_update)
								{
									free(port->pretty_name);
									port->pretty_name = NULL;
								}

								if(needs_position_update)
								{
									port->order = 0;

									client_t *client2 = port->client;
									_hash_sort(&client2->sources, _client_port_sort);
									_hash_sort(&client2->sinks, _client_port_sort);
								}

								if(needs_designation_update)
								{
									port->designation = DESIGNATION_NONE;
									//FIXME do something?
								}
							}
							else if((client = _client_find_by_uuid(app, ev->property_change.uuid, JackPortIsInput | JackPortIsOutput))) //FIXME
							{
								bool needs_pretty_update = false;

								if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									needs_pretty_update = true;
								}
								else // all keys removed
								{
									needs_pretty_update = true;
								}

								if(needs_pretty_update)
								{
									free(client->pretty_name);
									client->pretty_name = NULL;
								}
							}
						}
						else
						{
							fprintf(stderr, "all properties in current JACK session deleted\n");
							//TODO
						}

						break;
					}
				}

				if(ev->property_change.key)
					free(ev->property_change.key); // strdup

				refresh = true;
			} break;
#endif

			case EVENT_ON_INFO_SHUTDOWN:
			{
				app->client = NULL; // JACK has shut down, hasn't it?

			} break;

			case EVENT_GRAPH_ORDER:
			{
				//FIXME
			} break;

			case EVENT_SESSION:
			{
				jack_session_event_t *jev = ev->session.event;

				// path may not exist yet
				mkdirp(jev->session_dir, S_IRWXU | S_IRGRP |  S_IXGRP | S_IROTH | S_IXOTH);

				asprintf(&jev->command_line, "patchmatrix -u %s ${SESSION_DIR}",
					jev->client_uuid);

				switch(jev->type)
				{
					case JackSessionSaveAndQuit:
						quit = true;
						break;
					case JackSessionSave:
						break;
					case JackSessionSaveTemplate:
						break;
				}

				jack_session_reply(app->client, jev);
				jack_session_event_free(jev);
			} break;

			case EVENT_FREEWHEEL:
			{
				app->freewheel = ev->freewheel.starting;

				realize = true;
			} break;

			case EVENT_BUFFER_SIZE:
			{
				app->buffer_size = log2(ev->buffer_size.nframes);

				realize = true;
			} break;

			case EVENT_SAMPLE_RATE:
			{
				app->sample_rate = ev->sample_rate.nframes;

				realize = true;
			} break;

			case EVENT_XRUN:
			{
				app->xruns += 1;

				realize = true;
			} break;

#ifdef JACK_HAS_PORT_RENAME_CALLBACK
			case EVENT_PORT_RENAME:
			{
				port_t *port = _port_find_by_name(app, ev->port_rename.old_name);
				if(port)
				{
					free(port->name);
					free(port->short_name);

					const char *name = ev->port_rename.new_name;
					char *sep = strchr(name, ':');
					char *client_name = strndup(name, sep - name);
					const char *short_name = sep + 1;

					if(client_name)
					{
						client_t *client = _client_find_by_name(app, client_name, JackPortIsInput | JackPortIsOutput); //FIXME
						if(client)
						{
							_hash_sort(&client->sources, _client_port_sort);
							_hash_sort(&client->sinks, _client_port_sort);
						}

						free(client_name); // strdup
					}
				}

				if(ev->port_rename.old_name)
					free(ev->port_rename.old_name);
				if(ev->port_rename.new_name)
					free(ev->port_rename.new_name);

				refresh = true;
			} break;
#endif
		};

		varchunk_read_advance(app->from_jack);
	}

	if(refresh)
		app->needs_refresh = true;
	if(realize)
		nk_pugl_post_redisplay(&app->win);

	return quit;
}

static void
_jack_on_info_shutdown_cb(jack_status_t code, const char *reason, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_ON_INFO_SHUTDOWN;
		ev->on_info_shutdown.code = code;
		ev->on_info_shutdown.reason = strdup(reason);

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

static void
_jack_freewheel_cb(int starting, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_FREEWHEEL;
		ev->freewheel.starting = starting;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

static int
_jack_buffer_size_cb(jack_nframes_t nframes, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_BUFFER_SIZE;
		ev->buffer_size.nframes = nframes;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}

	return 0;
}

static int
_jack_sample_rate_cb(jack_nframes_t nframes, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_SAMPLE_RATE;
		ev->sample_rate.nframes = nframes;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}

	return 0;
}

static void
_jack_client_registration_cb(const char *name, int state, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_CLIENT_REGISTER;
		ev->client_register.name = strdup(name);
		ev->client_register.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

static void
_jack_port_registration_cb(jack_port_id_t id, int state, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PORT_REGISTER;
		ev->port_register.id = id;
		ev->port_register.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

#ifdef JACK_HAS_PORT_RENAME_CALLBACK
static void
_jack_port_rename_cb(jack_port_id_t id, const char *old_name, const char *new_name, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PORT_RENAME;
		ev->port_rename.old_name = strdup(old_name);
		ev->port_rename.new_name = strdup(new_name);

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}
#endif

static void
_jack_port_connect_cb(jack_port_id_t id_source, jack_port_id_t id_sink, int state, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PORT_CONNECT;
		ev->port_connect.id_source = id_source;
		ev->port_connect.id_sink = id_sink;
		ev->port_connect.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

static int
_jack_xrun_cb(void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_XRUN;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}

	return 0;
}

static int
_jack_graph_order_cb(void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_GRAPH_ORDER;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}

	return 0;
}

#ifdef JACK_HAS_METADATA_API
static void
_jack_property_change_cb(jack_uuid_t uuid, const char *key, jack_property_change_t state, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PROPERTY_CHANGE;
		ev->property_change.uuid = uuid;
		ev->property_change.key = key ? strdup(key) : NULL;
		ev->property_change.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}
#endif

// non-rt
static void
_jack_session_cb(jack_session_event_t *jev, void *arg)
{
	app_t *app = arg;

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_SESSION;
		ev->session.event = jev;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		_ui_signal(app);
	}
}

static int
_jack_init(app_t *app)
{
	jack_options_t opts = JackNullOption | JackNoStartServer;
	if(app->server_name)
		opts |= JackServerName;
	if(app->session_id)
		opts |= JackSessionID;

	jack_status_t status;
	app->client = jack_client_open("patchmatrix", opts, &status,
		app->server_name ? app->server_name : app->session_id,
		app->server_name ? app->session_id : NULL);
	if(!app->client)
		return -1;

#ifdef JACK_HAS_METADATA_API
	const char *client_name = jack_get_client_name(app->client);
	char *uuid_str = jack_get_uuid_for_client_name(app->client, client_name);
	if(uuid_str)
	{
		jack_uuid_parse(uuid_str, &app->uuid);
		free(uuid_str);
	}
	else
	{
		jack_uuid_clear(&app->uuid);
	}

	if(!jack_uuid_empty(app->uuid))
	{
		jack_set_property(app->client, app->uuid,
			JACK_METADATA_PRETTY_NAME, "PatchMatrix", "text/plain");
	}
#endif

	app->sample_rate = jack_get_sample_rate(app->client);
	app->buffer_size = log2(jack_get_buffer_size(app->client));
	app->xruns = 0;
	app->freewheel = false;
	app->realtime = jack_is_realtime(app->client);

	jack_on_info_shutdown(app->client, _jack_on_info_shutdown_cb, app);

	jack_set_freewheel_callback(app->client, _jack_freewheel_cb, app);
	jack_set_buffer_size_callback(app->client, _jack_buffer_size_cb, app);
	jack_set_sample_rate_callback(app->client, _jack_sample_rate_cb, app);

	jack_set_client_registration_callback(app->client, _jack_client_registration_cb, app);
	jack_set_port_registration_callback(app->client, _jack_port_registration_cb, app);
	jack_set_port_connect_callback(app->client, _jack_port_connect_cb, app);
	jack_set_xrun_callback(app->client, _jack_xrun_cb, app);
	jack_set_graph_order_callback(app->client, _jack_graph_order_cb, app);
	jack_set_session_callback(app->client, _jack_session_cb, app);
#ifdef JACK_HAS_PORT_RENAME_CALLBACK
	jack_set_port_rename_callback(app->client, _jack_port_rename_cb, app);
#endif
#ifdef JACK_HAS_METADATA_API
	jack_set_property_change_callback(app->client, _jack_property_change_cb, app);
#endif

	jack_activate(app->client);

	return 0;
}

static void
_jack_deinit(app_t *app)
{
	if(!app->client)
		return;

#ifdef JACK_HAS_METADATA_API
	if(!jack_uuid_empty(app->uuid))
	{
		jack_remove_properties(app->client, app->uuid);
	}
#endif

	jack_deactivate(app->client);
	jack_client_close(app->client);
}

static void
_client_moveable(struct nk_context *ctx, app_t *app, client_t *client,
	struct nk_rect *bounds)
{
	const struct nk_input *in = &ctx->input;

	/* node connector and linking */
	if(client->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_RIGHT))
		{
			client->moving = false;
		}
		else
		{
			client->pos.x += in->mouse.delta.x;
			client->pos.y += in->mouse.delta.y;
			bounds->x += in->mouse.delta.x;
			bounds->y += in->mouse.delta.y;

			// move connections together with client
			HASH_FOREACH(&app->conns, client_conn_itr)
			{
				client_conn_t *client_conn = *client_conn_itr;

				if(client_conn->source_client == client)
				{
					client_conn->pos.x += in->mouse.delta.x/2;
					client_conn->pos.y += in->mouse.delta.y/2;
				}

				if(client_conn->sink_client == client)
				{
					client_conn->pos.x += in->mouse.delta.x/2;
					client_conn->pos.y += in->mouse.delta.y/2;
				}
			}
		}
	}
	else if(nk_input_is_mouse_hovering_rect(in, *bounds))
	{
		if(nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
		{
			client->moving = 1;
		}
	}
}

static void
_client_connectors(struct nk_context *ctx, app_t *app, client_t *client,
	struct nk_vec2 dim)
{
	struct node_editor *nodedit = &app->nodedit;
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	const float cw = 4.f;

	/* output connector */
	if(client->source_type & app->type)
	{
		const float cx = client->pos.x - scrolling.x + dim.x/2 + 2*cw;
		const float cy = client->pos.y - scrolling.y;
		const struct nk_rect circle = nk_rect(
			cx - cw, cy - cw,
			2*cw, 2*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));
		if(  nk_input_is_mouse_hovering_rect(in, nk_shrink_rect(circle, -cw))
			&& !nodedit->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, nk_rgb(100, 100, 100));
		}

		/* start linking process */
		if(nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, circle, nk_true)) {
			nodedit->linking.active = nk_true;
			nodedit->linking.source_client = client;
		}

		/* draw curve from linked node slot to mouse position */
		if(  nodedit->linking.active
			&& (nodedit->linking.source_client == client) )
		{
			struct nk_vec2 m = in->mouse.pos;

			nk_stroke_line(canvas, cx, cy, m.x, m.y, 1.f, nk_rgb(200, 200, 200));
		}
	}

	/* input connector */
	if(client->sink_type & app->type)
	{
		const float cx = client->mixer
			? client->pos.x - scrolling.x
			: client->pos.x - scrolling.x - dim.x/2 - 2*cw;
		const float cy = client->mixer
			? client->pos.y - scrolling.y - dim.y/2 - 2*cw
			: client->pos.y - scrolling.y;
		const struct nk_rect circle = nk_rect(
			cx - cw, cy - cw,
			2*cw, 2*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));
		if(  nk_input_is_mouse_hovering_rect(in, nk_shrink_rect(circle, -cw))
			&& nodedit->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, nk_rgb(200, 200, 200));
		}

		if(  nk_input_is_mouse_released(in, NK_BUTTON_LEFT)
			&& nk_input_is_mouse_hovering_rect(in, circle)
			&& nodedit->linking.active)
		{
			nodedit->linking.active = nk_false;

			client_t *src = nodedit->linking.source_client;
			if(src)
			{
				client_conn_t *client_conn = _client_conn_find(app, src, client);
				if(!client_conn) // does not yet exist
					client_conn = _client_conn_add(app, src, client);
				if(client_conn)
				{
					client_conn->type |= app->type;
				}
			}
		}
	}
}

static void
node_editor_mixer(struct nk_context *ctx, app_t *app, client_t *client)
{
	if(  !(client->source_type & app->type)
		&& !(client->sink_type & app->type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	mixer_t *mixer = client->mixer;

	const float ps = 32.f;
	const unsigned nx = mixer->nsinks;
	const unsigned ny = mixer->nsources;

	client->dim.x = nx * ps;
	client->dim.y = ny * ps;

	struct nk_rect bounds = nk_rect(
		client->pos.x - client->dim.x/2 - scrolling.x,
		client->pos.y - client->dim.y/2 - scrolling.y,
		client->dim.x, client->dim.y);

	_client_moveable(ctx, app, client, &bounds);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

    const struct nk_style_item *background;
		nk_flags state = 0; //FIXME
    if(state & NK_WIDGET_STATE_HOVER)
			background = &style->hover;
		else if(state & NK_WIDGET_STATE_ACTIVED)
			background = &style->active;
		else
			background = &style->normal;

		//nk_fill_rect(canvas, body, style->rounding, background->data.color);
		nk_fill_rect(canvas, body, style->rounding, ctx->style.button.hover.data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, style->border_color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		float x = body.x + ps/2;
		for(unsigned i = 0; i < nx; i++)
		{
			float y = body.y + ps/2;
			for(unsigned j = 0; j < ny; j++)
			{
				int32_t jgain = atomic_load_explicit(&mixer->jgains[i][j], memory_order_acquire);

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);

				const struct nk_mouse_button *btn = &in->mouse.buttons[NK_BUTTON_LEFT];;
				const bool left_mouse_down = btn->down;
				const bool left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in,
					NK_BUTTON_LEFT, tile, nk_true);

				int32_t dd = 0;
				if(left_mouse_down && left_mouse_click_in_cursor)
				{
					const float dx = in->mouse.delta.x;
					const float dy = in->mouse.delta.y;
					dd = fabs(dx) > fabs(dy) ? dx : -dy;
				}
				else if(nk_input_is_mouse_hovering_rect(in, tile))
				{
					if(in->mouse.scroll_delta != 0.f) // has scrolling
					{
						dd = in->mouse.scroll_delta;
						in->mouse.scroll_delta = 0.f;
					}
				}

				if(dd != 0)
				{
					jgain = NK_CLAMP(-36, jgain + dd, 36);
					atomic_store_explicit(&mixer->jgains[i][j], jgain, memory_order_release);
				}

				if( (left_mouse_down && left_mouse_click_in_cursor) || (dd != 0) )
				{
					char tooltip [32];
					snprintf(tooltip, 32, "%+2"PRIi32" dBFS", jgain);
					nk_tooltip(ctx, tooltip);
				}

				if(jgain > -36)
				{
					const float alpha = (jgain + 36) / 72.f;
					const float beta = NK_PI/2;

					nk_stroke_arc(canvas,
						x, y, 10.f,
						beta + 0.2f*NK_PI, beta + 1.8f*NK_PI,
						1.f,
						nk_rgb(100, 100, 100));
					nk_stroke_arc(canvas,
						x, y, 7.f,
						beta + 0.2f*NK_PI, beta + (0.2f + alpha*1.6f)*NK_PI,
						2.f,
						nk_rgb(200, 200, 200));
				}

				y += ps;
			}

			x += ps;
		}
	}

	_client_connectors(ctx, app, client, nk_vec2(bounds.w, bounds.h));
}

static void
node_editor_client(struct nk_context *ctx, app_t *app, client_t *client)
{
	if(  !(client->source_type & app->type)
		&& !(client->sink_type & app->type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	const struct nk_vec2 scrolling = nodedit->scrolling;

	struct nk_rect bounds = nk_rect(
		client->pos.x - client->dim.x/2 - scrolling.x,
		client->pos.y - client->dim.y/2 - scrolling.y,
		client->dim.x, client->dim.y);

	_client_moveable(ctx, app, client, &bounds);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	nk_button_label(ctx, client->name);

	_client_connectors(ctx, app, client, nk_vec2(bounds.w, bounds.h));
}

static void
node_editor_client_conn(struct nk_context *ctx, app_t *app,
	client_conn_t *client_conn, port_type_t port_type)
{
	if(  (client_conn->type != app->type)
		&& !(client_conn->type & port_type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	client_t *src = client_conn->source_client;
	client_t *snk = client_conn->sink_client;

	if(!src || !snk)
		return;

	int nx = 0;
	HASH_FOREACH(&client_conn->source_client->sources, source_port_itr)
	{
		port_t *source_port = *source_port_itr;

		if(!(source_port->type & port_type))
			continue;

		nx += 1;
	}

	int ny = 0;
	HASH_FOREACH(&client_conn->sink_client->sinks, sink_port_itr)
	{
		port_t *sink_port = *sink_port_itr;

		if(!(sink_port->type & port_type))
			continue;

		ny += 1;
	}

	const float ps = 16.f;
	const float pw = nx * ps;
	const float ph = ny * ps;
	struct nk_rect bounds = nk_rect(
		client_conn->pos.x - scrolling.x - pw/2,
		client_conn->pos.y - scrolling.y - ph/2,
		pw, ph
	);

	int hovers = 0;
	if(client_conn->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_RIGHT))
		{
			client_conn->moving = false;
		}
		else
		{
			client_conn->pos.x += in->mouse.delta.x;
			client_conn->pos.y += in->mouse.delta.y;
			bounds.x += in->mouse.delta.x;
			bounds.y += in->mouse.delta.y;
		}
	}
	else if(nk_input_is_mouse_hovering_rect(in, bounds))
	{
		hovers = 1;

		if(nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
		{
			client_conn->moving = 1;
		}
	}
	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

    const struct nk_style_item *background;
		nk_flags state = 0; //FIXME
    if(state & NK_WIDGET_STATE_HOVER)
			background = &style->hover;
		else if(state & NK_WIDGET_STATE_ACTIVED)
			background = &style->active;
		else
			background = &style->normal;

		nk_fill_rect(canvas, body, style->rounding, background->data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, style->border_color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		float x = body.x + ps/2;
		HASH_FOREACH(&client_conn->source_client->sources, source_port_itr)
		{
			port_t *source_port = *source_port_itr;

			if(!(source_port->type & port_type))
				continue;

			float y = body.y + ps/2;
			HASH_FOREACH(&client_conn->sink_client->sinks, sink_port_itr)
			{
				port_t *sink_port = *sink_port_itr;

				if(!(sink_port->type & port_type))
					continue;

				port_conn_t *port_conn = _port_conn_find(client_conn, source_port, sink_port);

				if(port_conn)
					nk_fill_arc(canvas, x, y, 4.f, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);
				if(  nk_input_is_mouse_hovering_rect(in, tile)
					&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) )
				{
					if(port_conn)
						jack_disconnect(app->client, source_port->name, sink_port->name);
					else
						jack_connect(app->client, source_port->name, sink_port->name);
				}

				y += ps;
			}

			x += ps;
		}
	}

	const float cs = 4.f;
	const float cx = client_conn->pos.x - scrolling.x;
	const float cxr = cx + pw/2;
	const float cy = client_conn->pos.y - scrolling.y;
	const float cyl = cy - ph/2;
	const struct nk_color col = hovers || client_conn->moving
		? nk_rgb(200, 200, 200)
		: nk_rgb(100, 100, 100);

	const float l0x = src->pos.x - scrolling.x + src->dim.x/2 + cs*2;
	const float l0y = src->pos.y - scrolling.y;
	const float l1x = snk->mixer
		? snk->pos.x - scrolling.x
		: snk->pos.x - scrolling.x - snk->dim.x/2 - cs*2;
	const float l1y = snk->mixer
		? snk->pos.y - scrolling.y - snk->dim.y/2 - cs*2
		: snk->pos.y - scrolling.y;

	const float bend = 50.f;
	nk_stroke_curve(canvas,
		l0x, l0y,
		l0x + bend, l0y,
		cx, cyl - bend,
		cx, cyl,
		1.f, col);
	nk_stroke_curve(canvas,
		cxr, cy,
		cxr + bend, cy,
		snk->mixer ? l1x : l1x - bend, snk->mixer ? l1y - bend : l1y,
		l1x, l1y,
		1.f, col);

	nk_fill_arc(canvas, cx, cyl, cs, 2*M_PI/2, 4*M_PI/2, col);
	nk_fill_arc(canvas, cxr, cy, cs, 3*M_PI/2, 5*M_PI/2, col);
}

static inline void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	app_t *app = data;

	const float dy = 20.f * nk_pugl_get_scale(&app->win);
	app->dy = dy;

	int n = 0;
	const struct nk_input *in = &ctx->input;
	client_t *updated = 0;
	struct node_editor *nodedit = &app->nodedit;

	if(nk_begin(ctx, "Base", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);
		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
		const struct nk_rect total_space = nk_window_get_content_region(ctx);

		nk_menubar_begin(ctx);
		{
#ifdef JACK_HAS_METADATA_API
			nk_layout_row_dynamic(ctx, dy, 4);
#else
			nk_layout_row_dynamic(ctx, dy, 2);
#endif
			const bool is_audio = app->type == TYPE_AUDIO;
			if(is_audio)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.audio, "AUDIO", NK_TEXT_RIGHT))
				app->type = TYPE_AUDIO;
			if(is_audio)
				nk_style_pop_color(ctx);

			const bool is_midi = app->type == TYPE_MIDI;
			if(is_midi)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.midi, "MIDI", NK_TEXT_RIGHT))
				app->type = TYPE_MIDI;
			if(is_midi)
				nk_style_pop_color(ctx);

#ifdef JACK_HAS_METADATA_API
			const bool is_cv = app->type == TYPE_CV;
			if(is_cv)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.cv, "CV", NK_TEXT_RIGHT))
				app->type = TYPE_CV;
			if(is_cv)
				nk_style_pop_color(ctx);

			const bool is_osc = app->type == TYPE_OSC;
			if(is_osc)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.osc, "OSC", NK_TEXT_RIGHT))
				app->type = TYPE_OSC;
			if(is_osc)
				nk_style_pop_color(ctx);
#endif

			nk_layout_row_dynamic(ctx, dy, 4);
			if(nk_button_label(ctx, "Audio Mixer 1x1"))
			{
				_mixer_add(app, 1, 1);
			}
			if(nk_button_label(ctx, "Audio Mixer 2x2"))
			{
				_mixer_add(app, 2, 2);
			}
			if(nk_button_label(ctx, "Audio Mixer 4x4"))
			{
				_mixer_add(app, 4, 4);
			}
			if(nk_button_label(ctx, "Audio Mixer 8x8"))
			{
				_mixer_add(app, 8, 8);
			}
		}
		nk_menubar_end(ctx);

		const struct nk_vec2 scrolling = nodedit->scrolling;

		/* allocate complete window space */
		nk_layout_space_begin(ctx, NK_STATIC, total_space.h,
			_hash_size(&app->clients) + _hash_size(&app->conns));
		{
			{
				/* display grid */
				struct nk_rect ssize = nk_layout_space_bounds(ctx);
				const float grid_size = 28.0f;
				const struct nk_color grid_color = nk_rgb(50, 50, 50);

				for(float x = fmod(ssize.x - scrolling.x, grid_size);
					x < ssize.w;
					x += grid_size)
				{
					nk_stroke_line(canvas, x + ssize.x, ssize.y, x + ssize.x, ssize.y + ssize.h,
						1.0f, grid_color);
				}

				for(float y = fmod(ssize.y - scrolling.y, grid_size);
					y < ssize.h;
					y += grid_size)
				{
					nk_stroke_line(canvas, ssize.x, y + ssize.y, ssize.x + ssize.w, y + ssize.y,
						1.0f, grid_color);
				}
			}

			/* execute each node as a movable group */
			HASH_FOREACH(&app->clients, client_itr)
			{
				client_t *client = *client_itr;

				if(client->mixer)
					node_editor_mixer(ctx, app, client);
				else
					node_editor_client(ctx, app, client);
			}

			/* reset linking connection */
			if(  nodedit->linking.active
				&& nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
			{
				nodedit->linking.active = nk_false;
				fprintf(stdout, "linking failed\n");
			}

			HASH_FOREACH(&app->conns, client_conn_itr)
			{
				client_conn_t *client_conn = *client_conn_itr;

				node_editor_client_conn(ctx, app, client_conn, app->type);
			}
		}
		nk_layout_space_end(ctx);

		/* window content scrolling */
		if(  nk_input_is_mouse_hovering_rect(in, nk_window_get_bounds(ctx))
			&& nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			nodedit->scrolling.x -= in->mouse.delta.x;
			nodedit->scrolling.y -= in->mouse.delta.y;
		}
	}
	nk_end(ctx);
}

static struct nk_image
_icon_load(app_t *app, const char *path)
{
	return nk_pugl_icon_load(&app->win, path);
}

static void
_icon_unload(app_t *app, struct nk_image img)
{
	nk_pugl_icon_unload(&app->win, img);
}

static int
_ui_init(app_t *app)
{
	// UI
	nk_pugl_config_t *cfg = &app->win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "PatchMatrix";
	cfg->title = "PatchMatrix";
	cfg->parent = 0;
	cfg->data = app;
	cfg->expose = _expose;
	cfg->font.face = PATCHMATRIX_DATA_DIR"/Cousine-Regular.ttf";
	cfg->font.size = 13;

	app->type = TYPE_AUDIO;
	app->designation = DESIGNATION_NONE;

	nk_pugl_init(&app->win);
	nk_pugl_show(&app->win);

	app->icons.audio= _icon_load(app, PATCHMATRIX_DATA_DIR"audio.png");
	app->icons.midi = _icon_load(app, PATCHMATRIX_DATA_DIR"midi.png");
#ifdef JACK_HAS_METADATA_API
	app->icons.cv = _icon_load(app, PATCHMATRIX_DATA_DIR"cv.png");
	app->icons.osc = _icon_load(app, PATCHMATRIX_DATA_DIR"osc.png");
#endif

	return 0;
}

static void
_ui_deinit(app_t *app)
{
	_icon_unload(app, app->icons.audio);
	_icon_unload(app, app->icons.midi);
#ifdef JACK_HAS_METADATA_API
	_icon_unload(app, app->icons.cv);
	_icon_unload(app, app->icons.osc);
#endif

	nk_pugl_hide(&app->win);
	nk_pugl_shutdown(&app->win);
}

static void
_ui_populate(app_t *app)
{
	HASH_FREE(&app->clients, client_ptr)
	{
		client_t *client = client_ptr;

		HASH_FREE(&client->ports, port_ptr)
		{
			port_t *port = port_ptr;

			free(port->name);
			free(port->short_name);
			free(port->pretty_name);
			free(port);
		}
		_hash_free(&client->sources);
		_hash_free(&client->sinks);

		free(client->name);
		free(client->pretty_name);
		free(client);
	}

	HASH_FREE(&app->conns, client_conn_ptr)
	{
		client_conn_t *client_conn = client_conn_ptr;

		HASH_FREE(&client_conn->conns, port_conn_ptr)
		{
			port_conn_t *port_conn = port_conn_ptr;

			free(port_conn);
		}

		free(client_conn);
	}

	const char **port_names = jack_get_ports(app->client, NULL, NULL, 0);
	if(port_names)
	{
		for(const char **itr = port_names; *itr; itr++)
		{
			const char *port_name = *itr;
			const char *port_short_name = strchr(port_name, ':');

			if(!port_short_name)
				continue;

			char *client_name = strndup(port_name, port_short_name - port_name);
			if(!client_name)
				continue;

			port_short_name++;
			jack_port_t *jport = jack_port_by_name(app->client, port_name);
			if(jport)
			{
				const int port_flags = jack_port_flags(jport);
				jack_uuid_t port_uuid = jack_port_uuid(jport);

				const bool is_physical = port_flags & JackPortIsPhysical;
				const bool is_input = port_flags & JackPortIsInput;
				const int client_flags = is_physical
					? (is_input ? JackPortIsInput : JackPortIsOutput)
					: JackPortIsInput | JackPortIsOutput;

				client_t *client = _client_find_by_name(app, client_name, client_flags);
				if(!client)
					client = _client_add(app, client_name, client_flags);
				if(client)
				{
					port_t *port = _client_port_find_by_name(client, port_name);
					if(!port)
					{
						port_type_t port_type = !strcmp(jack_port_type(jport), JACK_DEFAULT_AUDIO_TYPE)
							? TYPE_AUDIO
							: TYPE_MIDI;

						port = _port_add(client, port_uuid, port_name, port_short_name,
							port_type, is_input);

#ifdef JACK_HAS_METADATA_API
						{
							char *value = NULL;
							char *type = NULL;
							jack_get_property(port->uuid, JACKEY_SIGNAL_TYPE, &value, &type);
							if(value)
							{
								if(!strcmp(value, "CV"))
									port_type = TYPE_CV;
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
								if(strstr(value, "OSC"))
									port_type = TYPE_OSC;
								jack_free(value);
							}
							if(type)
								jack_free(type);
						}
						port->type = port_type;
						_client_refresh_type(port->client);
#endif
					}
				}
			}

			free(client_name);
		}

		jack_free(port_names);
	}

	HASH_FOREACH(&app->clients, client_itr)
	{
		client_t *client = *client_itr;

		HASH_FOREACH(&client->sources, src_itr)
		{
			port_t *src_port = *src_itr;
			const char *src_name = src_port->name;

			jack_port_t *src_jport = jack_port_by_name(app->client, src_name);
			if(!src_jport)
				continue;

			const char **connections = jack_port_get_all_connections(app->client, src_jport);
			if(connections)
			{
				for(const char **snk_name_ptr = connections; *snk_name_ptr; snk_name_ptr++)
				{
					const char *snk_name = *snk_name_ptr;

					port_t *snk_port = _port_find_by_name(app, snk_name);
					client_t *src_client = src_port->client;
					client_t *snk_client = snk_port->client;

					if(snk_port && src_client && snk_client)
					{
						client_conn_t *client_conn = _client_conn_find(app, src_client, snk_client);

						if(!client_conn)
							client_conn = _client_conn_add(app, src_client, snk_client);
						if(client_conn)
						{
							client_conn->type |= src_port->type | snk_port->type;

							_port_conn_add(client_conn, src_port, snk_port);
						}
					}
				}

				jack_free(connections);
			}
		}
	}
}

static void
_ui_depopulate(app_t *app)
{
	HASH_FREE(&app->conns, client_conn_ptr)
	{
		client_conn_t *client_conn = client_conn_ptr;

		_client_conn_free(client_conn);
	}

	HASH_FREE(&app->clients, client_ptr)
	{
		client_t *client = client_ptr;

		_client_free(client);
	}

	HASH_FREE(&app->mixers, mixer_ptr)
	{
		mixer_t *mixer = mixer_ptr;

		_mixer_free(mixer);
	}
}

static void
_ui_signal(app_t *app)
{
	if(!atomic_load_explicit(&done, memory_order_acquire))
		nk_pugl_async_redisplay(&app->win);
}

static void
_ui_fill(void *data, uintptr_t source_id, uintptr_t sink_id,
	bool *state)
{
	app_t *app = data;

}

// update all grid and connections
static void
_ui_refresh(app_t *app)
{
	int ret;

	nk_pugl_post_redisplay(&app->win);
}

static app_t *app_ptr = NULL; // need a global variable to capture signals :(

static void
_sig(int signum)
{
	_ui_signal(app_ptr);
	atomic_store_explicit(&done, true, memory_order_release);
}

int
main(int argc, char **argv)
{
	static app_t app;
	app_ptr = &app; // set global pointer

	app.nxt_source = 30; //FIXME make dependent on widget height
	app.nxt_sink = 720/2;
	app.nxt_default = 30;

	app.server_name = NULL;
	app.session_id = NULL;

	fprintf(stderr,
		"PatchMatrix "PATCHMATRIX_VERSION"\n"
		"Copyright (c) 2016 Hanspeter Portner (dev@open-music-kontrollers.ch)\n"
		"Released under Artistic License 2.0 by Open Music Kontrollers\n");

	int c;
	while((c = getopt(argc, argv, "vhn:u:")) != -1)
	{
		switch(c)
		{
			case 'v':
				fprintf(stderr,
					"--------------------------------------------------------------------\n"
					"This is free software: you can redistribute it and/or modify\n"
					"it under the terms of the Artistic License 2.0 as published by\n"
					"The Perl Foundation.\n"
					"\n"
					"This source is distributed in the hope that it will be useful,\n"
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
					"Artistic License 2.0 for more details.\n"
					"\n"
					"You should have received a copy of the Artistic License 2.0\n"
					"along the source as a COPYING file. If not, obtain it from\n"
					"http://www.perlfoundation.org/artistic_license_2_0.\n\n");
				return 0;
			case 'h':
				fprintf(stderr,
					"--------------------------------------------------------------------\n"
					"USAGE\n"
					"   %s [OPTIONS]\n"
					"\n"
					"OPTIONS\n"
					"   [-v]                 print version and full license information\n"
					"   [-h]                 print usage information\n"
					"   [-n] server-name     connect to named JACK daemon\n"
					"   [-u] client-uuid     client UUID for JACK session management\n\n"
					, argv[0]);
				return 0;
			case 'n':
				app.server_name = optarg;
				break;
			case 'u':
				app.session_id = optarg;
				break;
			case '?':
				if( (optopt == 'n') || (optopt == 'u') )
					fprintf(stderr, "Option `-%c' requires an argument.\n", optopt);
				else if(isprint(optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				return -1;
			default:
				return -1;
		}
	}

	signal(SIGINT, _sig);

	if(!(app.from_jack = varchunk_new(0x10000, true)))
		goto cleanup;

	if(_jack_init(&app))
		goto cleanup;

	if(_ui_init(&app))
		goto cleanup;

	_ui_populate(&app);
	app.needs_refresh = true;

	while(!atomic_load_explicit(&done, memory_order_acquire))
	{
		nk_pugl_wait_for_event(&app.win);

		if(  _jack_anim(&app)
			|| nk_pugl_process_events(&app.win) )
		{
			atomic_store_explicit(&done, true, memory_order_release);
		}
	}

cleanup:
	_ui_depopulate(&app);
	_ui_deinit(&app);
	_jack_deinit(&app);
	if(app.from_jack)
	{
		_jack_anim(&app); // drain ringbuffer
		varchunk_free(app.from_jack);
	}

	fprintf(stderr, "bye from PatchMatrix\n");

	return 0;
}
