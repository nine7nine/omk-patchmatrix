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

#include <sqlite3.h>

#include <jack/jack.h>
#include <jack/session.h>
#ifdef JACK_HAS_METADATA_API
#	include <jack/uuid.h>
#	include <jack/metadata.h>
#	include <jackey.h>
#endif

#include <lv2/lv2plug.in/ns/ext/port-groups/port-groups.h>

#include <varchunk.h>

#define NK_PUGL_IMPLEMENTATION
#include <nk_pugl.h>

#define NK_PATCHER_IMPLEMENTATION
#include <nk_patcher.h>

#if 0
# define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
# define debugf(...) {}
#endif

typedef struct _app_t app_t;
typedef struct _event_t event_t;

enum {
	EVENT_CLIENT_REGISTER,
	EVENT_PORT_REGISTER,
	EVENT_PORT_CONNECT,
	EVENT_PROPERTY_CHANGE,
	EVENT_ON_INFO_SHUTDOWN,
	EVENT_GRAPH_ORDER,
	EVENT_SESSION
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

enum {
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

struct _event_t {
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

		struct {
			jack_session_event_t *event;
		} session;
	};
};

struct _app_t {
	// UI
	int type;
	int designation;

	// JACK
	jack_client_t *client;
#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid;
#endif

	// varchunk
	varchunk_t *from_jack;

	// SQLite3
	sqlite3 *db;

	sqlite3_stmt *query_client_add;
	sqlite3_stmt *query_client_del;
	sqlite3_stmt *query_client_find_by_name;
#ifdef JACK_HAS_METADATA_API
	sqlite3_stmt *query_client_find_by_uuid;
#endif
	sqlite3_stmt *query_client_find_by_id;
	sqlite3_stmt *query_client_find_all_itr;
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
	sqlite3_stmt *query_port_find_all_itr;
	sqlite3_stmt *query_port_get_selected;
	sqlite3_stmt *query_port_set_selected;
	sqlite3_stmt *query_port_info;
	sqlite3_stmt *query_port_set_pretty;
	sqlite3_stmt *query_port_set_type;
	sqlite3_stmt *query_port_set_position;
	sqlite3_stmt *query_port_set_designation;

	sqlite3_stmt *query_connection_add;
	sqlite3_stmt *query_connection_del;
	sqlite3_stmt *query_connection_get;

	sqlite3_stmt *query_port_list;
	sqlite3_stmt *query_client_port_list;
	sqlite3_stmt *query_client_port_count;

	bool populating;
	const char *server_name;
	const char *session_id;

	bool done;

	nk_pugl_window_t win;
	nk_patcher_t patch;
#ifdef JACK_HAS_METADATA_API
	struct nk_image icons [4];
#else
	struct nk_image icons [2];
#endif

	int source_n;
	int sink_n;
};

static void _ui_refresh_single(app_t *app, int type, int designation);
static void _ui_refresh(app_t *app);
static void _ui_realize(app_t *app);

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
	return colors[n % COLOR_N];
}

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
			"position INTEGER,"
			"designation INTEGER);"
			""
		"CREATE TABLE Connections ("
			"id INTEGER PRIMARY KEY,"
			"source_id INT,"
			"sink_id INT);"
			""
		"CREATE TABLE Types ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT);"
			""
		"CREATE TABLE Designations ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT);"
			""
		"CREATE TABLE Directions ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT);"
			""
		"INSERT INTO Types (id, name) VALUES (0, 'AUDIO');"
		"INSERT INTO Types (id, name) VALUES (1, 'MIDI');"
		"INSERT INTO Types (id, name) VALUES (2, 'OSC');"
		"INSERT INTO Types (id, name) VALUES (3, 'CV');"
			""
		"INSERT INTO Designations (id, name) VALUES (0, 'none');"
		"INSERT INTO Designations (id, name) VALUES (1, 'Left');"
		"INSERT INTO Designations (id, name) VALUES (2, 'Right');"
		"INSERT INTO Designations (id, name) VALUES (3, 'Center');"
		"INSERT INTO Designations (id, name) VALUES (4, 'Side');"
		"INSERT INTO Designations (id, name) VALUES (5, 'Center Left');"
		"INSERT INTO Designations (id, name) VALUES (6, 'Center Right');"
		"INSERT INTO Designations (id, name) VALUES (7, 'Side Left');"
		"INSERT INTO Designations (id, name) VALUES (8, 'Side Right');"
		"INSERT INTO Designations (id, name) VALUES (9, 'Rear Left');"
		"INSERT INTO Designations (id, name) VALUES (10, 'Rear Right');"
		"INSERT INTO Designations (id, name) VALUES (11, 'Rear Center');"
		"INSERT INTO Designations (id, name) VALUES (12, 'Low Frequency Effects');"
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
		"SELECT id FROM Clients",
		-1, &app->query_client_find_all_itr, NULL);
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
		"INSERT INTO Ports (name, client_id, short_name, pretty_name, type_id, direction_id, uuid, terminal, physical, position, designation, selected) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, 1)",
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
		"SELECT id FROM Ports WHERE client_id=$1 AND direction_id=$2",
		-1, &app->query_port_find_all_itr, NULL);
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
		"SELECT type_id, direction_id, client_id, terminal, physical FROM Ports WHERE id=$1",
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
		"UPDATE Ports SET position=$1 WHERE id=$2",
		-1, &app->query_port_set_position, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"UPDATE Ports SET designation=$1 WHERE id=$2",
		-1, &app->query_port_set_designation, NULL);
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
		"SELECT Ports.id, Ports.client_id FROM Ports INNER JOIN (Types, Directions, Clients, Designations) "
			"ON Ports.selected=1 "
			"AND Ports.type_id=Types.id "
			"AND Ports.direction_id=Directions.id "
			"AND Ports.client_id=Clients.id "
			"AND Ports.designation=Designations.id "
			"AND Clients.selected=1 "
			"AND Types.id=$1 "
			"AND Directions.id=$2 "
			"AND ($3=0 OR Designations.id=$3) " // NOTE designation=0 matches all designations
			"ORDER BY Ports.terminal=Ports.direction_id, Clients.position, Ports.position, Ports.short_name",
		-1, &app->query_port_list, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT id FROM Ports WHERE client_id=$1 AND direction_id=$2 ORDER BY position, type_id, short_name",
		-1, &app->query_client_port_list, NULL);
	(void)ret;
	ret = sqlite3_prepare_v2(app->db,
		"SELECT COUNT(id) FROM Ports WHERE client_id=$1 AND direction_id=$2",
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
	ret = sqlite3_finalize(app->query_client_find_all_itr);
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
	ret = sqlite3_finalize(app->query_port_find_all_itr);
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
	ret = sqlite3_finalize(app->query_port_set_position);
	(void)ret;
	ret = sqlite3_finalize(app->query_port_set_designation);
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

static int
_db_client_find_all_itr(app_t *app)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_find_all_itr;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		return sqlite3_column_int(stmt, 0);
	}
	else
	{
		ret = sqlite3_reset(stmt);
		(void)ret;

		return 0;
	}
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

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

	if(value)
		free(value);
	if(type)
		free(type);
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
	int designation = DESIGNATION_NONE;

	char *value = NULL;
	char *type = NULL;

#ifdef JACK_HAS_METADATA_API
	jack_uuid_t uuid = jack_port_uuid(port);
	if(!jack_uuid_empty(uuid))
	{
		if(type_id == 0) // signal-type
		{
			jack_get_property(uuid, JACKEY_SIGNAL_TYPE, &value, &type);
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
			jack_get_property(uuid, JACKEY_EVENT_TYPES, &value, &type);
			if(value && strstr(value, "OSC"))
			{
				type_id = TYPE_OSC;
				free(value);
			}
			if(type)
				free(type);
		}

		value = type = NULL;
		jack_get_property(uuid, JACKEY_ORDER, &value, &type);
		if(value)
		{
			position = atoi(value);
			free(value);
		}
		if(type)
			free(type);

		value = type = NULL;
		jack_get_property(uuid, JACKEY_DESIGNATION, &value, &type);
		if(value)
		{
			designation = _designation_get(value);
			free(value);
		}
		if(type)
			free(type);

		value = type = NULL;
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
	ret = sqlite3_bind_int(stmt, 11, designation);
	(void)ret;

	ret = sqlite3_step(stmt);
	(void)ret;

	ret = sqlite3_reset(stmt);
	(void)ret;

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
_db_port_find_all_itr(app_t *app, int client_id, int direction)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_find_all_itr;

	ret = sqlite3_bind_int(stmt, 1, client_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, direction);
	(void)ret;

	ret = sqlite3_step(stmt);

	if(ret != SQLITE_DONE)
	{
		return sqlite3_column_int(stmt, 0);
	}
	else
	{
		ret = sqlite3_reset(stmt);
		(void)ret;

		return 0;
	}
}

static int
_db_port_count(app_t *app, int client_id, int direction)
{
	int ret;

	sqlite3_stmt *stmt = app->query_client_port_count;

	ret = sqlite3_bind_int(stmt, 1, client_id);
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, direction);
	(void)ret;

	ret = sqlite3_step(stmt);

	int count = 0;

	if(ret != SQLITE_DONE)
	{
		count = sqlite3_column_int(stmt, 0);
	}

	ret = sqlite3_reset(stmt);
	(void)ret;

	return count;
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
_db_port_get_info(app_t *app, int id, int *type, int *direction, int *client_id,
	bool *terminal, bool *physical)
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
		if(terminal)
			*terminal = sqlite3_column_int(stmt, 3) ? true : false;
		if(physical)
			*physical = sqlite3_column_int(stmt, 4) ? true : false;
	}
	else
	{
		if(type)
			*type = -1;
		if(direction)
			*direction = -1;
		if(client_id)
			*client_id = -1;
		if(terminal)
			*terminal = false;
		if(physical)
			*physical = false;
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
_db_port_set_position(app_t *app, int id, int position)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_set_position;

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
_db_port_set_designation(app_t *app, int id, int designation)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_set_designation;

	ret = sqlite3_bind_int(stmt, 1, designation);
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
		printf("-> %s\n", sub);

		mkdir(sub, mode);
		chdir(sub);
	}

	chdir(cwd);

	free(p);

	return ret;
}

static void
_jack_anim(app_t *app)
{
	bool refresh = false;
	bool realize = false;
	bool done = false;

	const event_t *ev;
	size_t len;
	while((ev = varchunk_read_request(app->from_jack, &len)))
	{
		//printf("_jack_anim: %i\n", ev->type);

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

				refresh = true;

				break;
			}
			case EVENT_PORT_REGISTER:
			{
				const jack_port_t *port = jack_port_by_id(app->client, ev->port_register.id);
				if(port)
				{
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
				}

				refresh = true;

				break;
			}
			case EVENT_PORT_CONNECT:
			{
				const jack_port_t *port_source = jack_port_by_id(app->client, ev->port_connect.id_source);
				const jack_port_t *port_sink = jack_port_by_id(app->client, ev->port_connect.id_sink);
				if(port_source && port_sink)
				{
					const char *name_source = jack_port_name(port_source);
					const char *name_sink = jack_port_name(port_sink);

					//printf("port_connect: %s %s %i\n", name_source, name_sink,
					//	ev->port_connect.state);

					if(ev->port_connect.state)
						_db_connection_add(app, name_source, name_sink);
					else
						_db_connection_del(app, name_source, name_sink);
				}

				realize = true;

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
								else if(!strcmp(ev->property_change.key, JACKEY_EVENT_TYPES))
								{
									int id;
									int type_id = strstr(value, "OSC") ? TYPE_OSC : TYPE_MIDI;
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_type(app, id, type_id);
								}
								else if(!strcmp(ev->property_change.key, JACKEY_SIGNAL_TYPE))
								{
									int id;
									int type_id = !strcmp(value, "CV") ? TYPE_CV : TYPE_AUDIO;
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_type(app, id, type_id);
								}
								else if(!strcmp(ev->property_change.key, JACKEY_ORDER))
								{
									int id;
									int position = atoi(value);
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_position(app, id, position);
								}
								else if(!strcmp(ev->property_change.key, JACKEY_DESIGNATION))
								{
									int id;
									int designation = _designation_get(value);
									if((id = _db_port_find_by_uuid(app, ev->property_change.uuid)) != -1)
										_db_port_set_designation(app, id, designation);
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

								if(needs_position_update)
								{
									_db_port_set_position(app, id, 0); //TODO or rather use id?
								}

								if(needs_designation_update)
								{
									_db_port_set_designation(app, id, DESIGNATION_NONE);
								}
							}
							else if ((id = _db_client_find_by_uuid(app, ev->property_change.uuid)) != -1)
							{
								//printf("property_delete client: %i %s\n", id, ev->property_change.key);
								
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

				refresh = true;

				break;
			}
#endif
			case EVENT_ON_INFO_SHUTDOWN:
			{
				app->client = NULL; // JACK has shut down, hasn't it?
				done = true;

				break;
			}
			case EVENT_GRAPH_ORDER:
			{
				//FIXME

				break;
			}
			case EVENT_SESSION:
			{
				jack_session_event_t *jev = ev->session.event;
				
				printf("_session_signal: %s %s %s\n",
					jev->session_dir, jev->client_uuid, jev->command_line);

				// path may not exist yet
				mkdirp(jev->session_dir, S_IRWXU | S_IRGRP |  S_IXGRP | S_IROTH | S_IXOTH);

				asprintf(&jev->command_line, "patchmatrix -u %s ${SESSION_DIR}",
					jev->client_uuid);

				switch(jev->type)
				{
					case JackSessionSaveAndQuit:
						done = true;
						break;
					case JackSessionSave:
						break;
					case JackSessionSaveTemplate:
						break;
				}

				jack_session_reply(app->client, jev);
				jack_session_event_free(jev);
			}
		};

		varchunk_read_advance(app->from_jack);
	}

	if(refresh)
		_ui_refresh(app);
	else if(realize)
		_ui_realize(app);

	app->done = done;
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
		/* FIXME
		nk_pugl_signal_expose(&app->win);
		*/
	}
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

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_CLIENT_REGISTER;
		ev->client_register.name = strdup(name);
		ev->client_register.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
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
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
	}
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

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PORT_CONNECT;
		ev->port_connect.id_source = id_source;
		ev->port_connect.id_sink = id_sink;
		ev->port_connect.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
	}
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

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_GRAPH_ORDER;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
	}

	return 0;
}

#ifdef JACK_HAS_METADATA_API
static void
_jack_property_change_cb(jack_uuid_t uuid, const char *key, jack_property_change_t state, void *arg)
{
	app_t *app = arg;

	//printf("_jack_property_change_cb: %lu %s %i\n", uuid, key, state);

	event_t *ev;
	if((ev = varchunk_write_request(app->from_jack, sizeof(event_t))))
	{
		ev->type = EVENT_PROPERTY_CHANGE;
		ev->property_change.uuid = uuid;
		ev->property_change.key = key ? strdup(key) : NULL;
		ev->property_change.state = state;

		varchunk_write_advance(app->from_jack, sizeof(event_t));
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
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
		/*FIXME
		nk_pugl_signal_expose(&app->win);
		*/
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
	const char *uuid_str = jack_get_uuid_for_client_name(app->client, client_name);
	if(uuid_str)
		jack_uuid_parse(uuid_str, &app->uuid);
	else
		jack_uuid_clear(&app->uuid);

	if(!jack_uuid_empty(app->uuid))
	{
		jack_set_property(app->client, app->uuid,
			JACK_METADATA_PRETTY_NAME, "PatchMatrix", "text/plain");
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
	jack_set_session_callback(app->client, _jack_session_cb, app);

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

static int
_ui_init(app_t *app)
{
	nk_patcher_init(&app->patch, 0, 0);

	// UI
	app->type = TYPE_AUDIO;
	app->designation = DESIGNATION_NONE;

	nk_pugl_init(&app->win);
	nk_pugl_show(&app->win);

	app->icons[0] = nk_pugl_icon_load(&app->win, PATCHMATRIX_DATA_DIR"/audio.png");
	app->icons[1] = nk_pugl_icon_load(&app->win, PATCHMATRIX_DATA_DIR"/midi.png");
#ifdef JACK_HAS_METADATA_API
	app->icons[2] = nk_pugl_icon_load(&app->win, PATCHMATRIX_DATA_DIR"/osc.png");
	app->icons[3] = nk_pugl_icon_load(&app->win, PATCHMATRIX_DATA_DIR"/cv.png");
#endif

	return 0;
}

static void
_ui_deinit(app_t *app)
{
	nk_pugl_icon_unload(&app->win, app->icons[0]);
	nk_pugl_icon_unload(&app->win, app->icons[1]);
#ifdef JACK_HAS_METADATA_API
	nk_pugl_icon_unload(&app->win, app->icons[2]);
	nk_pugl_icon_unload(&app->win, app->icons[3]);
#endif

	nk_pugl_hide(&app->win);
	nk_pugl_shutdown(&app->win);

	nk_patcher_deinit(&app->patch);
}

static void
_ui_populate(app_t *app)
{
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
}

// update single grid and connections
static void
_ui_refresh_single(app_t *app, int type, int designation)
{
	int ret;

	sqlite3_stmt *stmt = app->query_port_list;

	ret = sqlite3_bind_int(stmt, 1, type); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 0); // source
	(void)ret;
	ret = sqlite3_bind_int(stmt, 3, designation);
	(void)ret;
	int num_sources = 0;
	while(sqlite3_step(stmt) != SQLITE_DONE)
	{
		num_sources += 1;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	ret = sqlite3_bind_int(stmt, 1, type); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 1); // sink
	(void)ret;
	ret = sqlite3_bind_int(stmt, 3, designation);
	(void)ret;
	int num_sinks = 0;
	while(sqlite3_step(stmt) != SQLITE_DONE)
	{
		num_sinks += 1;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	nk_patcher_deinit(&app->patch);
	nk_patcher_init(&app->patch, num_sources, num_sinks);

	ret = sqlite3_bind_int(stmt, 1, type); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 0); // source
	(void)ret;
	ret = sqlite3_bind_int(stmt, 3, designation);
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

		nk_patcher_source_id_set(&app->patch, source, id);
		nk_patcher_source_color_set(&app->patch, source, _color_get(client_id));

		if(port_pretty_name)
		{
			nk_patcher_source_label_set(&app->patch, source, port_pretty_name);
			free(port_pretty_name);
		}

		if(client_pretty_name)
		{
			/* FIXME
			if( (last_client_id == -1) || (last_client_id != client_id) )
				nk_patch_source_group_set(&app->patch, source, client_pretty_name);
			else
				nk_patch_source_group_set(&app->patch, source, "");
			*/
			free(client_pretty_name);
		}

		last_client_id = client_id;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	ret = sqlite3_bind_int(stmt, 1, type); // type
	(void)ret;
	ret = sqlite3_bind_int(stmt, 2, 1); // sink
	(void)ret;
	ret = sqlite3_bind_int(stmt, 3, designation);
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

		nk_patcher_sink_id_set(&app->patch, sink, id);
		nk_patcher_sink_color_set(&app->patch, sink, _color_get(client_id));

		if(port_pretty_name)
		{
			nk_patcher_sink_label_set(&app->patch, sink, port_pretty_name);
			free(port_pretty_name);
		}

		if(client_pretty_name)
		{
			/* FIXME
			if( (last_client_id == -1) || (last_client_id != client_id) )
				nk_patch_sink_group_set(&app->patch, sink, client_pretty_name);
			else
				nk_patch_sink_group_set(&app->patch, sink, "");
			*/
			free(client_pretty_name);
		}

		last_client_id = client_id;
	}
	ret = sqlite3_reset(stmt);
	(void)ret;

	//FIXME
	for(int source_idx=0; source_idx<app->patch.source_n; source_idx++)
	{
		const intptr_t source_id = app->patch.sources[source_idx].id;
		char *source_name = NULL;
		_db_port_find_by_id(app, source_id, &source_name, NULL, NULL);

		for(int sink_idx=0; sink_idx<app->patch.sink_n; sink_idx++)
		{
			const intptr_t sink_id = app->patch.sinks[sink_idx].id;
			char *sink_name = NULL;
			_db_port_find_by_id(app, sink_id,  &sink_name, NULL, NULL);

			const bool connected = _db_connection_get(app, source_name, sink_name);
			nk_patcher_connected_set(&app->patch, source_id, sink_id, connected, NK_PATCHER_TYPE_DIRECT);
		}
	}

	nk_pugl_post_redisplay(&app->win);
}

// update all grids and connections
static void
_ui_refresh(app_t *app)
{
	_ui_refresh_single(app, app->type, app->designation);
}

// update connections
static void
_ui_realize(app_t *app)
{
	nk_pugl_post_redisplay(&app->win);
}

static inline int
_expose_direction(struct nk_context *ctx, app_t *app, int direction)
{
	const int dy = 25;
	int count = 0;

	struct nk_style *style = &ctx->style;
	const struct nk_color tab_border_color = style->tab.border_color;

	for(int client_id = _db_client_find_all_itr(app);
		client_id;
		client_id = _db_client_find_all_itr(app))
	{
		char *client_name = NULL;
		char *client_pretty_name = NULL;
		_db_client_find_by_id(app, client_id, &client_name, &client_pretty_name);

		if(_db_port_count(app, client_id, direction) == 0)
			continue; // ignore

		style->tab.border_color = _color_get(client_id);

		int client_sel = _db_client_get_selected(app, client_id);
		nk_layout_row_dynamic(ctx, dy, 1);
		if( (client_sel = nk_tree_push_id(ctx, NK_TREE_TAB, client_pretty_name, client_sel ? NK_MAXIMIZED : NK_MINIMIZED, client_id) == NK_MAXIMIZED) )
		{
			for(int port_id = _db_port_find_all_itr(app, client_id, direction);
				port_id;
				port_id = _db_port_find_all_itr(app, client_id, direction))
			{
				char *port_name = NULL;
				char *port_short_name = NULL;
				char *port_pretty_name = NULL;
				_db_port_find_by_id(app, port_id, &port_name, &port_short_name, &port_pretty_name);

				int port_sel = _db_port_get_selected(app, port_id);
				int type = TYPE_AUDIO;
				_db_port_get_info(app, port_id, &type, NULL, NULL, NULL, NULL);
				port_sel = nk_select_image_label(ctx, app->icons[type], port_pretty_name, NK_TEXT_LEFT, port_sel);
				_db_port_set_selected(app, port_id, port_sel);

				count += 1;
			}

			nk_tree_pop(ctx);
		}
		_db_client_set_selected(app, client_id, client_sel);
	}

	style->tab.border_color = tab_border_color;
	return count;
}

static inline void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	app_t *app = data;

	_ui_refresh(app); //FIXME

	const struct nk_vec2 group_padding = nk_panel_get_padding(&ctx->style, NK_PANEL_GROUP);
	const float dy = 25;

	if(nk_begin(ctx, "Base", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		const struct nk_panel *base = nk_window_get_panel(ctx);
		const struct nk_vec2 size = nk_window_get_size(ctx);
		if( (size.x != wbounds.w) || (size.y != wbounds.h) )
			nk_window_set_size(ctx, nk_vec2(wbounds.w, wbounds.h));

		float base_h;
		{
			struct nk_rect bounds = base->bounds;
			bounds.x += group_padding.x;
			bounds.y += group_padding.y;
			bounds.w -= 2*group_padding.x;
			bounds.h -= 2*group_padding.y;

			base_h = bounds.h;
		}

		nk_layout_row_begin(ctx, NK_DYNAMIC, base_h, 3);

		nk_layout_row_push(ctx, 0.25);
		if(nk_group_begin(ctx, "Sources", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
		{
			app->source_n = _expose_direction(ctx, app, 0);

			nk_group_end(ctx);
		}

		nk_layout_row_push(ctx, 0.5);
		if(nk_group_begin(ctx, "Connections", NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
		{
			const struct nk_panel *center = nk_window_get_panel(ctx);
#ifdef JACK_HAS_METADATA_API
			nk_layout_row_dynamic(ctx, dy, 4);
#else
			nk_layout_row_dynamic(ctx, dy, 2);
#endif

			app->type = nk_button_symbol_label(ctx,
				app->type == TYPE_AUDIO ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE, "AUDIO", NK_TEXT_RIGHT)
				? TYPE_AUDIO : app->type;
			app->type = nk_button_symbol_label(ctx,
				app->type == TYPE_MIDI ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE, "MIDI", NK_TEXT_RIGHT)
				? TYPE_MIDI : app->type;
#ifdef JACK_HAS_METADATA_API
			app->type = nk_button_symbol_label(ctx,
				app->type == TYPE_OSC ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE, "OSC", NK_TEXT_RIGHT)
				? TYPE_OSC : app->type;
			app->type = nk_button_symbol_label(ctx,
				app->type == TYPE_CV ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_CIRCLE_OUTLINE, "CV", NK_TEXT_RIGHT)
				? TYPE_CV : app->type;
#endif

			struct nk_rect bounds = center->bounds;
			bounds.x += group_padding.x;
			bounds.y += group_padding.y;
			bounds.w -= 4*group_padding.x;
			bounds.h -= 4*group_padding.y + dy;

			nk_layout_row_dynamic(ctx, bounds.h, 1);
			const nk_patcher_event_t *ev = nk_patcher_render(&app->patch, ctx, bounds);
			if(ev)
			{
				//printf("ev: %lu %lu %i\n", ev->source_id, ev->sink_id, ev->state);

				char *source_name = NULL;
				char *sink_name = NULL;
				_db_port_find_by_id(app, ev->source_id, &source_name, NULL, NULL);
				_db_port_find_by_id(app, ev->sink_id, &sink_name, NULL, NULL);
				if(source_name && sink_name)
				{
					if(ev->state)
						jack_connect(app->client, source_name, sink_name);
					else
						jack_disconnect(app->client, source_name, sink_name);
				}
			}

			nk_group_end(ctx);
		}

		nk_layout_row_push(ctx, 0.25);
		if(nk_group_begin(ctx, "Sinks", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
		{
			app->sink_n = _expose_direction(ctx, app, 1);

			nk_group_end(ctx);
		}

		nk_layout_row_end(ctx);

		nk_end(ctx);
	}
}

int
main(int argc, char **argv)
{
	static app_t app;

	app.server_name = NULL;
	app.session_id = NULL;

	//FIXME
	nk_patcher_priv_t *priv = &app.patch.priv;
	priv->scale = 0.45;
	priv->ax = -1;
	priv->ay = -1;
	priv->sx = 0;
	priv->sy = 0;

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

	nk_pugl_config_t *cfg = &app.win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "patchmatrix";
	cfg->title = "PatchMatrix";
	cfg->parent = 0;
	cfg->data = &app;
	cfg->expose = _expose;

	cfg->font.face = PATCHMATRIX_DATA_DIR"/Cousine-Regular.ttf";
	cfg->font.size = 13;

	if(!(app.from_jack = varchunk_new(0x10000, true)))
		goto cleanup;

	if(_jack_init(&app))
		goto cleanup;

	if(_db_init(&app))
		goto cleanup;

	if(_ui_init(&app))
		goto cleanup;

	_ui_populate(&app);
	_ui_refresh(&app);

	while(!app.done)
	{
		//FIXME
		//nk_pugl_wait_for_event(&app.win);
		_jack_anim(&app);
		if(nk_pugl_process_events(&app.win))
			app.done = true;
		usleep(40000); //25FPS
	}

cleanup:
	_ui_deinit(&app);
	_db_deinit(&app);
	_jack_deinit(&app);
	if(app.from_jack)
		varchunk_free(app.from_jack);

	return 0;
}
