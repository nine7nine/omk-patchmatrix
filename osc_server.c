#include <assert.h>

#include <osc.lv2/osc.h>
#include <osc.lv2/reader.h>
#include <osc.lv2/writer.h>
#include <osc.lv2/stream.h>

#define BUF_LEN 2048

static uint8_t buf_rx [BUF_LEN];
static uint8_t buf_tx [BUF_LEN];
static size_t reat = 0;
static size_t writ = 0;

static void *
_write_req(void *data, size_t minimum, size_t *maximum)
{
	if(maximum)
	{
		*maximum = BUF_LEN;
	}

	return buf_rx;
}

static void
_write_adv(void *data, size_t written)
{
	reat = written;
}

static const void *
_read_req(void *data, size_t *toread)
{
	if(toread)
	{
		*toread = writ;
	}

	if(writ > 0)
	{
		return buf_tx;
	}

	return NULL;
}

static void
_read_adv(void *data)
{
	writ = 0;
}

static const LV2_OSC_Driver driv = {
	.write_req = _write_req,
	.write_adv = _write_adv,
	.read_req = _read_req,
	.read_adv = _read_adv
};

int
main(int argc, char **argv)
{
	LV2_OSC_Stream stream;

	assert(argc == 2);

	assert(lv2_osc_stream_init(&stream, argv[1], &driv, NULL) == 0);

	int32_t count = 0;
	while(true)
	{
		bool skip = false;
		const LV2_OSC_Enum ev = lv2_osc_stream_run(&stream);

		if(ev & LV2_OSC_RECV)
		{
			LV2_OSC_Reader reader;

			lv2_osc_reader_initialize(&reader, buf_rx, reat);
			assert(lv2_osc_reader_is_message(&reader));

			OSC_READER_MESSAGE_FOREACH(&reader, arg, reat)
			{
				assert(strcmp(arg->path, "/trip") == 0);
				assert(*arg->type == 'i');
				assert(arg->size == sizeof(int32_t));
				assert(arg->i == count);
				if(++count >= 1024)
				{
					skip = true;
				}
			}

			// send back
			memcpy(buf_tx, buf_rx, reat);
			writ = reat;
		}

		if(skip)
		{
			break;
		}
	}

	const LV2_OSC_Enum ev = lv2_osc_stream_run(&stream);
	assert(ev & LV2_OSC_SEND);

	assert(lv2_osc_stream_deinit(&stream) == 0);

	return 0;
}
