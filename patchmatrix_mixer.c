/*
 * Copyright (c) 2016-2017 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This sink is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the iapplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the sink as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <patchmatrix.h>

typedef struct _mixer_app_t mixer_app_t;

struct _mixer_app_t {
	jack_client_t *client;
	jack_port_t *jsinks [PORT_MAX];
	jack_port_t *jsources[PORT_MAX];

	mixer_shm_t *shm;	
};

static atomic_bool closed = ATOMIC_VAR_INIT(false);

static void
_jack_on_info_shutdown_cb(jack_status_t code, const char *reason, void *arg)
{
	mixer_app_t *mixer = arg;
	mixer_shm_t *shm = mixer->shm;

	atomic_store_explicit(&shm->closing, true, memory_order_relaxed);
	sem_post(&shm->done);
}

static int
_audio_mixer_process(jack_nframes_t nframes, void *arg)
{
	mixer_app_t *mixer = arg;
	mixer_shm_t *shm = mixer->shm;

	if(  atomic_load_explicit(&closed, memory_order_relaxed)
		|| atomic_load_explicit(&shm->closing, memory_order_relaxed) )
	{
		return 0;
	}

	float *psources [PORT_MAX];
	const float *psinks [PORT_MAX];

	const unsigned nsinks = shm->nsinks;
	const unsigned nsources = shm->nsources;

	for(unsigned i = 0; i < nsinks; i++)
	{
		jack_port_t *jsink = mixer->jsinks[i];
		psinks[i] = jack_port_get_buffer(jsink, nframes);
	}

	for(unsigned j = 0; j < nsources; j++)
	{
		jack_port_t *jsource = mixer->jsources[j];
		psources[j] = jack_port_get_buffer(jsource, nframes);

		// clear
		for(unsigned k = 0; k < nframes; k++)
		{
			psources[j][k] = 0.f;
		}
	}

	for(unsigned j = 0; j < nsources; j++)
	{
		for(unsigned i = 0; i < nsinks; i++)
		{
			const int32_t mBFS = atomic_load_explicit(&shm->jgains[j][i], memory_order_relaxed);
			const float dBFS = mBFS / 100.f;

			if(dBFS == 0.f) // just add
			{
				for(unsigned k = 0; k < nframes; k++)
				{
					psources[j][k] += psinks[i][k];
				}
			}
			else if(dBFS > -36.f) // multiply-add
			{
				const float gain = exp10f(dBFS / 20.f); // jgain = 20.f*log10f(gain);

				for(unsigned k = 0; k < nframes; k++)
				{
					psources[j][k] += gain * psinks[i][k];
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
	mixer_app_t *mixer = arg;
	mixer_shm_t *shm = mixer->shm;

	if(  atomic_load_explicit(&closed, memory_order_relaxed)
		|| atomic_load_explicit(&shm->closing, memory_order_relaxed) )
	{
		return 0;
	}

	void *psources [PORT_MAX];
	void *psinks [PORT_MAX];

	const unsigned nsinks = shm->nsinks;
	const unsigned nsources = shm->nsources;

	int count [PORT_MAX];
	int pos [PORT_MAX];

	for(unsigned i = 0; i < nsinks; i++)
	{
		jack_port_t *jsink = mixer->jsinks[i];
		psinks[i] = jack_port_get_buffer(jsink, nframes);

		count[i] = jack_midi_get_event_count(psinks[i]);
		pos[i] = 0;
	}

	for(unsigned j = 0; j < nsources; j++)
	{
		jack_port_t *jsource = mixer->jsources[j];
		psources[j] = jack_port_get_buffer(jsource, nframes);

		// clear
		jack_midi_clear_buffer(psources[j]);
	}

	while(true)
	{
		uint32_t T = UINT32_MAX;
		int I = -1;

		for(unsigned i = 0; i < nsinks; i++)
		{
			if(pos[i] >= count[i]) // no more events to process on this sink
				continue;

			jack_midi_event_t ev;
			jack_midi_event_get(&ev, psinks[i], pos[i]);

			if(ev.time <= T)
			{
				T = ev.time;
				I = i;
			}
		}

		if(I == -1) // no more events to process from all sinks
			break;

		jack_midi_event_t ev;
		jack_midi_event_get(&ev, psinks[I], pos[I]);

		for(unsigned j = 0; j < nsources; j++)
		{
			const int32_t mBFS = atomic_load_explicit(&shm->jgains[j][I], memory_order_relaxed);
			const float dBFS = mBFS / 100.f;

			if(dBFS > -36.f) // connection to be mixed
			{
				uint8_t *msg = jack_midi_event_reserve(psources[j], ev.time, ev.size);
				if(!msg)
					continue;

				memcpy(msg, ev.buffer, ev.size);

				if( (dBFS != 0.f) && (ev.size == 3) ) // multiply-add
				{
					const uint8_t cmd = msg[0] & 0xf0;
					if( (cmd == 0x90) || (cmd == 0x80) ) // noteOn or noteOff
					{
						const float gain = exp10f(dBFS / 20.f); // jgain = 20*log10(gain/1);

						const float vel = msg[2] * gain; // velocity
						msg[2] = vel < 0 ? 0 : (vel > 0x7f ? 0x7f : vel);
					}
				}
			}
			// else connection not to be mixed
		}

		pos[I] += 1; // advance event pointer from this sink
	}

	return 0;
}

int
main(int argc, char **argv)
{
	static mixer_app_t mixer;
	const size_t total_size = sizeof(mixer_shm_t);

	if(argc < 4)
		return -1;

	port_type_t port_type = TYPE_AUDIO;
	if(!strcasecmp(argv[1], "MIDI"))
		port_type = TYPE_MIDI;
#ifdef JACK_HAS_METADATA_API
	else if(!strcasecmp(argv[1], "CV"))
		port_type = TYPE_CV;
	else if(!strcasecmp(argv[1], "OSC"))
		port_type = TYPE_OSC;
#endif

	unsigned nsinks = atoi(argv[2]);
	if(nsinks > PORT_MAX)
		nsinks = PORT_MAX;

	unsigned nsources = atoi(argv[3]);
	if(nsources > PORT_MAX)
		nsources = PORT_MAX;

	const jack_options_t opts = JackNullOption;
	jack_status_t status;
	mixer.client = jack_client_open(PATCHMATRIX_MIXER, opts, &status);
	if(!mixer.client)
		return -1;

	for(unsigned i = 0; i < nsinks; i++)
	{
		char buf [32];
		snprintf(buf, 32, "sink_%02u", i + 1);

		jack_port_t *jsink = jack_port_register(mixer.client, buf,
			port_type == TYPE_AUDIO ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			JackPortIsInput, 0);

#ifdef JACK_HAS_METADATA_API
		jack_uuid_t uuid = jack_port_uuid(jsink);

		snprintf(buf, 32, "%u", i);
		jack_set_property(mixer.client, uuid, JACKEY_ORDER, buf, XSD__integer);

		snprintf(buf, 32, "Sink %u", i + 1);
		jack_set_property(mixer.client, uuid, JACK_METADATA_PRETTY_NAME, buf, "text/plain");
#endif

		mixer.jsinks[i] = jsink;
	}

	for(unsigned j = 0; j < nsources; j++)
	{
		char buf [32];
		snprintf(buf, 32, "source_%02u", j + 1);

		jack_port_t *jsource = jack_port_register(mixer.client, buf,
			port_type == TYPE_AUDIO ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			JackPortIsOutput, 0);

#ifdef JACK_HAS_METADATA_API
		jack_uuid_t uuid = jack_port_uuid(jsource);

		snprintf(buf, 32, "%u", j);
		jack_set_property(mixer.client, uuid, JACKEY_ORDER, buf, XSD__integer);

		snprintf(buf, 32, "Source %u", j + 1);
		jack_set_property(mixer.client, uuid, JACK_METADATA_PRETTY_NAME, buf, "text/plain");
#endif

		mixer.jsources[j] = jsource;
	}

	const char *client_name = jack_get_client_name(mixer.client);
	const int fd = shm_open(client_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if(fd != -1)
	{
		if(ftruncate(fd, total_size) != -1)
		{
			if((mixer.shm = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0)) != MAP_FAILED)
			{
				mixer.shm->nsinks = nsinks;
				mixer.shm->nsources = nsources;

				for(unsigned j = 0; j < nsources; j++)
				{
					for(unsigned i = 0; i < nsinks; i++)
					{
						if(j == i)
							atomic_init(&mixer.shm->jgains[j][i], 0);
						else
							atomic_init(&mixer.shm->jgains[j][i], -3600);
					}
				}

				if(sem_init(&mixer.shm->done, 1, 0) != -1)
				{
					jack_on_info_shutdown(mixer.client, _jack_on_info_shutdown_cb, &mixer);
					jack_set_process_callback(mixer.client,
						port_type == TYPE_AUDIO ? _audio_mixer_process : _midi_mixer_process,
						&mixer);
					//TODO CV

					jack_activate(mixer.client);

					sem_wait(&mixer.shm->done);
					atomic_store_explicit(&mixer.shm->closing, true, memory_order_relaxed);

					jack_deactivate(mixer.client);

					sem_destroy(&mixer.shm->done);
				}

				atomic_store_explicit(&closed, true, memory_order_relaxed);
				munmap(mixer.shm, total_size);
			}
		}

		close(fd);
		shm_unlink(client_name);
	}

	for(unsigned i = 0; i < nsinks; i++)
	{
#ifdef JACK_HAS_METADATA_API
		jack_uuid_t uuid = jack_port_uuid(mixer.jsinks[i]);
		jack_remove_properties(mixer.client, uuid);
#endif
		jack_port_unregister(mixer.client, mixer.jsinks[i]);
	}

	for(unsigned j = 0; j < nsources; j++)
	{
#ifdef JACK_HAS_METADATA_API
		jack_uuid_t uuid = jack_port_uuid(mixer.jsources[j]);
		jack_remove_properties(mixer.client, uuid);
#endif
		jack_port_unregister(mixer.client, mixer.jsources[j]);
	}

	jack_client_close(mixer.client);

	return 0;
}
