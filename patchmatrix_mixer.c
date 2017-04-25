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

#include <patchmatrix.h>

typedef struct _mixer_app_t mixer_app_t;

struct _mixer_app_t {
	jack_client_t *client;
	jack_port_t *jsources [PORT_MAX];
	jack_port_t *jsinks[PORT_MAX];

	mixer_shm_t *shm;	
};

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

	if(atomic_load_explicit(&shm->closing, memory_order_relaxed))
		return 0;

	float *psinks [PORT_MAX];
	const float *psources [PORT_MAX];

	const unsigned nsources = shm->nsources;
	const unsigned nsinks = shm->nsinks;

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
			const int32_t mBFS = atomic_load_explicit(&shm->jgains[i][j], memory_order_relaxed);
			const float dBFS = mBFS / 100.f;

			if(dBFS == 0.f) // just add
			{
				for(unsigned k = 0; k < nframes; k++)
				{
					psinks[j][k] += psources[i][k];
				}
			}
			else if(dBFS > -36.f) // multiply-add
			{
				const float gain = exp10f(dBFS / 20.f); // jgain = 20.f*log10f(gain);

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
	mixer_app_t *mixer = arg;
	mixer_shm_t *shm = mixer->shm;

	if(atomic_load_explicit(&shm->closing, memory_order_relaxed))
		return 0;

	void *psinks [PORT_MAX];
	void *psources [PORT_MAX];

	const unsigned nsources = shm->nsources;
	const unsigned nsinks = shm->nsinks;

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
			const int32_t mBFS = atomic_load_explicit(&shm->jgains[i][J], memory_order_relaxed);
			const float dBFS = mBFS / 100.f;

			if(dBFS > -36.f) // connection to be mixed
			{
				uint8_t *msg = jack_midi_event_reserve(psinks[i], ev.time, ev.size);
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

		pos[J] += 1; // advance event pointer from this source
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

	const unsigned nsources = atoi(argv[2]);
	const unsigned nsinks = atoi(argv[3]);

	const jack_options_t opts = JackNullOption;
	jack_status_t status;
	mixer.client = jack_client_open("/patchmatrix_mixer", opts, &status);
	if(!mixer.client)
		return -1;

	for(unsigned i = 0; i < nsources; i++)
	{
		char port_name [32];
		snprintf(port_name, 32, "sink_%u", i);

		jack_port_t *jsource = jack_port_register(mixer.client, port_name,
			port_type == TYPE_AUDIO ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			JackPortIsInput, 0);

		mixer.jsources[i] = jsource;
	}

	for(unsigned j = 0; j < nsinks; j++)
	{
		char port_name [32];
		snprintf(port_name, 32, "source_%u", j);

		jack_port_t *jsink = jack_port_register(mixer.client, port_name,
			port_type == TYPE_AUDIO ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			JackPortIsOutput, 0);

		mixer.jsinks[j] = jsink;
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
				mixer.shm->nsources = nsources;
				mixer.shm->nsinks = nsinks;

				for(unsigned j = 0; j < nsinks; j++)
				{
					for(unsigned i = 0; i < nsources; i++)
					{
						if(i == j)
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

					jack_deactivate(mixer.client);

					sem_destroy(&mixer.shm->done);
				}

				munmap(mixer.shm, total_size);
			}
		}

		close(fd);
		shm_unlink(client_name);
	}

	for(unsigned i = 0; i < nsources; i++)
		jack_port_unregister(mixer.client, mixer.jsources[i]);

	jack_client_close(mixer.client);

	return 0;
}
