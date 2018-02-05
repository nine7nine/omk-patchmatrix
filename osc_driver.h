typedef struct _item_t item_t;
typedef struct _stash_t stash_t;

struct _item_t {
	size_t size;
	uint8_t buf [];
};

struct _stash_t {
	size_t size;
	item_t **items;
	item_t *rsvd;
};

static uint8_t *
_stash_write_req(stash_t *stash, size_t minimum, size_t *maximum)
{
	if(!stash->rsvd || (stash->rsvd->size < minimum))
	{
		const size_t sz = sizeof(item_t) + minimum;
		stash->rsvd = realloc(stash->rsvd, sz);
		assert(stash->rsvd);
		stash->rsvd->size = minimum;
	}

	if(maximum)
	{
		*maximum = stash->rsvd->size;
	}

	return stash->rsvd->buf;
}

static void
_stash_write_adv(stash_t *stash, size_t written)
{
	assert(stash->rsvd);
	assert(stash->rsvd->size >= written);
	stash->rsvd->size = written;
	stash->size += 1;
	stash->items = realloc(stash->items, sizeof(item_t *) * stash->size);
	stash->items[stash->size - 1] = stash->rsvd;
	stash->rsvd = NULL;
}

static const uint8_t *
_stash_read_req(stash_t *stash, size_t *size)
{
	if(stash->size == 0)
	{
		if(size)
		{
			*size = 0;
		}

		return NULL;
	}

	item_t *item = stash->items[0];

	if(size)
	{
		*size = item->size;
	}

	return item->buf;
}

static void
_stash_read_adv(stash_t *stash)
{
	assert(stash->size);

	free(stash->items[0]);
	stash->size -= 1;

	for(unsigned i = 0; i < stash->size; i++)
	{
		stash->items[i] = stash->items[i+1];
	}

	stash->items = realloc(stash->items, sizeof(item_t *) * stash->size);
}

static void *
_write_req(void *data, size_t minimum, size_t *maximum)
{
	stash_t *stash = data;

	return _stash_write_req(&stash[0], minimum, maximum);
}

static void
_write_adv(void *data, size_t written)
{
	stash_t *stash = data;

	_stash_write_adv(&stash[0], written);
}

static const void *
_read_req(void *data, size_t *toread)
{
	stash_t *stash = data;

	return _stash_read_req(&stash[1], toread);
}

static void
_read_adv(void *data)
{
	stash_t *stash = data;

	_stash_read_adv(&stash[1]);
}

static const LV2_OSC_Driver driv = {
	.write_req = _write_req,
	.write_adv = _write_adv,
	.read_req = _read_req,
	.read_adv = _read_adv
};
