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

//#define LEN(x) sizeof(x)

static struct nk_rect
nk_shrink_rect(struct nk_rect r, float amount)
{
    struct nk_rect res;
    r.w = NK_MAX(r.w, 2 * amount);
    r.h = NK_MAX(r.h, 2 * amount);
    res.x = r.x + amount;
    res.y = r.y + amount;
    res.w = r.w - 2 * amount;
    res.h = r.h - 2 * amount;
    return res;
}

static void
_client_moveable(struct nk_context *ctx, app_t *app, client_t *client,
	struct nk_rect *bounds)
{
	const struct nk_input *in = &ctx->input;

	/* node connector and linking */
	if(client->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_RIGHT))
		{
			client->moving = false;
		}
		else
		{
			client->pos.x += in->mouse.delta.x;
			client->pos.y += in->mouse.delta.y;
			bounds->x += in->mouse.delta.x;
			bounds->y += in->mouse.delta.y;

			// move connections together with client
			HASH_FOREACH(&app->conns, client_conn_itr)
			{
				client_conn_t *client_conn = *client_conn_itr;

				if(client_conn->source_client == client)
				{
					client_conn->pos.x += in->mouse.delta.x/2;
					client_conn->pos.y += in->mouse.delta.y/2;
				}

				if(client_conn->sink_client == client)
				{
					client_conn->pos.x += in->mouse.delta.x/2;
					client_conn->pos.y += in->mouse.delta.y/2;
				}
			}
		}
	}
	else if(nk_input_is_mouse_hovering_rect(in, *bounds))
	{
		if(nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
		{
			client->moving = 1;
		}
	}
}

static void
_client_connectors(struct nk_context *ctx, app_t *app, client_t *client,
	struct nk_vec2 dim)
{
	struct node_editor *nodedit = &app->nodedit;
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	const float cw = 4.f;

	/* output connector */
	if(client->source_type & app->type)
	{
		const float cx = client->pos.x - scrolling.x + dim.x/2 + 2*cw;
		const float cy = client->pos.y - scrolling.y;
		const struct nk_rect circle = nk_rect(
			cx - cw, cy - cw,
			2*cw, 2*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));
		if(  nk_input_is_mouse_hovering_rect(in, nk_shrink_rect(circle, -cw))
			&& !nodedit->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, nk_rgb(100, 100, 100));
		}

		/* start linking process */
		if(nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, circle, nk_true)) {
			nodedit->linking.active = nk_true;
			nodedit->linking.source_client = client;
		}

		/* draw curve from linked node slot to mouse position */
		if(  nodedit->linking.active
			&& (nodedit->linking.source_client == client) )
		{
			struct nk_vec2 m = in->mouse.pos;

			nk_stroke_line(canvas, cx, cy, m.x, m.y, 1.f, nk_rgb(200, 200, 200));
		}
	}

	/* input connector */
	if(client->sink_type & app->type)
	{
		const float cx = client->mixer
			? client->pos.x - scrolling.x
			: client->pos.x - scrolling.x - dim.x/2 - 2*cw;
		const float cy = client->mixer
			? client->pos.y - scrolling.y - dim.y/2 - 2*cw
			: client->pos.y - scrolling.y;
		const struct nk_rect circle = nk_rect(
			cx - cw, cy - cw,
			2*cw, 2*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));
		if(  nk_input_is_mouse_hovering_rect(in, nk_shrink_rect(circle, -cw))
			&& nodedit->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, nk_rgb(200, 200, 200));
		}

		if(  nk_input_is_mouse_released(in, NK_BUTTON_LEFT)
			&& nk_input_is_mouse_hovering_rect(in, circle)
			&& nodedit->linking.active)
		{
			nodedit->linking.active = nk_false;

			client_t *src = nodedit->linking.source_client;
			if(src)
			{
				client_conn_t *client_conn = _client_conn_find(app, src, client);
				if(!client_conn) // does not yet exist
					client_conn = _client_conn_add(app, src, client);
				if(client_conn)
				{
					client_conn->type |= app->type;
				}
			}
		}
	}
}

static void
node_editor_mixer(struct nk_context *ctx, app_t *app, client_t *client)
{
	if(  !(client->source_type & app->type)
		&& !(client->sink_type & app->type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	mixer_t *mixer = client->mixer;

	const float ps = 32.f;
	const unsigned nx = mixer->nsinks;
	const unsigned ny = mixer->nsources;

	client->dim.x = nx * ps;
	client->dim.y = ny * ps;

	struct nk_rect bounds = nk_rect(
		client->pos.x - client->dim.x/2 - scrolling.x,
		client->pos.y - client->dim.y/2 - scrolling.y,
		client->dim.x, client->dim.y);

	_client_moveable(ctx, app, client, &bounds);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

    const struct nk_style_item *background;
		nk_flags state = 0; //FIXME
    if(state & NK_WIDGET_STATE_HOVER)
			background = &style->hover;
		else if(state & NK_WIDGET_STATE_ACTIVED)
			background = &style->active;
		else
			background = &style->normal;

		nk_fill_rect(canvas, body, style->rounding, ctx->style.button.hover.data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, style->border_color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		float x = body.x + ps/2;
		for(unsigned i = 0; i < nx; i++)
		{
			float y = body.y + ps/2;
			for(unsigned j = 0; j < ny; j++)
			{
				int32_t jgain = atomic_load_explicit(&mixer->jgains[i][j], memory_order_acquire);

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);

				const struct nk_mouse_button *btn = &in->mouse.buttons[NK_BUTTON_LEFT];;
				const bool left_mouse_down = btn->down;
				const bool left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in,
					NK_BUTTON_LEFT, tile, nk_true);

				int32_t dd = 0;
				if(left_mouse_down && left_mouse_click_in_cursor)
				{
					const float dx = in->mouse.delta.x;
					const float dy = in->mouse.delta.y;
					dd = fabs(dx) > fabs(dy) ? dx : -dy;
				}
				else if(nk_input_is_mouse_hovering_rect(in, tile))
				{
					if(in->mouse.scroll_delta != 0.f) // has scrolling
					{
						dd = in->mouse.scroll_delta;
						in->mouse.scroll_delta = 0.f;
					}
				}

				if(dd != 0)
				{
					jgain = NK_CLAMP(-36, jgain + dd, 36);
					atomic_store_explicit(&mixer->jgains[i][j], jgain, memory_order_release);
				}

				if( (left_mouse_down && left_mouse_click_in_cursor) || (dd != 0) )
				{
					char tooltip [32];
					snprintf(tooltip, 32, "%+2"PRIi32" dBFS", jgain);
					nk_tooltip(ctx, tooltip);
				}

				if(jgain > -36)
				{
					const float alpha = (jgain + 36) / 72.f;
					const float beta = NK_PI/2;

					nk_stroke_arc(canvas,
						x, y, 10.f,
						beta + 0.2f*NK_PI, beta + 1.8f*NK_PI,
						1.f,
						nk_rgb(100, 100, 100));
					nk_stroke_arc(canvas,
						x, y, 7.f,
						beta + 0.2f*NK_PI, beta + (0.2f + alpha*1.6f)*NK_PI,
						2.f,
						nk_rgb(200, 200, 200));
				}

				y += ps;
			}

			x += ps;
		}
	}

	_client_connectors(ctx, app, client, nk_vec2(bounds.w, bounds.h));
}

static void
node_editor_monitor(struct nk_context *ctx, app_t *app, client_t *client)
{
	if(  !(client->source_type & app->type)
		&& !(client->sink_type & app->type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	monitor_t *monitor = client->monitor;

	const float ps = 24.f;
	const unsigned ny = monitor->nsources;

	client->dim.x = 6 * ps;
	client->dim.y = ny * ps;

	struct nk_rect bounds = nk_rect(
		client->pos.x - client->dim.x/2 - scrolling.x,
		client->pos.y - client->dim.y/2 - scrolling.y,
		client->dim.x, client->dim.y);

	_client_moveable(ctx, app, client, &bounds);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

    const struct nk_style_item *background;
		nk_flags state = 0; //FIXME
    if(state & NK_WIDGET_STATE_HOVER)
			background = &style->hover;
		else if(state & NK_WIDGET_STATE_ACTIVED)
			background = &style->active;
		else
			background = &style->normal;

		nk_fill_rect(canvas, body, style->rounding, ctx->style.button.hover.data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, style->border_color);

		for(unsigned j = 0; j < ny; j++)
		{
			int32_t jgain = atomic_load_explicit(&monitor->jgains[j], memory_order_relaxed);

			struct nk_rect orig = nk_rect(body.x, body.y + j*ps, body.w, ps);
			struct nk_rect tile = orig;
			struct nk_rect outline;
			const float mx1 = 58.f / 70.f;
			const float mx2 = 12.f / 70.f;
			const uint8_t alph = 0x7f;
			const float e = (jgain + 64.f) / 70.f;
			const float peak = NK_CLAMP(0.f, e, 1.f);

			{
				const float dbfs = NK_MIN(peak, mx1);
				const uint8_t dcol = 0xff * dbfs / mx1;
				const struct nk_color left = nk_rgba(0x00, 0xff, 0xff, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(dcol, 0xff, 0xff-dcol, alph);
				const struct nk_color top = right;

				const float ox = ctx->style.font->height/2 + ctx->style.property.border + ctx->style.property.padding.x;
				const float oy = ctx->style.property.border + ctx->style.property.padding.y;
				tile.x += ox;
				tile.y += oy;
				tile.w -= 2*ox;
				tile.h -= 2*oy;
				outline = tile;
				tile.w *= dbfs;

				nk_fill_rect_multi_color(canvas, tile, left, top, right, bottom);
			}

			// > 6dBFS
			if(peak > mx1)
			{
				const float dbfs = peak- mx1;
				const uint8_t dcol = 0xff * dbfs / mx2;
				const struct nk_color left = nk_rgba(0xff, 0xff, 0x00, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(0xff, 0xff - dcol, 0x00, alph);
				const struct nk_color top = right;

				tile= outline;
				tile.x += tile.w * mx1;
				tile.w *= dbfs;
				nk_fill_rect_multi_color(canvas, tile, left, top, right, bottom);
			}

			// draw 6dBFS lines from -60 to +6
			for(unsigned i = 4; i <= 70; i += 6)
			{
				const bool is_zero = (i == 64);
				const float dx = outline.w * i / 70.f;

				const float x0 = outline.x + dx;
				const float y0 = is_zero ? orig.y + 2.f : outline.y;

				const float border = (is_zero ? 2.f : 1.f) * ctx->style.window.group_border;

				const float x1 = x0;
				const float y1 = is_zero ? orig.y + orig.h - 2.f : outline.y + outline.h;

				nk_stroke_line(canvas, x0, y0, x1, y1, border, ctx->style.window.group_border_color);
			}

			nk_stroke_rect(canvas, outline, 0.f, ctx->style.window.group_border, ctx->style.window.group_border_color);
		}
	}

	_client_connectors(ctx, app, client, nk_vec2(bounds.w, bounds.h));
	app->animating = true;
}

static void
node_editor_client(struct nk_context *ctx, app_t *app, client_t *client)
{
	if(  !(client->source_type & app->type)
		&& !(client->sink_type & app->type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	const struct nk_vec2 scrolling = nodedit->scrolling;

	struct nk_rect bounds = nk_rect(
		client->pos.x - client->dim.x/2 - scrolling.x,
		client->pos.y - client->dim.y/2 - scrolling.y,
		client->dim.x, client->dim.y);

	_client_moveable(ctx, app, client, &bounds);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	nk_button_label(ctx, client->name);

	_client_connectors(ctx, app, client, nk_vec2(bounds.w, bounds.h));
}

static void
node_editor_client_conn(struct nk_context *ctx, app_t *app,
	client_conn_t *client_conn, port_type_t port_type)
{
	if(  (client_conn->type != app->type)
		&& !(client_conn->type & port_type) )
	{
		return;
	}

	struct node_editor *nodedit = &app->nodedit;
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = nodedit->scrolling;

	client_t *src = client_conn->source_client;
	client_t *snk = client_conn->sink_client;

	if(!src || !snk)
		return;

	int nx = 0;
	HASH_FOREACH(&client_conn->source_client->sources, source_port_itr)
	{
		port_t *source_port = *source_port_itr;

		if(!(source_port->type & port_type))
			continue;

		nx += 1;
	}

	int ny = 0;
	HASH_FOREACH(&client_conn->sink_client->sinks, sink_port_itr)
	{
		port_t *sink_port = *sink_port_itr;

		if(!(sink_port->type & port_type))
			continue;

		ny += 1;
	}

	const float ps = 16.f;
	const float pw = nx * ps;
	const float ph = ny * ps;
	struct nk_rect bounds = nk_rect(
		client_conn->pos.x - scrolling.x - pw/2,
		client_conn->pos.y - scrolling.y - ph/2,
		pw, ph
	);

	int hovers = 0;
	if(client_conn->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_RIGHT))
		{
			client_conn->moving = false;
		}
		else
		{
			client_conn->pos.x += in->mouse.delta.x;
			client_conn->pos.y += in->mouse.delta.y;
			bounds.x += in->mouse.delta.x;
			bounds.y += in->mouse.delta.y;
		}
	}
	else if(nk_input_is_mouse_hovering_rect(in, bounds))
	{
		hovers = 1;

		if(nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT) )
		{
			client_conn->moving = 1;
		}
	}
	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

    const struct nk_style_item *background;
		nk_flags state = 0; //FIXME
    if(state & NK_WIDGET_STATE_HOVER)
			background = &style->hover;
		else if(state & NK_WIDGET_STATE_ACTIVED)
			background = &style->active;
		else
			background = &style->normal;

		nk_fill_rect(canvas, body, style->rounding, background->data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border, style->border_color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		float x = body.x + ps/2;
		HASH_FOREACH(&client_conn->source_client->sources, source_port_itr)
		{
			port_t *source_port = *source_port_itr;

			if(!(source_port->type & port_type))
				continue;

			float y = body.y + ps/2;
			HASH_FOREACH(&client_conn->sink_client->sinks, sink_port_itr)
			{
				port_t *sink_port = *sink_port_itr;

				if(!(sink_port->type & port_type))
					continue;

				port_conn_t *port_conn = _port_conn_find(client_conn, source_port, sink_port);

				if(port_conn)
					nk_fill_arc(canvas, x, y, 4.f, 0.f, 2*NK_PI, nk_rgb(100, 100, 100));

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);
				if(  nk_input_is_mouse_hovering_rect(in, tile)
					&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT) )
				{
					if(port_conn)
						jack_disconnect(app->client, source_port->name, sink_port->name);
					else
						jack_connect(app->client, source_port->name, sink_port->name);
				}

				y += ps;
			}

			x += ps;
		}
	}

	const float cs = 4.f;
	const float cx = client_conn->pos.x - scrolling.x;
	const float cxr = cx + pw/2;
	const float cy = client_conn->pos.y - scrolling.y;
	const float cyl = cy - ph/2;
	const struct nk_color col = hovers || client_conn->moving
		? nk_rgb(200, 200, 200)
		: nk_rgb(100, 100, 100);

	const float l0x = src->pos.x - scrolling.x + src->dim.x/2 + cs*2;
	const float l0y = src->pos.y - scrolling.y;
	const float l1x = snk->mixer
		? snk->pos.x - scrolling.x
		: snk->pos.x - scrolling.x - snk->dim.x/2 - cs*2;
	const float l1y = snk->mixer
		? snk->pos.y - scrolling.y - snk->dim.y/2 - cs*2
		: snk->pos.y - scrolling.y;

	const float bend = 50.f;
	nk_stroke_curve(canvas,
		l0x, l0y,
		l0x + bend, l0y,
		cx, cyl - bend,
		cx, cyl,
		1.f, col);
	nk_stroke_curve(canvas,
		cxr, cy,
		cxr + bend, cy,
		snk->mixer ? l1x : l1x - bend, snk->mixer ? l1y - bend : l1y,
		l1x, l1y,
		1.f, col);

	nk_fill_arc(canvas, cx, cyl, cs, 2*M_PI/2, 4*M_PI/2, col);
	nk_fill_arc(canvas, cxr, cy, cs, 3*M_PI/2, 5*M_PI/2, col);
}

static inline void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	app_t *app = data;

	app->animating = false;

	const float dy = 20.f * nk_pugl_get_scale(&app->win);
	app->dy = dy;

	int n = 0;
	const struct nk_input *in = &ctx->input;
	client_t *updated = 0;
	struct node_editor *nodedit = &app->nodedit;

	if(nk_begin(ctx, "Base", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);
		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
		const struct nk_rect total_space = nk_window_get_content_region(ctx);

		nk_menubar_begin(ctx);
		{
#ifdef JACK_HAS_METADATA_API
			nk_layout_row_dynamic(ctx, dy, 4);
#else
			nk_layout_row_dynamic(ctx, dy, 2);
#endif
			const bool is_audio = app->type == TYPE_AUDIO;
			if(is_audio)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.audio, "AUDIO", NK_TEXT_RIGHT))
				app->type = TYPE_AUDIO;
			if(is_audio)
				nk_style_pop_color(ctx);

			const bool is_midi = app->type == TYPE_MIDI;
			if(is_midi)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.midi, "MIDI", NK_TEXT_RIGHT))
				app->type = TYPE_MIDI;
			if(is_midi)
				nk_style_pop_color(ctx);

#ifdef JACK_HAS_METADATA_API
			const bool is_cv = app->type == TYPE_CV;
			if(is_cv)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.cv, "CV", NK_TEXT_RIGHT))
				app->type = TYPE_CV;
			if(is_cv)
				nk_style_pop_color(ctx);

			const bool is_osc = app->type == TYPE_OSC;
			if(is_osc)
				nk_style_push_color(ctx, &ctx->style.button.border_color, nk_rgb(200, 200, 200));
			if(nk_button_image_label(ctx, app->icons.osc, "OSC", NK_TEXT_RIGHT))
				app->type = TYPE_OSC;
			if(is_osc)
				nk_style_pop_color(ctx);
#endif

			nk_layout_row_dynamic(ctx, dy, 8);
			{
				if(nk_button_label(ctx, "Audio Mixer 1x1"))
					_mixer_add(app, 1, 1);
				if(nk_button_label(ctx, "Audio Mixer 2x2"))
					_mixer_add(app, 2, 2);
				if(nk_button_label(ctx, "Audio Mixer 4x4"))
					_mixer_add(app, 4, 4);
				if(nk_button_label(ctx, "Audio Mixer 8x8"))
					_mixer_add(app, 8, 8);
			}
			{
				if(nk_button_label(ctx, "Audio Monitor x1"))
					_monitor_add(app, 1);
				if(nk_button_label(ctx, "Audio Monitor x2"))
					_monitor_add(app, 2);
				if(nk_button_label(ctx, "Audio Monitor x4"))
					_monitor_add(app, 4);
				if(nk_button_label(ctx, "Audio Monitor x8"))
					_monitor_add(app, 8);
			}
		}
		nk_menubar_end(ctx);

		const struct nk_vec2 scrolling = nodedit->scrolling;

		/* allocate complete window space */
		nk_layout_space_begin(ctx, NK_STATIC, total_space.h,
			_hash_size(&app->clients) + _hash_size(&app->conns));
		{
			{
				/* display grid */
				struct nk_rect ssize = nk_layout_space_bounds(ctx);
				const float grid_size = 28.0f;
				const struct nk_color grid_color = nk_rgb(50, 50, 50);

				for(float x = fmod(ssize.x - scrolling.x, grid_size);
					x < ssize.w;
					x += grid_size)
				{
					nk_stroke_line(canvas, x + ssize.x, ssize.y, x + ssize.x, ssize.y + ssize.h,
						1.0f, grid_color);
				}

				for(float y = fmod(ssize.y - scrolling.y, grid_size);
					y < ssize.h;
					y += grid_size)
				{
					nk_stroke_line(canvas, ssize.x, y + ssize.y, ssize.x + ssize.w, y + ssize.y,
						1.0f, grid_color);
				}
			}

			/* execute each node as a movable group */
			HASH_FOREACH(&app->clients, client_itr)
			{
				client_t *client = *client_itr;

				if(client->mixer)
					node_editor_mixer(ctx, app, client);
				else if(client->monitor)
					node_editor_monitor(ctx, app, client);
				else
					node_editor_client(ctx, app, client);
			}

			/* reset linking connection */
			if(  nodedit->linking.active
				&& nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
			{
				nodedit->linking.active = nk_false;
			}

			HASH_FOREACH(&app->conns, client_conn_itr)
			{
				client_conn_t *client_conn = *client_conn_itr;

				node_editor_client_conn(ctx, app, client_conn, app->type);
			}
		}
		nk_layout_space_end(ctx);

		/* window content scrolling */
		if(  nk_input_is_mouse_hovering_rect(in, nk_window_get_bounds(ctx))
			&& nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			nodedit->scrolling.x -= in->mouse.delta.x;
			nodedit->scrolling.y -= in->mouse.delta.y;
		}
	}
	nk_end(ctx);
}

static struct nk_image
_icon_load(app_t *app, const char *path)
{
	return nk_pugl_icon_load(&app->win, path);
}

static void
_icon_unload(app_t *app, struct nk_image img)
{
	nk_pugl_icon_unload(&app->win, img);
}

int
_ui_init(app_t *app)
{
	// UI
	nk_pugl_config_t *cfg = &app->win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "PatchMatrix";
	cfg->title = "PatchMatrix";
	cfg->parent = 0;
	cfg->data = app;
	cfg->expose = _expose;
	cfg->font.face = PATCHMATRIX_DATA_DIR"/Cousine-Regular.ttf";
	cfg->font.size = 13;

	app->type = TYPE_AUDIO;
	app->designation = DESIGNATION_NONE;

	nk_pugl_init(&app->win);
	nk_pugl_show(&app->win);

	app->icons.audio= _icon_load(app, PATCHMATRIX_DATA_DIR"audio.png");
	app->icons.midi = _icon_load(app, PATCHMATRIX_DATA_DIR"midi.png");
#ifdef JACK_HAS_METADATA_API
	app->icons.cv = _icon_load(app, PATCHMATRIX_DATA_DIR"cv.png");
	app->icons.osc = _icon_load(app, PATCHMATRIX_DATA_DIR"osc.png");
#endif

	return 0;
}

void
_ui_deinit(app_t *app)
{
	_icon_unload(app, app->icons.audio);
	_icon_unload(app, app->icons.midi);
#ifdef JACK_HAS_METADATA_API
	_icon_unload(app, app->icons.cv);
	_icon_unload(app, app->icons.osc);
#endif

	nk_pugl_hide(&app->win);
	nk_pugl_shutdown(&app->win);
}

void
_ui_signal(app_t *app)
{
	if(!atomic_load_explicit(&app->done, memory_order_acquire))
		nk_pugl_async_redisplay(&app->win);
}
