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

typedef struct _monitor_app_t monitor_app_t;

struct _monitor_app_t {
	jack_client_t *client;
	jack_port_t *jsources [PORT_MAX];
	float sample_rate_1;
	union {
		struct {
			float dBFSs [PORT_MAX];
		} audio;
		struct {
			float vels [PORT_MAX];
		} midi;
	};

	monitor_shm_t *shm;	
};

static void
_jack_on_info_shutdown_cb(jack_status_t code, const char *reason, void *arg)
{
	monitor_app_t *monitor = arg;
	monitor_shm_t *shm = monitor->shm;

	atomic_store_explicit(&shm->closing, true, memory_order_relaxed);
	sem_post(&shm->done);
}

static int
_audio_monitor_process(jack_nframes_t nframes, void *arg)
{
	monitor_app_t *monitor = arg;
	monitor_shm_t *shm = monitor->shm;

	if(atomic_load_explicit(&shm->closing, memory_order_relaxed))
		return 0;

	const float *psources [PORT_MAX];

	const unsigned nsources = shm->nsources;

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

		// go to zero in 1/2 s
		if(monitor->audio.dBFSs[i] > -64.f)
			monitor->audio.dBFSs[i] -= nframes * 70.f * 2.f * monitor->sample_rate_1;

		const float dBFS = (peak > 0.f)
			? 6.f + 20.f*log10f(peak / 2.f) // dBFS+6
			: -64.f;

		if(dBFS > monitor->audio.dBFSs[i])
			monitor->audio.dBFSs[i] = dBFS;

		const int32_t mBFS = rintf(monitor->audio.dBFSs[i] * 100.f);
		atomic_store_explicit(&shm->jgains[i], mBFS, memory_order_relaxed);
	}

	return 0;
}

static int
_midi_monitor_process(jack_nframes_t nframes, void *arg)
{
	monitor_app_t *monitor = arg;
	monitor_shm_t *shm = monitor->shm;

	if(atomic_load_explicit(&shm->closing, memory_order_relaxed))
		return 0;

	void *psources [PORT_MAX];

	const unsigned nsources = shm->nsources;

	for(unsigned i = 0; i < nsources; i++)
	{
		jack_port_t *jsource = monitor->jsources[i];
		psources[i] = jack_port_get_buffer(jsource, nframes);

		float vel = 0.f;
		const uint32_t count = jack_midi_get_event_count(psources[i]);
		for(unsigned k = 0; k < count; k++)
		{
			jack_midi_event_t ev;
			jack_midi_event_get(&ev, psources[i], k);

			if(ev.size != 3)
				continue;

			if( (ev.buffer[0] & 0xf0) == 0x90)
			{
				if(ev.buffer[2] > vel)
					vel = ev.buffer[2];
			}
		}

		// go to zero in 1/2 s
		if(monitor->midi.vels[i] > 0.f)
			monitor->midi.vels[i] -= nframes * 127.f * 2.f * monitor->sample_rate_1;

		if(vel > monitor->midi.vels[i])
			monitor->midi.vels[i] = vel;

		const int32_t cvel = rintf(monitor->midi.vels[i] * 100.f);
		atomic_store_explicit(&shm->jgains[i], cvel, memory_order_relaxed);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	static monitor_app_t monitor;
	const size_t total_size = sizeof(monitor_shm_t);

	if(argc < 3)
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

	const jack_options_t opts = JackNullOption;
	jack_status_t status;
	monitor.client = jack_client_open("/patchmatrix_monitor", opts, &status);
	if(!monitor.client)
		return -1;

	monitor.sample_rate_1 = 1.f / jack_get_sample_rate(monitor.client);

	for(unsigned i = 0; i < nsources; i++)
	{
		char port_name [32];
		snprintf(port_name, 32, "sink_%u", i);

		jack_port_t *jsource = jack_port_register(monitor.client, port_name,
			port_type == TYPE_AUDIO ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			JackPortIsInput | JackPortIsTerminal, 0);

		if(port_type == TYPE_AUDIO)
			monitor.audio.dBFSs[i] = -64.f;
		else if(port_type == TYPE_MIDI)
			monitor.midi.vels[i] = 0.f;

		monitor.jsources[i] = jsource;
	}

	const char *client_name = jack_get_client_name(monitor.client);
	const int fd = shm_open(client_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if(fd != -1)
	{
		if(ftruncate(fd, total_size) != -1)
		{
			if((monitor.shm = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0)) != MAP_FAILED)
			{
				monitor.shm->nsources = nsources;

				for(unsigned i = 0; i < nsources; i++)
					atomic_init(&monitor.shm->jgains[i], 0);

				if(sem_init(&monitor.shm->done, 1, 0) != -1)
				{
					jack_on_info_shutdown(monitor.client, _jack_on_info_shutdown_cb, &monitor);
					jack_set_process_callback(monitor.client,
						port_type == TYPE_AUDIO ? _audio_monitor_process : _midi_monitor_process,
						&monitor);
					//TODO CV

					jack_activate(monitor.client);

					sem_wait(&monitor.shm->done);

					jack_deactivate(monitor.client);

					sem_destroy(&monitor.shm->done);
				}

				munmap(monitor.shm, total_size);
			}
		}

		close(fd);
		shm_unlink(client_name);
	}

	for(unsigned i = 0; i < nsources; i++)
		jack_port_unregister(monitor.client, monitor.jsources[i]);

	jack_client_close(monitor.client);

	return 0;
}
