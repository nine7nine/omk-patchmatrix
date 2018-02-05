#include <assert.h>

#include <osc.lv2/osc.h>
#include <osc.lv2/reader.h>
#include <osc.lv2/writer.h>
#include <osc.lv2/stream.h>

#include "osc_driver.h"

int
main(int argc, char **argv)
{
	static LV2_OSC_Stream stream;
	static stash_t stash [2];

	assert(argc == 2);

	assert(lv2_osc_stream_init(&stream, argv[1], &driv, stash) == 0);

	int32_t count = 0;
	while(true)
	{
		bool skip = false;
		const LV2_OSC_Enum ev = lv2_osc_stream_run(&stream);

		if(ev & LV2_OSC_RECV)
		{
			const uint8_t *buf_rx;
			size_t reat;

			while( (buf_rx = _stash_read_req(&stash[0], &reat)) )
			{
				LV2_OSC_Reader reader;

				lv2_osc_reader_initialize(&reader, buf_rx, reat);
				assert(lv2_osc_reader_is_message(&reader));

				OSC_READER_MESSAGE_FOREACH(&reader, arg, reat)
				{
					assert(strcmp(arg->path, "/trip") == 0);
					assert(*arg->type == 'i');
					assert(arg->size == sizeof(int32_t));
					assert(arg->i == count++);
				}

				// send back
				uint8_t *buf_tx;
				if( (buf_tx = _stash_write_req(&stash[1], reat, NULL)) )
				{
					memcpy(buf_tx, buf_rx, reat);

					_stash_write_adv(&stash[1], reat);
				}

				_stash_read_adv(&stash[0]);
			}
		}

		if(count >= 1024)
		{
			break;
		}
	}

	const LV2_OSC_Enum ev = lv2_osc_stream_run(&stream);
	assert(ev & LV2_OSC_SEND);

	assert(lv2_osc_stream_deinit(&stream) == 0);

	if(stash[0].rsvd)
	{
		free(stash[0].rsvd);
		stash[0].rsvd = NULL;
	}

	if(stash[1].rsvd)
	{
		free(stash[1].rsvd);
		stash[1].rsvd = NULL;
	}

	return 0;
}
