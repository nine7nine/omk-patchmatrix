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

#ifndef _PATCHMATRIX_H
#define _PATCHMATRIX_H

#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h> // mkdir
#include <errno.h> // mkdir
#include <signal.h>
#include <string.h>

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

#define NK_PUGL_API
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
	bool moving;
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
	int32_t sample_rate;

	union {
		struct {
			float dBFSs [PORT_MAX];
		} audio;
		struct {
			float vels [PORT_MAX];
		} midi;
	};
};

struct _port_t {
	jack_port_t *body;
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
	bool active;
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
	bool moving;
	bool hilighted;
	bool hovered;

	mixer_t *mixer;
	monitor_t *monitor;
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

	const char *server_name;
	const char *session_id;

	nk_pugl_window_t win;

	float dy;

	float nxt_source;
	float nxt_sink;
	float nxt_default;
	hash_t clients;
	hash_t conns;

	struct node_editor nodedit;

	struct {
		struct nk_image audio;
		struct nk_image midi;
#ifdef JACK_HAS_METADATA_API
		struct nk_image cv;
		struct nk_image osc;
#endif
	} icons;

	atomic_bool done;
	bool animating;
	client_t *contextual;
	struct nk_rect contextbounds;
};

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
	hash->nodes = realloc(hash->nodes, (hash->size + 1)*sizeof(void *));
	if(hash->nodes)
	{
		hash->nodes[hash->size] = node;
		hash->size++;
	}
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
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
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
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
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

#endif // _PATCHMATRIX_H
