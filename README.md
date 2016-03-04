# Varchunk

## Ringbuffer optimized for realtime event handling

### Properties

* Is realtime-safe
* Is lock-free
* Supports variably sized chunks
* Supports contiguous memory chunks
* Supports zero copy operation
* Uses a simplistic API

### Build Status

[![Build Status](https://travis-ci.org/OpenMusicKontrollers/varchunk.svg)](https://travis-ci.org/OpenMusicKontrollers/varchunk)

### Usage

``` c
#include <pthread.h>
#include <varchunk.h>

static void *
producer_main(void *arg)
{
	varchunk_t *varchunk = arg;
	void *ptr;
	size_t towrite = 128;

	for(unsigned i=0; i<1000000; i++)
	{
		if( (ptr = varchunk_write_request(varchunk, towrite)) )
		{
			// write 'towrite' bytes to 'ptr'
			varchunk_write_advance(varchunk, towrite);
		}
	}

	return NULL;
}

static void *
consumer_main(void *arg)
{
	varchunk_t *varchunk = arg;
	const void *ptr;
	size_t toread;

	for(unsigned i=0; i<1000000; i++)
	{
		if( (ptr = varchunk_read_request(varchunk, &toread)) )
		{
			// read 'toread' bytes from 'ptr'
			varchunk_read_advance(varchunk);
		}
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	if(!varchunk_is_lock_free())
		return -1;

	pthread_t producer;
	pthread_t consumer;
	varchunk_t *varchunk = varchunk_new(8192);
	if(!varchunk)
		return -1;

	pthread_create(&consumer, NULL, consumer_main, varchunk);
	pthread_create(&producer, NULL, producer_main, varchunk);

	pthread_join(producer, NULL);
	pthread_join(consumer, NULL);

	varchunk_free(varchunk);

	return 0;
}
```

### License

Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.
