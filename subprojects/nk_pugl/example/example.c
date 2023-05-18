/*
 * SPDX-FileCopyrightText: Hanspeter Portner <dev@open-music-kontrollers.ch>
 * SPDX-License-Identifier: Artistic-2.0
 */

#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <limits.h>

#define LEN NK_LEN
#define MAX NK_MAX

#define NK_PUGL_IMPLEMENTATION
#include <nk_pugl/nk_pugl.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "nuklear/demo/overview.c"
#pragma GCC diagnostic pop

static volatile sig_atomic_t done = 0;

static void
_sigint(int signum __attribute__((unused)))
{
	done = 1;
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds __attribute__((unused)),
	void *data __attribute__((unused)))
{
	overview(ctx);
}

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	static nk_pugl_window_t win;
	nk_pugl_config_t *cfg = &win.cfg;

	const float scale = nk_pugl_get_scale();

	cfg->width = 1280 * scale;
	cfg->height = 720 * scale;
	cfg->resizable = true;
	cfg->parent = 0;
	cfg->threads = false;
	cfg->ignore = false;
	cfg->class = "nk_pugl_example";
	cfg->title = "Nk Pugl Example";
	cfg->expose = _expose;
	cfg->data = NULL;
	cfg->font.face = "./Cousine-Regular.ttf";
	cfg->font.size = 13 * scale;

	signal(SIGTERM, _sigint);
	signal(SIGINT, _sigint);
#if !defined(_WIN32)
	signal(SIGQUIT, _sigint);
	signal(SIGKILL, _sigint);
#endif

	const intptr_t widget = nk_pugl_init(&win);
	if(!widget)
	{
		return 1;
	}

	nk_pugl_show(&win);

#if !defined(_WIN32) && !defined(__APPLE__)
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC, &tm);
#endif

	while(!done)
	{
#if !defined(_WIN32) && !defined(__APPLE__)
		if(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tm, NULL) != 0)
		{
			continue;
		}

#define NANOS 1000000000
		tm.tv_nsec += NANOS / 25;
		while(tm.tv_nsec >= NANOS)
		{
			tm.tv_sec += 1;
			tm.tv_nsec -= NANOS;
		}
#undef NANOS
#else
		usleep(1000000 / 25);
#endif

		done = nk_pugl_process_events(&win);
	}

	nk_pugl_hide(&win);

	nk_pugl_shutdown(&win);

	return 0;
}
