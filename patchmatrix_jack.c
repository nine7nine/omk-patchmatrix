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

#include <patchmatrix_jack.h>
#include <patchmatrix_db.h>
#include <patchmatrix_nk.h>

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

static int
_mkdirp(const char* path, mode_t mode)
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

int
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

bool
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
										_client_sort(client);
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
									_client_sort(client2);
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
				_mkdirp(jev->session_dir, S_IRWXU | S_IRGRP |  S_IXGRP | S_IROTH | S_IXOTH);

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
							_client_sort(client);

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

int
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

void
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

void
_jack_populate(app_t *app)
{
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
					port_t *port = _client_find_port_by_name(client, port_name);
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

void
_jack_depopulate(app_t *app)
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
