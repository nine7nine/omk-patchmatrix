/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <Elementary.h>

#include <sqlite3.h>

#include <jack/jack.h>
#include <jack/metadata.h>
#include <jack/uuid.h>

#include <patcher.h>

typedef struct _app_t app_t;
typedef struct _event_t event_t;

enum {
	EVENT_CLIENT_REGISTER,
	EVENT_PORT_REGISTER,
	EVENT_PORT_CONNECT,
	EVENT_PROPERTY_CHANGE,
	EVENT_ON_INFO_SHUTDOWN
};

struct _event_t {
	app_t *app;
	int type;

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

		struct {
			jack_uuid_t uuid;
			char *key;
			jack_property_change_t state;
		} property_change;

		struct {
			jack_status_t code;
			char *reason;
		} on_info_shutdown;
	};
};

struct _app_t {
	// UI
	int w;
	int h;
	Evas_Object *win;
	Evas_Object *patcher [4];
	Evas_Object *pane;
	Evas_Object *list;
	Evas_Object *grid;
	Elm_Genlist_Item_Class *clientitc;
	Elm_Genlist_Item_Class *portitc;
	Elm_Gengrid_Item_Class *griditc;

	// JACK
	jack_client_t *client;
	Eina_List *events;
	Ecore_Timer *timer;

	// SQLite3
	sqlite3 *db;

	sqlite3_stmt *query_client_add;
	sqlite3_stmt *query_client_del;
	sqlite3_stmt *query_client_find_by_name;
	sqlite3_stmt *query_client_find_by_id;
	sqlite3_stmt *query_client_get_selected;
	sqlite3_stmt *query_client_set_selected;

	sqlite3_stmt *query_port_add;
	sqlite3_stmt *query_port_del;
	sqlite3_stmt *query_port_find_by_name;
	sqlite3_stmt *query_port_find_by_id;
	sqlite3_stmt *query_port_select;

	sqlite3_stmt *query_connection_add;
	sqlite3_stmt *query_connection_del;
	sqlite3_stmt *query_connection_get;

	sqlite3_stmt *query_port_list;
	sqlite3_stmt *query_client_port_list;
};

static void _ui_refresh(app_t *app);
static void _ui_realize(app_t *app);

static int
_db_init(app_t *app)
{
	int ret;
	char *err;
	if( (ret = sqlite3_open(":memory:", &app->db)) )
	{
		fprintf(stderr, "_db_init: could not open in-memory database\n");
		return -1;
	}
	
	if( (ret = sqlite3_exec(app->db,
		"CREATE TABLE Clients ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT,"
			"pretty_name TEXT,"
			"selected BOOL,"
			"uuid UNSIGNED BIG INT);"
			""
		"CREATE TABLE Ports ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT,"
			"short_name TEXT,"
			"pretty_name TEXT,"
			"client_id INT,"
			"type_id INT,"
			"direction_id INT,"
			"selected BOOL,"
			"uuid UNSIGNED BIG INT);"
			""
		"CREATE TABLE Connections ("
			"id INTEGER PRIMARY KEY,"
			"source_id INT,"
			"sink_id INT);"
			""
		"CREATE TABLE Types ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT,"
			"selected BOOL);"
			""
		"CREATE TABLE Directions ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT);"
			""
		"INSERT INTO Types (id, name, selected) VALUES (0, 'AUDIO', 1);"
		"INSERT INTO Types (id, name, selected) VALUES (1, 'MIDI', 1);"
		"INSERT INTO Types (id, name, selected) VALUES (2, 'CV', 1);"
		"INSERT INTO Types (id, name, selected) VALUES (3, 'OSC', 1);"
			""
		"INSERT INTO Directions (id, name) VALUES (0, 'SOURCE');"
		"INSERT INTO Directions (id, name) VALUES (1, 'SINK');",
		NULL, NULL, &err)) )
	{
		fprintf(stderr, "_db_init: %s\n", err);
		sqlite3_free(err);
		return -1;
	}

	// Client
	ret = sqlite3_prepare_v2(app->db,
		"INSERT INTO Clients (name, pretty_name, uuid, selected) VALUES ($1, $2, $3, 0)",
		-1, &app->query_client_add, NULL);

	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Clients WHERE id=$1",
		-1, &app->query_client_del, NULL);

	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Clients WHERE name=$1",
		-1, &app->query_client_find_by_name, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"SELECT name, pretty_name FROM Clients WHERE id=$1",
		-1, &app->query_client_find_by_id, NULL);
		
	ret = sqlite3_prepare_v2(app->db,
		"SELECT selected FROM Clients WHERE id=$1",
		-1, &app->query_client_get_selected, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Clients SET selected=$1 WHERE id=$2",
		-1, &app->query_client_set_selected, NULL);

	// Port
	ret = sqlite3_prepare_v2(app->db,
		"INSERT INTO Ports (name, client_id, short_name, pretty_name, type_id, direction_id, uuid, selected) VALUES ($1, $2, $3, $4, $5, $6, $7, 1)",
		-1, &app->query_port_add, NULL);

	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Ports WHERE id=$1",
		-1, &app->query_port_del, NULL);

	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE name=$1",
		-1, &app->query_port_find_by_name, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"SELECT name, short_name FROM Ports WHERE id=$1",
		-1, &app->query_port_find_by_id, NULL);
		
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Ports SET selected=$1 WHERE id=$2",
		-1, &app->query_port_select, NULL);
	
	ret = sqlite3_prepare_v2(app->db,
		"INSERT INTO Connections (source_id, sink_id) VALUES ($1, $2)",
		-1, &app->query_connection_add, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Connections WHERE source_id=$1 AND sink_id=$2",
		-1, &app->query_connection_del, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Connections WHERE source_id=$1 AND sink_id=$2",
		-1, &app->query_connection_get, NULL);

	// port list
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id, client_id FROM Ports WHERE type_id=$1 AND direction_id=$2 ORDER BY client_id",
		-1, &app->query_port_list, NULL);
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE client_id=$1 ORDER BY direction_id, type_id",
		-1, &app->query_client_port_list, NULL);

	return 0;
}

static void
_db_deinit(app_t *app)
{
	int ret;

	if(!app->db)
		return;
	
	ret = sqlite3_finalize(app->query_client_add);
	ret = sqlite3_finalize(app->query_client_del);
	ret = sqlite3_finalize(app->query_client_find_by_name);
	ret = sqlite3_finalize(app->query_client_find_by_id);
	ret = sqlite3_finalize(app->query_client_get_selected);
	ret = sqlite3_finalize(app->query_client_set_selected);

	ret = sqlite3_finalize(app->query_port_add);
	ret = sqlite3_finalize(app->query_port_del);
	ret = sqlite3_finalize(app->query_port_find_by_name);
	ret = sqlite3_finalize(app->query_port_find_by_id);
	ret = sqlite3_finalize(app->query_port_select);
	
	ret = sqlite3_finalize(app->query_connection_add);
	ret = sqlite3_finalize(app->query_connection_del);
	ret = sqlite3_finalize(app->query_connection_get);
	
	ret = sqlite3_finalize(app->query_port_list);
	ret = sqlite3_finalize(app->query_client_port_list);

	if( (ret = sqlite3_close(app->db)) )
		fprintf(stderr, "_db_deinit: could not close in-memory database\n");
}

static int
_db_client_find_by_name(app_t *app, const char *name)
{
	int ret;
	int id;

	sqlite3_stmt *stmt = app->query_client_find_by_name;
		
	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);

	return id;
}

static void
_db_client_find_by_id(app_t *app, int id, char **name, char **pretty_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_find_by_id;
		
	ret = sqlite3_bind_int(stmt, 1, id);

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		if(name)
			*name = strdup((const char *)sqlite3_column_text(stmt, 0));
		if(pretty_name)
			*pretty_name = strdup((const char *)sqlite3_column_text(stmt, 1));
	}
	else
	{
		if(name)
			*name = NULL;
		if(pretty_name)
			*pretty_name = NULL;
	}

	ret = sqlite3_reset(stmt);
}

static void
_db_client_add(app_t *app, const char *name)
{
	int ret;
		
	jack_uuid_t uuid;
				
	const char *uuid_str = jack_get_uuid_for_client_name(app->client, name);
	if(uuid_str)
		jack_uuid_parse(uuid_str, &uuid);
	else
		jack_uuid_clear(&uuid);
	
	char *value = NULL;
	char *type = NULL;
	jack_get_property(uuid, JACK_METADATA_PRETTY_NAME, &value, &type);

	sqlite3_stmt *stmt = app->query_client_add;

	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);
	ret = sqlite3_bind_text(stmt, 2, value ? value : name, -1, NULL);
	ret = sqlite3_bind_int64(stmt, 3, uuid);
	
	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);

	if(value)
		free(value);
	if(type)
		free(type);

	int *data = calloc(1, sizeof(int));
	*data = _db_client_find_by_name(app, name);
	elm_genlist_item_append(app->list, app->clientitc, data, NULL,
		ELM_GENLIST_ITEM_TREE, NULL, NULL);
}

static void
_db_client_del(app_t *app, const char *name)
{
	int ret;

	int id = _db_client_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_client_del;

	ret = sqlite3_bind_int(stmt, 1, id);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->clientitc)
			continue; // ignore port items

		int *ref = elm_object_item_data_get(itm);
		if(*ref == id)
		{
			elm_object_item_del(itm);
			break;
		}
	}
}

static int
_db_client_get_selected(app_t *app, const char *name)
{
	int ret;
	int selected;

	int id = _db_client_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_client_get_selected;

	ret = sqlite3_bind_int(stmt, 1, id);

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		selected = sqlite3_column_int(stmt, 0);
	else
		selected = -1;

	ret = sqlite3_reset(stmt);

	return selected;
}

static void
_db_client_set_selected(app_t *app, const char *name, int selected)
{
	int ret;

	int id = _db_client_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_client_set_selected;

	ret = sqlite3_bind_int(stmt, 1, id);
	ret = sqlite3_bind_int(stmt, 2, selected);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);
}
					
static void
_db_port_add(app_t *app, const char *client_name, const char *name,
	const char *short_name)
{
	int ret;

	jack_port_t *port = jack_port_by_name(app->client, name);
	int client_id = _db_client_find_by_name(app, client_name);
	int midi = !strcmp(jack_port_type(port), JACK_DEFAULT_MIDI_TYPE) ? 1 : 0;
	int type_id = midi ? 1 : 0;
	int flags = jack_port_flags(port);
	int direction_id = flags & JackPortIsInput ? 1 : 0;
	jack_uuid_t uuid = jack_port_uuid(port);
	
	char *value = NULL;
	char *type = NULL;

	if(type_id == 0) // signal-type
	{
		jack_get_property(uuid, "http://jackaudio.org/metadata/signal-type", &value, &type);
		if(value && !strcmp(value, "CV"))
		{
			type_id = 2;
			free(value);
		}
		if(type)
			free(type);
	}
	else if(type_id == 1) // event-type
	{
		jack_get_property(uuid, "http://jackaudio.org/metadata/event-types", &value, &type);
		if(value && strstr(value, "OSC"))
		{
			type_id = 3;
			free(value);
		}
		if(type)
			free(type);
	}

	value = NULL;
	type = NULL;
	jack_get_property(uuid, JACK_METADATA_PRETTY_NAME, &value, &type);

	sqlite3_stmt *stmt = app->query_port_add;

	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);
	ret = sqlite3_bind_int(stmt, 2, client_id);
	ret = sqlite3_bind_text(stmt, 3, short_name, -1, NULL);
	ret = sqlite3_bind_text(stmt, 4, value ? value : short_name, -1, NULL);
	ret = sqlite3_bind_int(stmt, 5, type_id);
	ret = sqlite3_bind_int(stmt, 6, direction_id);
	ret = sqlite3_bind_int(stmt, 7, uuid);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);

	if(value)
		free(value);
	if(type)
		free(type);
}

static int
_db_port_find_by_name(app_t *app, const char *name)
{
	int ret;
	int id;

	sqlite3_stmt *stmt = app->query_port_find_by_name;
		
	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);

	return id;
}

static void
_db_port_find_by_id(app_t *app, int id, char **name, char **short_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_find_by_id;
		
	ret = sqlite3_bind_int(stmt, 1, id);

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		if(name)
			*name = strdup((const char *)sqlite3_column_text(stmt, 0));
		if(short_name)
			*short_name = strdup((const char *)sqlite3_column_text(stmt, 1));
	}
	else
	{
		if(name)
			*name = NULL;
		if(short_name)
			*short_name = NULL;
	}

	ret = sqlite3_reset(stmt);
}

static void
_db_port_del(app_t *app, const char *client_name, const char *name,
	const char *short_name)
{
	int ret;

	int id = _db_port_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_port_del;

	ret = sqlite3_bind_int(stmt, 1, id);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);
	
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->portitc)
			continue; // ignore client items

		int *ref = elm_object_item_data_get(itm);
		if(*ref == id)
		{
			elm_object_item_del(itm);
			break;
		}
	}
}

static void
_db_connection_add(app_t *app, const char *name_source, const char *name_sink)
{
	int ret;

	int id_source = _db_port_find_by_name(app, name_source);
	int id_sink = _db_port_find_by_name(app, name_sink);

	sqlite3_stmt *stmt = app->query_connection_add;

	ret = sqlite3_bind_int(stmt, 1, id_source);
	ret = sqlite3_bind_int(stmt, 2, id_sink);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);
}

static void
_db_connection_del(app_t *app, const char *name_source, const char *name_sink)
{
	int ret;

	int id_source = _db_port_find_by_name(app, name_source);
	int id_sink = _db_port_find_by_name(app, name_sink);

	sqlite3_stmt *stmt = app->query_connection_del;

	ret = sqlite3_bind_int(stmt, 1, id_source);
	ret = sqlite3_bind_int(stmt, 2, id_sink);

	ret = sqlite3_step(stmt);

	ret = sqlite3_reset(stmt);
}

static int
_db_connection_get(app_t *app, const char *name_source, const char *name_sink)
{
	int ret;
	int connected;

	int id_source = _db_port_find_by_name(app, name_source);
	int id_sink = _db_port_find_by_name(app, name_sink);

	sqlite3_stmt *stmt = app->query_connection_get;

	ret = sqlite3_bind_int(stmt, 1, id_source);
	ret = sqlite3_bind_int(stmt, 2, id_sink);

	ret = sqlite3_step(stmt);
	
	if(ret != SQLITE_DONE)
		connected = 1;
	else
		connected = 0;

	ret = sqlite3_reset(stmt);

	return connected;
}

static Eina_Bool
_jack_timer_cb(void *data)
{
	app_t *app = data;

	event_t *ev;
	EINA_LIST_FREE(app->events, ev)
	{
		printf("_jack_timer_cb: %i\n", ev->type);

		switch(ev->type)
		{
			case EVENT_CLIENT_REGISTER:
			{
				printf("client_register: %s %i\n", ev->client_register.name,
					ev->client_register.state);

				if(ev->client_register.state)
					_db_client_add(app, ev->client_register.name);
				else
					_db_client_del(app, ev->client_register.name);

				free(ev->client_register.name); // strdup

				break;
			}
			case EVENT_PORT_REGISTER:
			{
				const jack_port_t *port = jack_port_by_id(app->client, ev->port_register.id);
				jack_uuid_t uuid = jack_port_uuid(port);
				const char *name = jack_port_name(port);
				char *sep = strchr(name, ':');
				char *client_name = strndup(name, sep - name);
				const char *short_name = sep + 1;

				printf("port_register: %s %i\n", name, ev->port_register.state);

				if(ev->port_register.state)
					_db_port_add(app, client_name, name, short_name);
				else
					_db_port_del(app, client_name, name, short_name);

				free(client_name); // strdup

				break;
			}
			case EVENT_PORT_CONNECT:
			{
				// never reached

				break;
			}
			case EVENT_PROPERTY_CHANGE:
			{
				printf("property_change: %lu %s %i\n", ev->property_change.uuid,
					ev->property_change.key, ev->property_change.state);

				//TODO
				free(ev->property_change.key); // strdup

				break;
			}
			case EVENT_ON_INFO_SHUTDOWN:
			{
				// never reached

				break;
			}
		};

		free(ev);
	}

	_ui_refresh(app);

	return EINA_TRUE;
}

static void
_jack_async(void *data)
{
	event_t *ev = data;
	app_t *app = ev->app;

	int done = 0;

	switch(ev->type)
	{
		case EVENT_CLIENT_REGISTER:
		{
			// aggregate to event queue
			app->events = eina_list_append(app->events, ev);

			break;
		}
		case EVENT_PORT_REGISTER:
		{
			// aggregate to event queue
			app->events = eina_list_append(app->events, ev);

			break;
		}
		case EVENT_PORT_CONNECT:
		{
			const jack_port_t *port_source = jack_port_by_id(app->client, ev->port_connect.id_source);
			const jack_port_t *port_sink = jack_port_by_id(app->client, ev->port_connect.id_sink);
			const char *name_source = jack_port_name(port_source);
			const char *name_sink = jack_port_name(port_sink);

			printf("port_connect: %s %s %i\n", name_source, name_sink,
				ev->port_connect.state);

			if(ev->port_connect.state)
				_db_connection_add(app, name_source, name_sink);
			else
				_db_connection_del(app, name_source, name_sink);
				
			_ui_realize(app);

			free(ev);

			break;
		}
		case EVENT_PROPERTY_CHANGE:
		{
			// aggregate to event queue
			app->events = eina_list_append(app->events, ev);

			break;
		}
		case EVENT_ON_INFO_SHUTDOWN:
		{
			app->client = NULL; // JACK has shut down, hasn't it?
			done = 1;	

			free(ev);

			break;
		}
	};

	if(done)
		elm_exit();
}

static void
_jack_on_info_shutdown_cb(jack_status_t code, const char *reason, void *arg)
{
	app_t *app = arg;
	
	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_ON_INFO_SHUTDOWN;
	ev->on_info_shutdown.code = code;
	ev->on_info_shutdown.reason = strdup(reason);

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}

static void
_jack_freewheel_cb(int starting, void *arg)
{
	app_t *app = arg;

	//FIXME
}

static int
_jack_buffer_size_cb(jack_nframes_t nframes, void *arg)
{
	app_t *app = arg;

	//FIXME

	return 0;
}

static int
_jack_saapple_rate_cb(jack_nframes_t nframes, void *arg)
{
	app_t *app = arg;

	//FIXME

	return 0;
}

static void
_jack_client_registration_cb(const char *name, int state, void *arg)
{
	app_t *app = arg;
	
	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_CLIENT_REGISTER;
	ev->client_register.name = strdup(name);
	ev->client_register.state = state;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}

static void
_jack_port_registration_cb(jack_port_id_t id, int state, void *arg)
{
	app_t *app = arg;
	
	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_PORT_REGISTER;
	ev->port_register.id = id;
	ev->port_register.state = state;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}

static void
_jack_port_rename_cb(jack_port_id_t id, const char *old_name, const char *new_name, void *arg)
{
	app_t *app = arg;

	//FIXME
}

static void
_jack_port_connect_cb(jack_port_id_t id_source, jack_port_id_t id_sink, int state, void *arg)
{
	app_t *app = arg;

	/*
	const jack_port_t *port_source = jack_port_by_id(app->client, id_source);
	const jack_port_t *port_sink = jack_port_by_id(app->client, id_sink);

	jack_uuid_t uuid_source = jack_port_uuid(port_source);
	jack_uuid_t uuid_sink = jack_port_uuid(port_sink);
	*/

	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_PORT_CONNECT;
	ev->port_connect.id_source = id_source;
	ev->port_connect.id_sink = id_sink;
	ev->port_connect.state = state;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}

static int
_jack_xrun_cb(void *arg)
{
	app_t *app = arg;

	//FIXME

	return 0;
}

static int
_jack_graph_order_cb(void *arg)
{
	//FIXME

	return 0;
}

static void
_jack_property_change_cb(jack_uuid_t uuid, const char *key, jack_property_change_t state, void *arg)
{
	app_t *app = arg;

	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_PROPERTY_CHANGE;
	ev->property_change.uuid = uuid;
	ev->property_change.key = strdup(key);
	ev->property_change.state = state;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}

static int
_jack_init(app_t *app)
{
	jack_status_t status;

	app->client = jack_client_open("patchmatrix", JackNullOption, &status);
	if(!app->client)
		return -1;
	
	jack_on_info_shutdown(app->client, _jack_on_info_shutdown_cb, app);

	jack_set_freewheel_callback(app->client, _jack_freewheel_cb, app);
	jack_set_buffer_size_callback(app->client, _jack_buffer_size_cb, app);
	jack_set_sample_rate_callback(app->client, _jack_saapple_rate_cb, app);

	jack_set_client_registration_callback(app->client, _jack_client_registration_cb, app);
	jack_set_port_registration_callback(app->client, _jack_port_registration_cb, app);
	//jack_set_port_rename_callback(app->client, _jack_port_rename_cb, app);
	jack_set_port_connect_callback(app->client, _jack_port_connect_cb, app);
	jack_set_xrun_callback(app->client, _jack_xrun_cb, app);
	jack_set_graph_order_callback(app->client, _jack_graph_order_cb, app);
	jack_set_property_change_callback(app->client, _jack_property_change_cb, app);

	jack_activate(app->client);
	
	app->timer = ecore_timer_loop_add(0.5, _jack_timer_cb, app);

	return 0;
}

static void
_jack_deinit(app_t *app)
{
	if(!app->client)
		return;

	ecore_timer_del(app->timer);
	jack_deactivate(app->client);
	jack_client_close(app->client);
}

static void
_ui_delete_request(void *data, Evas_Object *obj, void *event)
{
	app_t *app = data;

	elm_exit();	
}

static void
_ui_connect_request(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;

	patcher_event_t *ev = event_info;
	if(!app || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	int source_id = source->id;
	int sink_id = sink->id;

	char *source_name;
	char *sink_name;
	_db_port_find_by_id(app, source_id, &source_name, NULL);
	_db_port_find_by_id(app, sink_id, &sink_name, NULL);
	if(!source_name || !sink_name)
		return;

	printf("connect_request: %s %s\n", source_name, sink_name);

	jack_connect(app->client, source_name, sink_name);

	free(source_name);
	free(sink_name);
}

static void
_ui_disconnect_request(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;

	patcher_event_t *ev = event_info;
	if(!app || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	int source_id = source->id;
	int sink_id = sink->id;

	char *source_name;
	char *sink_name;
	_db_port_find_by_id(app, source_id, &source_name, NULL);
	_db_port_find_by_id(app, sink_id, &sink_name, NULL);
	if(!source_name || !sink_name)
		return;

	printf("disconnect_request: %s %s\n", source_name, sink_name);

	jack_disconnect(app->client, source_name, sink_name);

	free(source_name);
	free(sink_name);
}

static void
_ui_realize_request(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;
	patcher_event_t *ev = event_info;
	if(!app || !ev)
		return;

	patcher_event_t *source = &ev[0];
	patcher_event_t *sink = &ev[1];
	if(!source || !sink)
		return;

	int source_id = source->id;
	int sink_id = sink->id;

	char *source_name;
	char *sink_name;
	 _db_port_find_by_id(app, source_id, &source_name, NULL);
	 _db_port_find_by_id(app, sink_id, &sink_name, NULL);
	if(!source_name || !sink_name)
		return;

	int connected = _db_connection_get(app, source_name, sink_name);

	//printf("realize_request: %s %s %i\n", source_name, sink_name, connected);

	patcher_object_connected_set(obj, source_id, sink_id, connected, 0);

	free(source_name);
	free(sink_name);
}

static char *
_ui_client_list_label_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	char *name;
	char *pretty_name;
	_db_client_find_by_id(app, *id, &name, &pretty_name);

	if(!strcmp(part, "elm.text"))
	{
		return pretty_name;
	}
	else if(!strcmp(part, "elm.text.sub"))
	{
		return name;
	}

	return NULL;
}

static void
_ui_client_list_del(void *data, Evas_Object *obj)
{
	int *id = data;

	free(id);
}

static char *
_ui_port_list_label_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	char *name;
	char *short_name;
	_db_port_find_by_id(app, *id, &name, &short_name);

	if(!strcmp(part, "elm.text"))
	{
		return short_name;
	}
	else if(!strcmp(part, "elm.text.sub"))
	{
		return name;
	}

	return NULL;
}

static void
_ui_port_list_del(void *data, Evas_Object *obj)
{
	int *id = data;

	free(id);
}

static void
_ui_list_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	//TODO
}

static void
_ui_list_expand_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	elm_genlist_item_expanded_set(itm, EINA_TRUE);
}

static void
_ui_list_contract_request(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	elm_genlist_item_expanded_set(itm, EINA_FALSE);
}

static void
_ui_list_expanded(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	int *client_id = elm_object_item_data_get(itm);

	sqlite3_stmt *stmt = app->query_client_port_list;
		
	sqlite3_bind_int(stmt, 1, *client_id); // client_id

	while(sqlite3_step(stmt) != SQLITE_DONE)
	{
		int id = sqlite3_column_int(stmt, 0);

		int *ref = calloc(1, sizeof(int));
		*ref = id;
		elm_genlist_item_append(app->list, app->portitc,
			ref, itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
	}

	sqlite3_reset(stmt);
}

static void
_ui_list_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	elm_genlist_item_subitems_clear(itm);
}

static char *
_ui_grid_label_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;

	if(!strcmp(part, "elm.text"))
	{
		switch(*id)
		{
			case 0:
				return strdup("Audio Ports");
			case 1:
				return strdup("MIDI Ports");
			case 2:
				return strdup("CV Ports");
			case 3:
				return strdup("OSC Ports");
		}
	}

	return NULL;
}

static void
_ui_patcher_free(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	app->patcher[*id] = NULL;
}

static Evas_Object *
_ui_grid_content_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	if(!strcmp(part, "elm.swallow.icon"))
	{
		Evas_Object *patcher = patcher_object_add(evas_object_evas_get(obj));
		if(patcher)
		{
			evas_object_data_set(patcher, "app", app);
			evas_object_smart_callback_add(patcher, "connect,request",
				_ui_connect_request, app);
			evas_object_smart_callback_add(patcher, "disconnect,request",
				_ui_disconnect_request, app);
			evas_object_smart_callback_add(patcher, "realize,request",
				_ui_realize_request, app);
			evas_object_size_hint_weight_set(patcher, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(patcher, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_event_callback_add(patcher, EVAS_CALLBACK_DEL, _ui_patcher_free, id);
			evas_object_show(patcher);

			app->patcher[*id] = patcher;

			return patcher;
		}
	}

	return NULL;
}

static void
_ui_grid_del(void *data, Evas_Object *obj)
{
	int *id = data;

	free(id);
}

static int
_ui_init(app_t *app)
{
	// UI
	app->w = 860;
	app->h = 645;
	
	app->clientitc = elm_genlist_item_class_new();
	if(app->clientitc)
	{
		app->clientitc->item_style = "double_label";
		app->clientitc->func.text_get = _ui_client_list_label_get;
		app->clientitc->func.content_get = NULL;
		app->clientitc->func.state_get = NULL;
		app->clientitc->func.del = _ui_client_list_del;
	}
	
	app->portitc = elm_genlist_item_class_new();
	if(app->portitc)
	{
		app->portitc->item_style = "double_label";
		app->portitc->func.text_get = _ui_port_list_label_get;
		app->portitc->func.content_get = NULL;
		app->portitc->func.state_get = NULL;
		app->portitc->func.del = _ui_port_list_del;
	}
	
	app->griditc = elm_gengrid_item_class_new();
	if(app->griditc)
	{
		app->griditc->item_style = "default";
		app->griditc->func.text_get = _ui_grid_label_get;
		app->griditc->func.content_get = _ui_grid_content_get;
		app->griditc->func.state_get = NULL;
		app->griditc->func.del = _ui_grid_del;
	}

	app->win = elm_win_util_standard_add("PatchMatrix", "PatchMarix");
	if(!app->win)
		return -1;

	evas_object_smart_callback_add(app->win, "delete,request", _ui_delete_request, app);
	evas_object_resize(app->win, app->w, app->h);
	evas_object_show(app->win);

	app->pane = elm_panes_add(app->win);
	if(app->pane)
	{
		elm_panes_horizontal_set(app->pane, EINA_FALSE);
		evas_object_size_hint_weight_set(app->pane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(app->pane, EVAS_HINT_FILL, EVAS_HINT_FILL);
		elm_win_resize_object_add(app->win, app->pane);
		evas_object_show(app->pane);

		app->list = elm_genlist_add(app->pane);
		if(app->list)
		{
			evas_object_smart_callback_add(app->list, "activated",
				_ui_list_activated, app);
			evas_object_smart_callback_add(app->list, "expand,request",
				_ui_list_expand_request, app);
			evas_object_smart_callback_add(app->list, "contract,request",
				_ui_list_contract_request, app);
			evas_object_smart_callback_add(app->list, "expanded",
				_ui_list_expanded, app);
			evas_object_smart_callback_add(app->list, "contracted",
				_ui_list_contracted, app);
			evas_object_data_set(app->list, "app", app);
			evas_object_size_hint_weight_set(app->list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(app->list, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(app->list);

			elm_object_part_content_set(app->pane, "left", app->list);
		} // app->list

		app->grid = elm_gengrid_add(app->pane);
		if(app->grid)
		{
			elm_gengrid_item_size_set(app->grid, 400, 400);
			elm_gengrid_select_mode_set(app->grid, ELM_OBJECT_SELECT_MODE_NONE);
			elm_gengrid_reorder_mode_set(app->grid, EINA_TRUE);
			evas_object_data_set(app->grid, "app", app);
			evas_object_size_hint_weight_set(app->grid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(app->grid, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(app->grid);

			elm_object_part_content_set(app->pane, "right", app->grid);

			for(int i=0; i<4; i++)
			{
				int *id = calloc(1, sizeof(id));
				*id = i;
				elm_gengrid_item_append(app->grid, app->griditc, id, NULL, NULL);
			}
		} // app->grid
	} // app->pane

	return 0;
}

static void
_ui_deinit(app_t *app)
{
	if(!app->win)
		return;

	for(int i=0; i<4; i++)
		evas_object_del(app->patcher[i]);
	evas_object_del(app->win);

	if(app->clientitc)
		elm_genlist_item_class_free(app->clientitc);
	if(app->portitc)
		elm_genlist_item_class_free(app->portitc);
	if(app->griditc)
		elm_gengrid_item_class_free(app->griditc);
}

static void
_ui_populate(app_t *app)
{
	elm_genlist_clear(app->list);

	const char **sources = jack_get_ports(app->client, NULL, NULL, JackPortIsOutput);
	const char **sinks = jack_get_ports(app->client, NULL, NULL, JackPortIsInput);

	if(sources)
	{
		for(const char **source=sources; *source; source++)
		{
			char *sep = strchr(*source, ':');
			char *client_name = strndup(*source, sep - *source);
			const char *short_name = sep + 1;

			if(_db_client_find_by_name(app, client_name) < 0)
				_db_client_add(app, client_name);

			_db_port_add(app, client_name, *source, short_name);
		}
	}

	if(sinks)
	{
		for(const char **sink=sinks; *sink; sink++)
		{
			char *sep = strchr(*sink, ':');
			char *client_name = strndup(*sink, sep - *sink);
			const char *short_name = sep + 1;

			if(_db_client_find_by_name(app, client_name) < 0)
				_db_client_add(app, client_name);

			_db_port_add(app, client_name, *sink, short_name);
		}
		free(sinks);
	}

	if(sources)
	{
		for(const char **source=sources; *source; source++)
		{
			const jack_port_t *port = jack_port_by_name(app->client, *source);
			const char **targets = jack_port_get_all_connections(app->client, port);
			if(targets)
			{
				for(const char **target=targets; *target; target++)
				{
					_db_connection_add(app, *source, *target);
				}
				free(targets);
			}
		}
		free(sources);
	}
}

// update grids and connections
static void
_ui_refresh(app_t *app)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_list;

	for(int i=0; i<4; i++)
	{
		if(!app->patcher[i])
			continue;

		ret = sqlite3_bind_int(stmt, 1, i); // type
		ret = sqlite3_bind_int(stmt, 2, 0); // source
		int num_sources = 0;
		while(sqlite3_step(stmt) != SQLITE_DONE)
			num_sources += 1;
		ret = sqlite3_reset(stmt);
		
		ret = sqlite3_bind_int(stmt, 1, i); // type
		ret = sqlite3_bind_int(stmt, 2, 1); // sink
		int num_sinks = 0;
		while(sqlite3_step(stmt) != SQLITE_DONE)
			num_sinks += 1;
		ret = sqlite3_reset(stmt);

		patcher_object_dimension_set(app->patcher[i], num_sources, num_sinks);

		ret = sqlite3_bind_int(stmt, 1, i); // type
		ret = sqlite3_bind_int(stmt, 2, 0); // source
		for(int source=0; source<num_sources; source++)
		{
			ret = sqlite3_step(stmt);
			int id = sqlite3_column_int(stmt, 0);
			int client_id = sqlite3_column_int(stmt, 1);
			char *name, *short_name;
			_db_port_find_by_id(app, id, &name, &short_name);

			patcher_object_source_id_set(app->patcher[i], source, id);
			patcher_object_source_color_set(app->patcher[i], source, client_id % 20 + 1);
			patcher_object_source_label_set(app->patcher[i], source, short_name);

			free(name);
			free(short_name);
		}
		ret = sqlite3_reset(stmt);

		ret = sqlite3_bind_int(stmt, 1, i); // type
		ret = sqlite3_bind_int(stmt, 2, 1); // sink
		for(int sink=0; sink<num_sinks; sink++)
		{
			ret = sqlite3_step(stmt);
			int id = sqlite3_column_int(stmt, 0);
			int client_id = sqlite3_column_int(stmt, 1);
			char *name, *short_name;
			_db_port_find_by_id(app, id, &name, &short_name);

			patcher_object_sink_id_set(app->patcher[i], sink, id);
			patcher_object_sink_color_set(app->patcher[i], sink, client_id % 20 + 1);
			patcher_object_sink_label_set(app->patcher[i], sink, short_name);

			free(name);
			free(short_name);
		}
		ret = sqlite3_reset(stmt);

		patcher_object_realize(app->patcher[i]);
	}
}

// update connections
static void
_ui_realize(app_t *app)
{
	for(int i=0; i<4; i++)
	{
		if(!app->patcher[i])
			continue;

		patcher_object_realize(app->patcher[i]);
	}
}
	
static int
elm_main(int argc, char **argv)
{
	static app_t app;

	if(_jack_init(&app))
		goto cleanup;

	if(_db_init(&app))
		goto cleanup;

	if(_ui_init(&app))
		goto cleanup;

	_ui_populate(&app);
	_ui_refresh(&app);

	elm_run();

cleanup:
	_ui_deinit(&app);
	_db_deinit(&app);
	_jack_deinit(&app);

	return 0;
}

ELM_MAIN()
