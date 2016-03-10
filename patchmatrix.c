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

#include <stdbool.h>

#include <Elementary.h>

#include <sqlite3.h>

#include <jack/jack.h>
#ifdef JACK_HAS_METADATA_API
#	include <jack/uuid.h>
#	include <jack/metadata.h>
#endif

#include <patcher.h>

typedef struct _app_t app_t;
typedef struct _event_t event_t;

enum {
	EVENT_CLIENT_REGISTER,
	EVENT_PORT_REGISTER,
	EVENT_PORT_CONNECT,
	EVENT_PROPERTY_CHANGE,
	EVENT_ON_INFO_SHUTDOWN,
	EVENT_GRAPH_ORDER
};

enum {
	TYPE_AUDIO	= 0,
	TYPE_MIDI		= 1,
#ifdef JACK_HAS_METADATA_API
	TYPE_OSC		= 2,
	TYPE_CV			= 3,
#endif
	TYPE_MAX
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
	};
};

struct _app_t {
	// UI
	int w;
	int h;
	int type;
	Evas_Object *win;
	Evas_Object *patcher;
	Evas_Object *popup;
	Evas_Object *tools;
	Evas_Object *list;
	Elm_Genlist_Item_Class *clientitc;
	Elm_Genlist_Item_Class *sourceitc;
	Elm_Genlist_Item_Class *sinkitc;
	Elm_Genlist_Item_Class *sepitc;
	Elm_Genlist_Item_Class *portitc;
	Elm_Object_Item *tool_audio;
	Elm_Object_Item *tool_midi;
#ifdef JACK_HAS_METADATA_API
	Elm_Object_Item *tool_osc;
	Elm_Object_Item *tool_cv;
#endif

	// JACK
	jack_client_t *client;
	Eina_List *events;
	Ecore_Timer *timer;
#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid;
#endif

	// SQLite3
	sqlite3 *db;

	sqlite3_stmt *query_client_add;
	sqlite3_stmt *query_client_del;
	sqlite3_stmt *query_client_find_by_name;
#ifdef JACK_HAS_METADATA_API
	sqlite3_stmt *query_client_find_by_uuid;
#endif
	sqlite3_stmt *query_client_find_by_id;
	sqlite3_stmt *query_client_get_selected;
	sqlite3_stmt *query_client_set_selected;
	sqlite3_stmt *query_client_set_pretty;
	sqlite3_stmt *query_client_set_position;

	sqlite3_stmt *query_port_add;
	sqlite3_stmt *query_port_del;
	sqlite3_stmt *query_port_find_by_name;
#ifdef JACK_HAS_METADATA_API
	sqlite3_stmt *query_port_find_by_uuid;
#endif
	sqlite3_stmt *query_port_find_by_id;
	sqlite3_stmt *query_port_get_selected;
	sqlite3_stmt *query_port_set_selected;
	sqlite3_stmt *query_port_info;
	sqlite3_stmt *query_port_set_pretty;
	sqlite3_stmt *query_port_set_type;

	sqlite3_stmt *query_connection_add;
	sqlite3_stmt *query_connection_del;
	sqlite3_stmt *query_connection_get;

	sqlite3_stmt *query_port_list;
	sqlite3_stmt *query_client_port_list;
	sqlite3_stmt *query_client_port_count;

	bool populating;
};

static void _ui_refresh_single(app_t *app, int i);
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
			"name TEXT UNIQUE ON CONFLICT REPLACE,"
			"pretty_name TEXT,"
			"selected BOOL,"
			"uuid UNSIGNED BIG INT,"
			"position INTEGER);"
			""
		"CREATE TABLE Ports ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT UNIQUE ON CONFLICT REPLACE,"
			"short_name TEXT,"
			"pretty_name TEXT,"
			"client_id INT,"
			"type_id INT,"
			"direction_id INT,"
			"selected BOOL,"
			"uuid UNSIGNED BIG INT,"
			"terminal BOOL,"
			"physical BOOL,"
			"position INTEGER);"
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
		"INSERT INTO Types (id, name, selected) VALUES (2, 'OSC', 1);"
		"INSERT INTO Types (id, name, selected) VALUES (3, 'CV', 1);"
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
		"INSERT INTO Clients (name, pretty_name, uuid, position, selected) VALUES ($1, $2, $3, $4, 1)",
		-1, &app->query_client_add, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Clients WHERE id=$1",
		-1, &app->query_client_del, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Clients WHERE name=$1",
		-1, &app->query_client_find_by_name, NULL);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Clients WHERE uuid=$1",
		-1, &app->query_client_find_by_uuid, NULL);
	(void)ret;
#endif
	ret = sqlite3_prepare_v2(app->db,
		"SELECT name, pretty_name FROM Clients WHERE id=$1",
		-1, &app->query_client_find_by_id, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"SELECT selected FROM Clients WHERE id=$1",
		-1, &app->query_client_get_selected, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Clients SET selected=$1 WHERE id=$2",
		-1, &app->query_client_set_selected, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Clients SET pretty_name=$1 WHERE id=$2",
		-1, &app->query_client_set_pretty, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Clients SET position=$1 WHERE id=$2",
		-1, &app->query_client_set_position, NULL);
	(void)ret;

	// Port
	ret = sqlite3_prepare_v2(app->db,
		"INSERT INTO Ports (name, client_id, short_name, pretty_name, type_id, direction_id, uuid, terminal, physical, position, selected) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, 1)",
		-1, &app->query_port_add, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Ports WHERE id=$1",
		-1, &app->query_port_del, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE name=$1",
		-1, &app->query_port_find_by_name, NULL);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE uuid=$1",
		-1, &app->query_port_find_by_uuid, NULL);
	(void)ret;
#endif
	ret = sqlite3_prepare_v2(app->db,
		"SELECT name, short_name, pretty_name FROM Ports WHERE id=$1",
		-1, &app->query_port_find_by_id, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Ports SET selected=$1 WHERE id=$2",
		-1, &app->query_port_set_selected, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT selected FROM Ports WHERE id=$1",
		-1, &app->query_port_get_selected, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT type_id, direction_id, client_id FROM Ports WHERE id=$1",
		-1, &app->query_port_info, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Ports SET pretty_name=$1 WHERE id=$2",
		-1, &app->query_port_set_pretty, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Ports SET type_id=$1 WHERE id=$2",
		-1, &app->query_port_set_type, NULL);
	(void)ret;

	ret = sqlite3_prepare_v2(app->db,
		"INSERT INTO Connections (source_id, sink_id) VALUES ($1, $2)",
		-1, &app->query_connection_add, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"DELETE FROM Connections WHERE source_id=$1 AND sink_id=$2",
		-1, &app->query_connection_del, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Connections WHERE source_id=$1 AND sink_id=$2",
		-1, &app->query_connection_get, NULL);
	(void)ret;

	// port list
	ret = sqlite3_prepare_v2(app->db,
		"SELECT Ports.id, Ports.client_id FROM Ports INNER JOIN (Types, Directions, Clients) "
			"ON Ports.selected=1 "
			"AND Ports.type_id=Types.id "
			"AND Ports.direction_id=Directions.id "
			"AND Ports.client_id=Clients.id "
			"AND Clients.selected=1 "
			"AND Types.id=$1 "
			"AND Directions.id=$2 "
			"ORDER BY Ports.terminal=Ports.direction_id, Clients.position, Ports.position, Ports.short_name",
		-1, &app->query_port_list, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE client_id=$1 AND direction_id=$2 ORDER BY position, type_id, short_name",
		-1, &app->query_client_port_list, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT COUNT(id) FROM Ports WHERE client_id=$1",
		-1, &app->query_client_port_count, NULL);
	(void)ret;

	return 0;
}

static void
_db_deinit(app_t *app)
{
	int ret;

	if(!app->db)
		return;

	ret = sqlite3_finalize(app->query_client_add);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_del);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_find_by_name);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_finalize(app->query_client_find_by_uuid);
	(void)ret;
#endif
	ret = sqlite3_finalize(app->query_client_find_by_id);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_get_selected);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_set_selected);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_set_pretty);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_set_position);
	(void)ret;

	ret = sqlite3_finalize(app->query_port_add);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_del);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_find_by_name);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_finalize(app->query_port_find_by_uuid);
	(void)ret;
#endif
	ret = sqlite3_finalize(app->query_port_find_by_id);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_get_selected);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_set_selected);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_info);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_set_pretty);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_set_type);
	(void)ret;

	ret = sqlite3_finalize(app->query_connection_add);
	(void)ret;
	ret = sqlite3_finalize(app->query_connection_del);
	(void)ret;
	ret = sqlite3_finalize(app->query_connection_get);
	(void)ret;

	ret = sqlite3_finalize(app->query_port_list);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_port_list);
	(void)ret;
	ret = sqlite3_finalize(app->query_client_port_count);
	(void)ret;

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
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return id;
}

#ifdef JACK_HAS_METADATA_API
static int
_db_client_find_by_uuid(app_t *app, jack_uuid_t uuid)
{
	int ret;
	int id;

	sqlite3_stmt *stmt = app->query_client_find_by_uuid;

	ret = sqlite3_bind_int64(stmt, 1, uuid);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return id;
}
#endif

static void
_db_client_find_by_id(app_t *app, int id, char **name, char **pretty_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_find_by_id;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

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
	(void)ret;
}

static void
_db_client_add(app_t *app, const char *name)
{
	int ret;

	char *value = NULL;
	char *type = NULL;

#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid;

	const char *uuid_str = jack_get_uuid_for_client_name(app->client, name);
	if(uuid_str)
		jack_uuid_parse(uuid_str, &uuid);
	else
		jack_uuid_clear(&uuid);

	if(!jack_uuid_empty(uuid))
		jack_get_property(uuid, JACK_METADATA_PRETTY_NAME, &value, &type);
#endif

	sqlite3_stmt *stmt = app->query_client_add;

	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_text(stmt, 2, value ? value : name, -1, NULL);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_bind_int64(stmt, 3, uuid);
#else
	ret = sqlite3_bind_int64(stmt, 3, 0);
#endif
	(void)ret;
	ret = sqlite3_bind_int(stmt, 4, elm_genlist_items_count(app->list) - 1);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

	if(value)
		free(value);
	if(type)
		free(type);

	int *id = calloc(1, sizeof(int));
	*id = _db_client_find_by_name(app, name);
	Elm_Object_Item *elmnt = elm_genlist_item_append(app->list, app->clientitc, id, NULL,
		ELM_GENLIST_ITEM_TREE, NULL, NULL);
	elm_genlist_item_expanded_set(elmnt, EINA_TRUE);
}

static void
_db_client_del(app_t *app, const char *name)
{
	int ret;

	int id = _db_client_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_client_del;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->clientitc)
			continue; // ignore port items

		int *ref = elm_object_item_data_get(itm);
		if(ref && (*ref == id) )
		{
			elm_object_item_del(itm);
			break;
		}
	}
}

static int
_db_client_get_selected(app_t *app, int id)
{
	int ret;
	int selected;

	sqlite3_stmt *stmt = app->query_client_get_selected;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		selected = sqlite3_column_int(stmt, 0);
	else
		selected = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return selected;
}

static void
_db_client_set_selected(app_t *app, int id, int selected)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_set_selected;

	ret = sqlite3_bind_int(stmt, 1, selected);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_client_set_pretty(app_t *app, int id, const char *pretty_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_set_pretty;

	ret = sqlite3_bind_text(stmt, 1, pretty_name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_client_set_position(app_t *app, int id, int position)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_set_position;

	ret = sqlite3_bind_int(stmt, 1, position);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_port_add(app_t *app, const char *client_name, const char *name,
	const char *short_name)
{
	int ret;

	jack_port_t *port = jack_port_by_name(app->client, name);
	int client_id = _db_client_find_by_name(app, client_name);
	int midi = !strcmp(jack_port_type(port), JACK_DEFAULT_MIDI_TYPE) ? 1 : 0;
	int type_id = midi ? TYPE_MIDI : TYPE_AUDIO;
	int flags = jack_port_flags(port);
	int direction_id = flags & JackPortIsInput ? 1 : 0;
	int terminal_id = flags & JackPortIsTerminal ? 1 : 0;
	int physical_id = flags & JackPortIsPhysical ? 1 : 0;
	int position = 0;

	char *value = NULL;
	char *type = NULL;

#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid = jack_port_uuid(port);
	if(!jack_uuid_empty(uuid))
	{
		if(type_id == 0) // signal-type
		{
			jack_get_property(uuid, "http://jackaudio.org/metadata/signal-type", &value, &type);
			if(value && !strcmp(value, "CV"))
			{
				type_id = TYPE_CV;
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
				type_id = TYPE_OSC;
				free(value);
			}
			if(type)
				free(type);
		}

		value = NULL;
		type = NULL;

		jack_get_property(uuid, "http://jackaudio.org/metadata/order", &value, &type);
		if(value)
		{
			position = atoi(value);
			free(value);
		}
		if(type)
				free(type);

		jack_get_property(uuid, JACK_METADATA_PRETTY_NAME, &value, &type);
	}
#endif

	sqlite3_stmt *stmt = app->query_port_add;

	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, client_id);
	(void)ret;
	ret = sqlite3_bind_text(stmt, 3, short_name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_text(stmt, 4, value ? value : short_name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 5, type_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 6, direction_id);
	(void)ret;
#ifdef JACK_HAS_METADATA_API
	ret = sqlite3_bind_int64(stmt, 7, uuid);
#else
	ret = sqlite3_bind_int64(stmt, 7, 0);
#endif
	(void)ret;
	ret = sqlite3_bind_int(stmt, 8, terminal_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 9, physical_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 10, position);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

	if(value)
		free(value);
	if(type)
		free(type);

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if( (itc != app->sourceitc) && (itc != app->sinkitc) )
			continue; // ignore port items

		int *ref = elm_object_item_data_get(itm);
		if(ref && (*ref == client_id) )
		{
			if(!app->populating && elm_genlist_item_expanded_get(itm))
			{
				elm_genlist_item_expanded_set(itm, EINA_FALSE);
				elm_genlist_item_expanded_set(itm, EINA_TRUE);
			}
		}
	}
}

static int
_db_port_find_by_name(app_t *app, const char *name)
{
	int ret;
	int id;

	sqlite3_stmt *stmt = app->query_port_find_by_name;

	ret = sqlite3_bind_text(stmt, 1, name, -1, NULL);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return id;
}

#ifdef JACK_HAS_METADATA_API
static int
_db_port_find_by_uuid(app_t *app, jack_uuid_t uuid)
{
	int ret;
	int id;

	sqlite3_stmt *stmt = app->query_port_find_by_uuid;

	ret = sqlite3_bind_int64(stmt, 1, uuid);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return id;
}
#endif

static void
_db_port_find_by_id(app_t *app, int id, char **name, char **short_name, char **pretty_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_find_by_id;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		if(name)
			*name = strdup((const char *)sqlite3_column_text(stmt, 0));
		if(short_name)
			*short_name = strdup((const char *)sqlite3_column_text(stmt, 1));
		if(pretty_name)
			*pretty_name = strdup((const char *)sqlite3_column_text(stmt, 2));
	}
	else
	{
		if(name)
			*name = NULL;
		if(short_name)
			*short_name = NULL;
		if(pretty_name)
			*pretty_name = NULL;
	}

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static int
_db_port_get_selected(app_t *app, int id)
{
	int ret;
	int selected;

	sqlite3_stmt *stmt = app->query_port_get_selected;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		selected = sqlite3_column_int(stmt, 0);
	else
		selected = -1;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return selected;
}

static void
_db_port_set_selected(app_t *app, int id, int selected)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_set_selected;

	ret = sqlite3_bind_int(stmt, 1, selected);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_port_get_info(app_t *app, int id, int *type, int *direction, int *client_id)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_info;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		if(type)
			*type = sqlite3_column_int(stmt, 0);
		if(direction)
			*direction = sqlite3_column_int(stmt, 1);
		if(client_id)
			*client_id = sqlite3_column_int(stmt, 2);
	}
	else
	{
		if(type)
			*type = -1;
		if(direction)
			*direction = -1;
		if(client_id)
			*client_id = -1;
	}

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_port_set_pretty(app_t *app, int id, const char *pretty_name)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_set_pretty;

	ret = sqlite3_bind_text(stmt, 1, pretty_name, -1, NULL);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_port_set_type(app_t *app, int id, int type_id)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_set_type;

	ret = sqlite3_bind_int(stmt, 1, type_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_port_del(app_t *app, const char *client_name, const char *name,
	const char *short_name)
{
	int ret;

	int id = _db_port_find_by_name(app, name);

	sqlite3_stmt *stmt = app->query_port_del;

	ret = sqlite3_bind_int(stmt, 1, id);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->portitc)
			continue; // ignore client items

		int *ref = elm_object_item_data_get(itm);
		if(ref && (*ref == id) )
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
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id_sink);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
}

static void
_db_connection_del(app_t *app, const char *name_source, const char *name_sink)
{
	int ret;

	int id_source = _db_port_find_by_name(app, name_source);
	int id_sink = _db_port_find_by_name(app, name_sink);

	sqlite3_stmt *stmt = app->query_connection_del;

	ret = sqlite3_bind_int(stmt, 1, id_source);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id_sink);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;
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
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, id_sink);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
		connected = 1;
	else
		connected = 0;

	ret = sqlite3_reset(stmt);
	(void)ret;

	return connected;
}

static Eina_Bool
_jack_timer_cb(void *data)
{
	app_t *app = data;

	int refresh = 0;
	int realize = 0;
	int done = 0;

	event_t *ev;
	EINA_LIST_FREE(app->events, ev)
	{
		//printf("_jack_timer_cb: %i\n", ev->type);

		switch(ev->type)
		{
			case EVENT_CLIENT_REGISTER:
			{
				//printf("client_register: %s %i\n", ev->client_register.name,
				//	ev->client_register.state);

				if(ev->client_register.state)
					_db_client_add(app, ev->client_register.name);
				else
					_db_client_del(app, ev->client_register.name);

				if(ev->client_register.name)
					free(ev->client_register.name); // strdup

				refresh = 1;

				break;
			}
			case EVENT_PORT_REGISTER:
			{
				const jack_port_t *port = jack_port_by_id(app->client, ev->port_register.id);
				const char *name = jack_port_name(port);
				char *sep = strchr(name, ':');
				char *client_name = strndup(name, sep - name);
				const char *short_name = sep + 1;

				//printf("port_register: %s %i\n", name, ev->port_register.state);

				if(client_name)
				{
					if(ev->port_register.state)
						_db_port_add(app, client_name, name, short_name);
					else
						_db_port_del(app, client_name, name, short_name);

					free(client_name); // strdup
				}

				refresh = 1;

				break;
			}
			case EVENT_PORT_CONNECT:
			{
				const jack_port_t *port_source = jack_port_by_id(app->client, ev->port_connect.id_source);
				const jack_port_t *port_sink = jack_port_by_id(app->client, ev->port_connect.id_sink);
				const char *name_source = jack_port_name(port_source);
				const char *name_sink = jack_port_name(port_sink);

				//printf("port_connect: %s %s %i\n", name_source, name_sink,
				//	ev->port_connect.state);

				if(ev->port_connect.state)
					_db_connection_add(app, name_source, name_sink);
				else
					_db_connection_del(app, name_source, name_sink);

				realize = 1;

				break;
			}
#ifdef JACK_HAS_METADATA_API
			case EVENT_PROPERTY_CHANGE:
			{
				//printf("property_change: %lu %s %i\n", ev->property_change.uuid,
				//	ev->property_change.key, ev->property_change.state);

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
						if(!jack_uuid_empty(ev->property_change.uuid))
						{
							jack_get_property(ev->property_change.uuid,
								ev->property_change.key, &value, &type);

							if(value)
							{
								if(!strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									int id;
									if((id = _db_client_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_client_set_pretty(app, id, value);
									else if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_pretty(app, id, value);
								}
								else if(!strcmp(ev->property_change.key, "http://jackaudio.org/metadata/event-types"))
								{
									int id;
									int type_id = strstr(value, "OSC") ? TYPE_OSC : TYPE_MIDI;
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_type(app, id, type_id);
								}
								else if(!strcmp(ev->property_change.key, "http://jackaudio.org/metadata/signal-type"))
								{
									int id;
									int type_id = !strcmp(value, "CV") ? TYPE_CV : TYPE_AUDIO;
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_type(app, id, type_id);
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
							int id;
							if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
							{
								//printf("property_delete port: %i %s\n", id, ev->property_change.key);

								jack_port_t *port = jack_port_by_id(app->client, id);
								int midi = 0;
								if(port) //FIXME
									midi = !strcmp(jack_port_type(port), JACK_DEFAULT_MIDI_TYPE) ? 1 : 0;
								int type_id = midi ? TYPE_MIDI : TYPE_AUDIO;

								int needs_port_update = 0;
								int needs_pretty_update = 0;

								if(  ev->property_change.key
									&& ( !strcmp(ev->property_change.key, "http://jackaudio.org/metadata/signal-type")
										|| !strcmp(ev->property_change.key, "http://jackaudio.org/metadata/event-types") ) )
								{
									needs_port_update = 1;
								}
								else if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									needs_pretty_update = 1;
								}
								else // all keys removed
								{
									needs_port_update = 1;
									needs_pretty_update = 1;
								}

								if(needs_port_update)
								{
									_db_port_set_type(app, id, type_id);
								}

								if(needs_pretty_update)
								{
									char *short_name = NULL;
									_db_port_find_by_id(app, id, NULL, &short_name, NULL);
									if(short_name)
									{
										_db_port_set_pretty(app, id, short_name);
										free(short_name);
									}
								}
							}
							else if ((id = _db_client_find_by_uuid(app, ev->property_change.uuid)) != -1)
							{
								//printf("property_delete client: %i %s\n", id, ev->property_change.key);
								
								int needs_pretty_update = 0;

								if(ev->property_change.key
									&& !strcmp(ev->property_change.key, JACK_METADATA_PRETTY_NAME))
								{
									needs_pretty_update = 1;
								}
								else // all keys removed
								{
									needs_pretty_update = 1;
								}

								if(needs_pretty_update)
								{
									char *name = NULL;
									_db_client_find_by_id(app, id, &name, NULL);
									if(name)
									{
										_db_client_set_pretty(app, id, name);
										free(name);
									}
								}
							}
						}
						else
						{
							fprintf(stderr, "all properties in current JACK session deleted\n");
							//TODO
						}
					}
				}

				if(ev->property_change.key)
					free(ev->property_change.key); // strdup

				refresh = 1;

				break;
			}
#endif
			case EVENT_ON_INFO_SHUTDOWN:
			{
				app->client = NULL; // JACK has shut down, hasn't it?
				done = 1;

				break;
			}
			case EVENT_GRAPH_ORDER:
			{
				//FIXME

				break;
			}
		};

		free(ev);
	}

	if(refresh)
		_ui_refresh(app);
	else if(realize)
		_ui_realize(app);

	if(done)
		elm_exit();

	app->timer = NULL;

	return ECORE_CALLBACK_CANCEL;
}

static void
_jack_async(void *data)
{
	event_t *ev = data;
	app_t *app = ev->app;

	app->events = eina_list_append(app->events, ev);

	if(app->timer)
		ecore_timer_reset(app->timer);
	else
		app->timer = ecore_timer_loop_add(0.1, _jack_timer_cb, app);
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
_jack_sample_rate_cb(jack_nframes_t nframes, void *arg)
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
	app_t *app = arg;

	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_GRAPH_ORDER;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);

	return 0;
}

#ifdef JACK_HAS_METADATA_API
static void
_jack_property_change_cb(jack_uuid_t uuid, const char *key, jack_property_change_t state, void *arg)
{
	app_t *app = arg;

	//printf("_jack_property_change_cb: %lu %s %i\n", uuid, key, state);

	event_t *ev = malloc(sizeof(event_t));
	ev->app = app;
	ev->type = EVENT_PROPERTY_CHANGE;
	ev->property_change.uuid = uuid;
	ev->property_change.key = key ? strdup(key) : NULL;
	ev->property_change.state = state;

	ecore_main_loop_thread_safe_call_async(_jack_async, ev);
}
#endif

static int
_jack_init(app_t *app)
{
	jack_status_t status;

	app->client = jack_client_open("patchmatrix", JackNullOption, &status);
	if(!app->client)
		return -1;

#ifdef JACK_HAS_METADATA_API
	const char *client_name = jack_get_client_name(app->client);
	const char *uuid_str = jack_get_uuid_for_client_name(app->client, client_name);
	if(uuid_str)
		jack_uuid_parse(uuid_str, &app->uuid);
	else
		jack_uuid_clear(&app->uuid);

	if(!jack_uuid_empty(app->uuid))
	{
		jack_set_property(app->client, app->uuid,
			JACK_METADATA_PRETTY_NAME, "Pathmatrix", "text/plain");
	}
#endif

	jack_on_info_shutdown(app->client, _jack_on_info_shutdown_cb, app);

	jack_set_freewheel_callback(app->client, _jack_freewheel_cb, app);
	jack_set_buffer_size_callback(app->client, _jack_buffer_size_cb, app);
	jack_set_sample_rate_callback(app->client, _jack_sample_rate_cb, app);

	jack_set_client_registration_callback(app->client, _jack_client_registration_cb, app);
	jack_set_port_registration_callback(app->client, _jack_port_registration_cb, app);
	//jack_set_port_rename_callback(app->client, _jack_port_rename_cb, app);
	jack_set_port_connect_callback(app->client, _jack_port_connect_cb, app);
	jack_set_xrun_callback(app->client, _jack_xrun_cb, app);
	jack_set_graph_order_callback(app->client, _jack_graph_order_cb, app);
#ifdef JACK_HAS_METADATA_API
	jack_set_property_change_callback(app->client, _jack_property_change_cb, app);
#endif

	jack_activate(app->client);

	return 0;
}

static void
_jack_deinit(app_t *app)
{
	if(app->timer)
		ecore_timer_del(app->timer);

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

	char *source_name = NULL;
	char *sink_name = NULL;
	_db_port_find_by_id(app, source_id, &source_name, NULL, NULL);
	_db_port_find_by_id(app, sink_id, &sink_name, NULL, NULL);
	if(!source_name || !sink_name)
		return;

	//printf("connect_request: %s %s\n", source_name, sink_name);

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

	char *source_name = NULL;
	char *sink_name = NULL;
	_db_port_find_by_id(app, source_id, &source_name, NULL, NULL);
	_db_port_find_by_id(app, sink_id, &sink_name, NULL, NULL);
	if(!source_name || !sink_name)
		return;

	//printf("disconnect_request: %s %s\n", source_name, sink_name);

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

	char *source_name = NULL;
	char *sink_name = NULL;
	 _db_port_find_by_id(app, source_id, &source_name, NULL, NULL);
	 _db_port_find_by_id(app, sink_id, &sink_name, NULL, NULL);
	if(!source_name || !sink_name)
		return;

	int connected = _db_connection_get(app, source_name, sink_name);

	//printf("realize_request: %s %s %i\n", source_name, sink_name, connected);

	patcher_object_connected_set(obj, source_id, sink_id, connected, 0);

	free(source_name);
	free(sink_name);
}

static void
_client_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	int *id = data;
	app_t *app = evas_object_data_get(lay, "app");

	int selected = _db_client_get_selected(app, *id);
	selected ^= 1; // toggle
	elm_layout_signal_emit(lay, selected ? "link,on" : "link,off", "");

	_db_client_set_selected(app, *id, selected);
	_ui_refresh(app);
}

static Evas_Object *
_ui_client_list_content_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	int selected = _db_client_get_selected(app, *id);

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *lay = elm_layout_add(obj);
		if(lay)
		{
			elm_layout_file_set(lay, PATCHMATRIX_DATA_DIR"/patchmatrix.edj",
				"/patchmatrix/list/client");
			evas_object_data_set(lay, "app", app);
			evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(lay);
		
			// color
			char msg [7];
			sprintf(msg, "col,%02i", *id % 20);
			elm_layout_signal_emit(lay, msg, "/patchmatrix/list/ui");

			// link
			elm_layout_signal_callback_add(lay, "link,toggle", "", _client_link_toggle, id);
			elm_layout_signal_emit(lay, selected ? "link,on" : "link,off", "");

			// name/pretty_name
			char *name = NULL;
			char *pretty_name = NULL;;
			_db_client_find_by_id(app, *id, &name, &pretty_name);

			if(pretty_name)
			{
				elm_object_part_text_set(lay, "elm.text", pretty_name);
				free(pretty_name);
			}
			if(name)
			{
				if(!pretty_name)
					elm_object_part_text_set(lay, "elm.text", name);
				free(name);
			}

			return lay;
		}
	}

	return NULL;
}

static void
_ui_client_list_del(void *data, Evas_Object *obj)
{
	int *id = data;

	free(id);
}

static Evas_Object *
_ui_source_list_content_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *lay = elm_layout_add(obj);
		if(lay)
		{
			elm_layout_file_set(lay, PATCHMATRIX_DATA_DIR"/patchmatrix.edj",
				"/patchmatrix/list/group");
			evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(lay);
		
			// color
			char msg [7];
			sprintf(msg, "col,%02i", *id % 20);
			elm_layout_signal_emit(lay, msg, "/patchmatrix/list/ui");

			// group
			elm_object_part_text_set(lay, "elm.text", "Inputs");

			return lay;
		}
	}

	return NULL;
}

static Evas_Object *
_ui_sink_list_content_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *lay = elm_layout_add(obj);
		if(lay)
		{
			elm_layout_file_set(lay, PATCHMATRIX_DATA_DIR"/patchmatrix.edj",
				"/patchmatrix/list/group");
			evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(lay);
		
			// color
			char msg [7];
			sprintf(msg, "col,%02i", *id % 20);
			elm_layout_signal_emit(lay, msg, "/patchmatrix/list/ui");

			// group
			elm_object_part_text_set(lay, "elm.text", "Outputs");

			return lay;
		}
	}

	return NULL;
}

static void
_port_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	int *id = data;
	app_t *app = evas_object_data_get(lay, "app");

	int selected = _db_port_get_selected(app, *id);
	selected ^= 1; // toggle
	elm_layout_signal_emit(lay, selected ? "link,on" : "link,off", "");

	_db_port_set_selected(app, *id, selected);
	_ui_refresh(app);
}

static Evas_Object *
_ui_port_list_content_get(void *data, Evas_Object *obj, const char *part)
{
	int *id = data;
	app_t *app = evas_object_data_get(obj, "app");

	if(!id || !app)
		return NULL;

	int type;
	int direction;
	int client_id;
	_db_port_get_info(app, *id, &type, &direction, &client_id);
	int selected = _db_port_get_selected(app, *id);

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *lay = elm_layout_add(obj);
		if(lay)
		{
			elm_layout_file_set(lay, PATCHMATRIX_DATA_DIR"/patchmatrix.edj",
				"/patchmatrix/list/port");
			evas_object_data_set(lay, "app", app);
			evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(lay);

			// link
			elm_layout_signal_callback_add(lay, "link,toggle", "", _port_link_toggle, id);
			elm_layout_signal_emit(lay, selected ? "link,on" : "link,off", "");
			
			// type
			switch(type)
			{
				case TYPE_AUDIO:
					elm_layout_signal_emit(lay, "type,audio", "");
					break;
				case TYPE_MIDI:
					elm_layout_signal_emit(lay, "type,midi", "");
					break;
#ifdef JACK_HAS_METADATA_API
				case TYPE_OSC:
					elm_layout_signal_emit(lay, "type,osc", "");
					break;
				case TYPE_CV:
					elm_layout_signal_emit(lay, "type,cv", "");
					break;
#endif
				default:
					elm_layout_signal_emit(lay, "type,hide", "");
					break;
			}

			// name/pretty_name
			char *short_name = NULL;
			char *pretty_name = NULL;;
			_db_port_find_by_id(app, *id, NULL, &short_name, &pretty_name);

			if(pretty_name)
			{
				elm_object_part_text_set(lay, "elm.text", pretty_name);
				free(pretty_name);
			}
			if(short_name)
			{
				if(!pretty_name)
					elm_object_part_text_set(lay, "elm.text", short_name);
				free(short_name);
			}

			return lay;
		}
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

	if(!itm)
		return;

	int *id = elm_object_item_data_get(itm);
	const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
	Evas_Object *lay = elm_object_item_part_content_get(itm, "elm.swallow.content");

	if(!id || !itc || !lay)
		return;

	if(itc == app->clientitc)
		_client_link_toggle(id, lay, NULL, NULL);
	else if(itc == app->portitc)
		_port_link_toggle(id, lay, NULL, NULL);
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
	const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
	app_t *app = data;

	int *client_id = elm_object_item_data_get(itm);

	if(itc == app->clientitc)
	{
		Elm_Object_Item *elmnt;

		elmnt = elm_genlist_item_append(app->list, app->sourceitc,
			client_id, itm, ELM_GENLIST_ITEM_TREE, NULL, NULL);
		elm_genlist_item_expanded_set(elmnt, EINA_TRUE);

		elmnt = elm_genlist_item_append(app->list, app->sinkitc,
			client_id, itm, ELM_GENLIST_ITEM_TREE, NULL, NULL);
		elm_genlist_item_expanded_set(elmnt, EINA_TRUE);

		elm_genlist_item_append(app->list, app->sepitc,
			NULL, itm, ELM_GENLIST_ITEM_NONE, NULL, NULL);
	}
	else if(itc == app->sourceitc)
	{
		sqlite3_stmt *stmt = app->query_client_port_list;

		sqlite3_bind_int(stmt, 1, *client_id); // client_id
		sqlite3_bind_int(stmt, 2, 0); // sources

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
	else if(itc == app->sinkitc)
	{
		sqlite3_stmt *stmt = app->query_client_port_list;

		sqlite3_bind_int(stmt, 1, *client_id); // client_id
		sqlite3_bind_int(stmt, 2, 1); // sinks

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
}

static void
_ui_list_contracted(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	elm_genlist_item_subitems_clear(itm);
}

static void
_ui_list_moved(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	app_t *app = data;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);
	if(class == app->clientitc)
	{
		//printf("_ui_list_moved: client\n");

		// update client positions
		int pos = 0;
		for(Elm_Object_Item *itm2 = elm_genlist_first_item_get(app->list);
			itm2 != NULL;
			itm2 = elm_genlist_item_next_get(itm2))
		{
			const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm2);
			if(itc != app->clientitc) // is not a client
				continue; // skip 

			int *id = elm_object_item_data_get(itm2);
			_db_client_set_position(app, *id, pos++);
		}

		_ui_refresh(app);
	}
	else if(class == app->portitc)
	{
		//printf("_ui_list_moved: port\n");
	}
}

static Eina_Bool
_config_changed(void *data, int ev_type, void *ev)
{
	app_t *app = data;

	//FIXME
	//elm_gengrid_item_size_set(app->grid, ELM_SCALE_SIZE(384), ELM_SCALE_SIZE(384));

	return ECORE_CALLBACK_PASS_ON;
}

static void
_toolbar_selected(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;
	Elm_Object_Item *itm = event_info;

	if(itm == app->tool_audio)
	{
		app->type = TYPE_AUDIO;
		_ui_refresh(app);
	}
	else if(itm == app->tool_midi)
	{
		app->type = TYPE_MIDI;
		_ui_refresh(app);
	}
#ifdef JACK_HAS_METADATA_API
	else if(itm == app->tool_osc)
	{
		app->type = TYPE_OSC;
		_ui_refresh(app);
	}
	else if(itm == app->tool_cv)
	{
		app->type = TYPE_CV;
		_ui_refresh(app);
	}
#endif
}

static void
_menu_close(void *data, Evas_Object *obj, void *event_info)
{
	elm_exit();
}

static void
_menu_about(void *data, Evas_Object *obj, void *event_info)
{
	app_t *app = data;

	if(evas_object_visible_get(app->popup))
		evas_object_hide(app->popup);
	else
		evas_object_show(app->popup);
}

static void
_key_down(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
	app_t *app = data;
	const Evas_Event_Key_Down *ev = event_info;

	const Eina_Bool cntrl = evas_key_modifier_is_set(ev->modifiers, "Control");
	
	if(cntrl)
	{
		if(!strcmp(ev->key, "q"))
			_menu_close(app, NULL, NULL);
		else if(!strcmp(ev->key, "h"))
			_menu_about(app, NULL, NULL);
		else if(!strcmp(ev->key, "a"))
			elm_toolbar_item_selected_set(app->tool_audio, EINA_TRUE);
		else if(!strcmp(ev->key, "m"))
			elm_toolbar_item_selected_set(app->tool_midi, EINA_TRUE);
#ifdef JACK_HAS_METADATA_API
		else if(!strcmp(ev->key, "o"))
			elm_toolbar_item_selected_set(app->tool_osc, EINA_TRUE);
		else if(!strcmp(ev->key, "c"))
			elm_toolbar_item_selected_set(app->tool_cv, EINA_TRUE);
#endif
	}
}

static int
_ui_init(app_t *app)
{
	// UI
	app->w = 1024;
	app->h = 420;
	app->type = TYPE_AUDIO;

	app->clientitc = elm_genlist_item_class_new();
	if(app->clientitc)
	{
		app->clientitc->item_style = "full";
		app->clientitc->func.text_get = NULL;
		app->clientitc->func.content_get = _ui_client_list_content_get;
		app->clientitc->func.state_get = NULL;
		app->clientitc->func.del = _ui_client_list_del;
	}

	app->sourceitc = elm_genlist_item_class_new();
	if(app->sourceitc)
	{
		app->sourceitc->item_style = "full";
		app->sourceitc->func.text_get = NULL;
		app->sourceitc->func.content_get = _ui_source_list_content_get;
		app->sourceitc->func.state_get = NULL;
		app->sourceitc->func.del = NULL;
	}

	app->sinkitc = elm_genlist_item_class_new();
	if(app->sinkitc)
	{
		app->sinkitc->item_style = "full";
		app->sinkitc->func.text_get = NULL;
		app->sinkitc->func.content_get = _ui_sink_list_content_get;
		app->sinkitc->func.state_get = NULL;
		app->sinkitc->func.del = NULL;
	}

	app->sepitc = elm_genlist_item_class_new();
	if(app->sepitc)
	{
		app->sepitc->item_style = "default";
		app->sepitc->func.text_get = NULL;
		app->sepitc->func.content_get = NULL;
		app->sepitc->func.state_get = NULL;
		app->sepitc->func.del = NULL;
	}

	app->portitc = elm_genlist_item_class_new();
	if(app->portitc)
	{
		app->portitc->item_style = "full";
		app->portitc->func.text_get = NULL;
		app->portitc->func.content_get = _ui_port_list_content_get;
		app->portitc->func.state_get = NULL;
		app->portitc->func.del = _ui_port_list_del;
	}

	app->win = elm_win_util_standard_add("PatchMatrix", "PatchMarix");
	if(!app->win)
		return -1;

	evas_object_smart_callback_add(app->win, "delete,request", _ui_delete_request, app);
	ecore_event_handler_add(ELM_EVENT_CONFIG_ALL_CHANGED, _config_changed, app);
	evas_object_resize(app->win, app->w, app->h);
	evas_object_show(app->win);

	evas_object_event_callback_add(app->win, EVAS_CALLBACK_KEY_DOWN, _key_down, app);

	const Eina_Bool exclusive = EINA_FALSE;
	const Evas_Modifier_Mask ctrl_mask = evas_key_modifier_mask_get(
		evas_object_evas_get(app->win), "Control");
	if(!evas_object_key_grab(app->win, "q", ctrl_mask, 0, exclusive)) // quit
		fprintf(stderr, "could not grab 'q' key\n");
	if(!evas_object_key_grab(app->win, "h", ctrl_mask, 0, exclusive)) // about
		fprintf(stderr, "could not grab 'h' key\n");
	if(!evas_object_key_grab(app->win, "a", ctrl_mask, 0, exclusive)) // AUDIO
		fprintf(stderr, "could not grab 'a' key\n");
	if(!evas_object_key_grab(app->win, "m", ctrl_mask, 0, exclusive)) // MIDI
		fprintf(stderr, "could not grab 'm' key\n");
#ifdef JACK_HAS_METADATA_API
	if(!evas_object_key_grab(app->win, "o", ctrl_mask, 0, exclusive)) // OSC
		fprintf(stderr, "could not grab 'o' key\n");
	if(!evas_object_key_grab(app->win, "c", ctrl_mask, 0, exclusive)) // CV
		fprintf(stderr, "could not grab 'c' key\n");
#endif

	Evas_Object *mainmenu = elm_win_main_menu_get(app->win);
	if(mainmenu)
	{
		evas_object_show(mainmenu);

		Elm_Object_Item *elmnt;

		elmnt = elm_menu_item_add(mainmenu, NULL, "application-exit", "Quit", _menu_close, app);
		elm_object_item_tooltip_text_set(elmnt, "Ctrl+Q");

		elmnt = elm_menu_item_add(mainmenu, NULL, "help-about", "About", _menu_about, app);
		elm_object_item_tooltip_text_set(elmnt, "Ctrl+H");
	}

	app->popup = elm_popup_add(app->win);
	if(app->popup)
	{
		elm_popup_allow_events_set(app->popup, EINA_TRUE);
		elm_popup_timeout_set(app->popup, 0.f);

		Evas_Object *hbox = elm_box_add(app->popup);
		if(hbox)
		{
			elm_box_horizontal_set(hbox, EINA_TRUE);
			elm_box_homogeneous_set(hbox, EINA_FALSE);
			elm_box_padding_set(hbox, 10, 10);
			evas_object_show(hbox);
			elm_object_content_set(app->popup, hbox);

			Evas_Object *icon = elm_icon_add(hbox);
			if(icon)
			{
				elm_image_file_set(icon, PATCHMATRIX_DATA_DIR"/omk_logo_256x256.png", NULL);
				evas_object_size_hint_min_set(icon, 128, 128);
				evas_object_size_hint_max_set(icon, 256, 256);
				evas_object_size_hint_aspect_set(icon, EVAS_ASPECT_CONTROL_BOTH, 1, 1);
				evas_object_show(icon);
				elm_box_pack_end(hbox, icon);
			}

			Evas_Object *label2 = elm_label_add(hbox);
			if(label2)
			{
				elm_object_text_set(label2,
					"<color=#b00 shadow_color=#fff font_size=20>"
					"PatchMatrix - a JACK Patchbay"
					"</color></br><align=left>"
					"Version "PATCHMATRIX_VERSION"</br></br>"
					"Copyright (c) 2015 Hanspeter Portner</br></br>"
					"This is free and libre software</br>"
					"Released under Artistic License 2.0</br>"
					"By Open Music Kontrollers</br></br>"
					"<color=#bbb>"
					"https://github.com/OpenMusicKontrollers/patchmatrix</br>"
					"dev@open-music-kontrollers.ch"
					"</color></align>");

				evas_object_show(label2);
				elm_box_pack_end(hbox, label2);
			}
		}
	}

	Evas_Object *pane = elm_panes_add(app->win);
	if(pane)
	{
		elm_panes_horizontal_set(pane, EINA_FALSE);
		elm_panes_content_left_size_set(pane, 0.7);
		evas_object_size_hint_weight_set(pane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(pane, EVAS_HINT_FILL, EVAS_HINT_FILL);
		elm_win_resize_object_add(app->win, pane);
		evas_object_show(pane);

		Evas_Object *hbox = elm_box_add(pane);
		if(hbox)
		{
			elm_box_horizontal_set(hbox, EINA_TRUE);
			elm_box_homogeneous_set(hbox, EINA_FALSE);
			elm_box_padding_set(hbox, 10, 0);
			evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(hbox);

			elm_object_part_content_set(pane, "left", hbox);

			app->patcher = patcher_object_add(pane);
			if(app->patcher)
			{
				evas_object_data_set(app->patcher, "app", app);
				evas_object_smart_callback_add(app->patcher, "connect,request",
					_ui_connect_request, app);
				evas_object_smart_callback_add(app->patcher, "disconnect,request",
					_ui_disconnect_request, app);
				evas_object_smart_callback_add(app->patcher, "realize,request",
					_ui_realize_request, app);
				evas_object_size_hint_weight_set(app->patcher, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(app->patcher, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(app->patcher);

				elm_box_pack_end(hbox, app->patcher);

				_ui_refresh(app);
			} // app->patcher

			app->tools = elm_toolbar_add(hbox);
			if(app->tools)
			{
				elm_toolbar_horizontal_set(app->tools, EINA_FALSE);
				elm_toolbar_homogeneous_set(app->tools, EINA_TRUE);
				elm_toolbar_align_set(app->tools, 0.f);
				elm_toolbar_select_mode_set(app->tools, ELM_OBJECT_SELECT_MODE_ALWAYS);
				evas_object_smart_callback_add(app->tools, "selected",
					_toolbar_selected, app);
				evas_object_size_hint_weight_set(app->tools, 0.f, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(app->tools, 0.f, EVAS_HINT_FILL);
				evas_object_show(app->tools);

				app->tool_audio = elm_toolbar_item_append(app->tools,
					PATCHMATRIX_DATA_DIR"/audio.png", "AUDIO", NULL, NULL);
				elm_toolbar_item_selected_set(app->tool_audio, EINA_TRUE);
				elm_object_item_tooltip_text_set(app->tool_audio, "Ctrl+A");

				app->tool_midi = elm_toolbar_item_append(app->tools,
					PATCHMATRIX_DATA_DIR"/midi.png", "MIDI", NULL, NULL);
				elm_object_item_tooltip_text_set(app->tool_midi, "Ctrl+M");

#ifdef JACK_HAS_METADATA_API
				app->tool_osc = elm_toolbar_item_append(app->tools,
					PATCHMATRIX_DATA_DIR"/osc.png", "OSC", NULL, NULL);
				elm_object_item_tooltip_text_set(app->tool_osc, "Ctrl+O");

				app->tool_cv = elm_toolbar_item_append(app->tools,
					PATCHMATRIX_DATA_DIR"/cv.png", "CV", NULL, NULL);
				elm_object_item_tooltip_text_set(app->tool_cv, "Ctrl+C");
#endif

				elm_box_pack_end(hbox, app->tools);
			} // app->tools
		} // hbox

		app->list = elm_genlist_add(hbox);
		if(app->list)
		{
			elm_genlist_reorder_mode_set(app->list, EINA_TRUE);
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
			evas_object_smart_callback_add(app->list, "moved",
				_ui_list_moved, app);
			evas_object_data_set(app->list, "app", app);
			evas_object_size_hint_weight_set(app->list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(app->list, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(app->list);

			elm_object_part_content_set(pane, "right", app->list);
		} // app->list
	} // pane

	return 0;
}

static void
_ui_deinit(app_t *app)
{
	if(app->clientitc)
		elm_genlist_item_class_free(app->clientitc);
	if(app->sourceitc)
		elm_genlist_item_class_free(app->sourceitc);
	if(app->sinkitc)
		elm_genlist_item_class_free(app->sinkitc);
	if(app->sepitc)
		elm_genlist_item_class_free(app->sepitc);
	if(app->portitc)
		elm_genlist_item_class_free(app->portitc);

	if(app->win)
		evas_object_del(app->win);
}

static void
_ui_populate(app_t *app)
{
	elm_genlist_clear(app->list);

	const char **sources = jack_get_ports(app->client, NULL, NULL, JackPortIsOutput);
	const char **sinks = jack_get_ports(app->client, NULL, NULL, JackPortIsInput);

	app->populating = true;

	if(sources)
	{
		for(const char **source=sources; *source; source++)
		{
			char *sep = strchr(*source, ':');
			char *client_name = strndup(*source, sep - *source);
			const char *short_name = sep + 1;

			if(client_name)
			{
				if(_db_client_find_by_name(app, client_name) < 0)
					_db_client_add(app, client_name);

				_db_port_add(app, client_name, *source, short_name);
				free(client_name);
			}
		}
	}

	if(sinks)
	{
		for(const char **sink=sinks; *sink; sink++)
		{
			char *sep = strchr(*sink, ':');
			char *client_name = strndup(*sink, sep - *sink);
			const char *short_name = sep + 1;

			if(client_name)
			{
				if(_db_client_find_by_name(app, client_name) < 0)
					_db_client_add(app, client_name);

				_db_port_add(app, client_name, *sink, short_name);
				free(client_name);
			}
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

	app->populating = false;

	for(Elm_Object_Item *itm = elm_genlist_first_item_get(app->list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != app->clientitc)
			continue; // ignore non-client items

		if(elm_genlist_item_expanded_get(itm))
		{
			elm_genlist_item_expanded_set(itm, EINA_FALSE);
			elm_genlist_item_expanded_set(itm, EINA_TRUE);
		}
	}
}

// update single grid and connections
static void
_ui_refresh_single(app_t *app, int i)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_list;

	if(!app->patcher)
		return;

	ret = sqlite3_bind_int(stmt, 1, i); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 0); // source
	(void)ret;
	int num_sources = 0;
	while(sqlite3_step(stmt) != SQLITE_DONE)
	{
		num_sources += 1;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	ret = sqlite3_bind_int(stmt, 1, i); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 1); // sink
	(void)ret;
	int num_sinks = 0;
	while(sqlite3_step(stmt) != SQLITE_DONE)
	{
		num_sinks += 1;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	patcher_object_dimension_set(app->patcher, num_sources, num_sinks);

	ret = sqlite3_bind_int(stmt, 1, i); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 0); // source
	(void)ret;
	int last_client_id = -1;
	for(int source=0; source<num_sources; source++)
	{
		ret = sqlite3_step(stmt);
		(void)ret;
		int id = sqlite3_column_int(stmt, 0);
		int client_id = sqlite3_column_int(stmt, 1);
		char *port_pretty_name = NULL;
		char *client_pretty_name = NULL;
		_db_port_find_by_id(app, id, NULL, NULL, &port_pretty_name);
		_db_client_find_by_id(app, client_id, NULL, &client_pretty_name);

		patcher_object_source_id_set(app->patcher, source, id);
		patcher_object_source_color_set(app->patcher, source, client_id % 20);

		if(port_pretty_name)
		{
			patcher_object_source_label_set(app->patcher, source, port_pretty_name);
			free(port_pretty_name);
		}

		if(client_pretty_name)
		{
			if( (last_client_id == -1) || (last_client_id != client_id) )
				patcher_object_source_group_set(app->patcher, source, client_pretty_name);
			else
				patcher_object_source_group_set(app->patcher, source, "");
			free(client_pretty_name);
		}

		last_client_id = client_id;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	ret = sqlite3_bind_int(stmt, 1, i); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 1); // sink
	(void)ret;
	last_client_id = -1;
	for(int sink=0; sink<num_sinks; sink++)
	{
		ret = sqlite3_step(stmt);
		(void)ret;
		int id = sqlite3_column_int(stmt, 0);
		int client_id = sqlite3_column_int(stmt, 1);
		char *port_pretty_name;
		char *client_pretty_name;
		_db_port_find_by_id(app, id, NULL, NULL, &port_pretty_name);
		_db_client_find_by_id(app, client_id, NULL, &client_pretty_name);

		patcher_object_sink_id_set(app->patcher, sink, id);
		patcher_object_sink_color_set(app->patcher, sink, client_id % 20);

		if(port_pretty_name)
		{
			patcher_object_sink_label_set(app->patcher, sink, port_pretty_name);
			free(port_pretty_name);
		}

		if(client_pretty_name)
		{
			if( (last_client_id == -1) || (last_client_id != client_id) )
				patcher_object_sink_group_set(app->patcher, sink, client_pretty_name);
			else
				patcher_object_sink_group_set(app->patcher, sink, "");
			free(client_pretty_name);
		}

		last_client_id = client_id;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	patcher_object_realize(app->patcher);
}

// update all grids and connections
static void
_ui_refresh(app_t *app)
{
	_ui_refresh_single(app, app->type);
}

// update connections
static void
_ui_realize(app_t *app)
{
	if(app->patcher)
		patcher_object_realize(app->patcher);
}

static int
elm_main(int argc, char **argv)
{
	static app_t app;

	elm_config_accel_preference_set("gl");
	elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

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
