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

#ifndef _NK_PATCHER_H
#define _NK_PATCHER_H

typedef enum _nk_patcher_type_t nk_patcher_type_t;
typedef struct _nk_patcher_port_t nk_patcher_port_t;
typedef struct _nk_patcher_connection_t nk_patcher_connection_t;
typedef struct _nk_patcher_priv_t nk_patcher_priv_t;
typedef struct _nk_patcher_t nk_patcher_t;
typedef void (*nk_patcher_fill_t)(void *data, uintptr_t source_id, uintptr_t sink_id,
	bool *state, nk_patcher_type_t *type);
typedef void (nk_patcher_change_t)(void *data, uintptr_t source_id, uintptr_t sink_id,
	bool state);

enum _nk_patcher_type_t {
	NK_PATCHER_TYPE_DIRECT,
	NK_PATCHER_TYPE_FEEDBACK,
	NK_PATCHER_TYPE_INDIRECT
};

struct _nk_patcher_port_t {
	int idx;
	uintptr_t id;
	struct nk_color color;
	char *label;
};

struct _nk_patcher_connection_t {
	bool state;
	nk_patcher_type_t type;
	int enm;
};

struct _nk_patcher_priv_t {
	float X, Y;
	float x, y;
	float w, h;
	float W, H;
	float scale;
	int ncols, nrows;
	float span, span1, span2;
	float x0, y0;
	int ax, ay;
	int sx, sy;

	bool needs_predraw;
	bool realizing;
};

struct _nk_patcher_t {
	int source_n;
	int sink_n;

	nk_patcher_port_t *sources;
	nk_patcher_port_t *sinks;

	nk_patcher_connection_t **connections;

	nk_patcher_priv_t priv;
};

enum {
	CONNECTED				= (1 << 0),
	VERTICAL				= (1 << 1),
	HORIZONTAL			= (1 << 2),
	VERTICAL_EDGE		= (1 << 3),
	HORIZONTAL_EDGE	= (1 << 4),
	FEEDBACK				= (1 << 5),
	INDIRECT				= (1 << 6)
};

static void
nk_patcher_init(nk_patcher_t *patch);

static void
nk_patcher_reinit(nk_patcher_t *patch, int source_n, int sink_n);

static void
nk_patcher_deinit(nk_patcher_t *patch);

static int
nk_patcher_connected_set(nk_patcher_t *patch, uintptr_t source_id, intptr_t sink_id,
	bool state, nk_patcher_type_t type);

static int
nk_patcher_source_id_set(nk_patcher_t *patch, int source_idx, uintptr_t source_id);

static int
nk_patcher_sink_id_set(nk_patcher_t *patch, int sink_idx, uintptr_t sink_id);

static int
nk_patcher_source_color_set(nk_patcher_t *patch, int source_idx, struct nk_color source_color);

static int
nk_patcher_sink_color_set(nk_patcher_t *patch, int sink_idx, struct nk_color sink_color);

static int
nk_patcher_source_label_set(nk_patcher_t *patch, int source_idx, const char *source_label);

static int
nk_patcher_sink_label_set(nk_patcher_t *patch, int sink_idx, const char *sink_label);

static void
nk_patcher_render(nk_patcher_t *patch, struct nk_context *ctx, struct nk_rect bounds,
	nk_patcher_change_t *change, void *data);

static void
nk_patcher_fill(nk_patcher_t *patch, nk_patcher_fill_t fill, void *data);

#endif // _NK_PATCHER_H

#ifdef NK_PATCHER_IMPLEMENTATION
static const struct nk_color bright = {.r = 0xee, .g = 0xee, .b = 0xee, .a = 0xff};

static void
nk_patcher_init(nk_patcher_t *patch)
{
	nk_patcher_reinit(patch, 0, 0);

	nk_patcher_priv_t *priv = &patch->priv;
	priv->scale = 0.45;
	priv->ax = -1;
	priv->ay = -1;
	priv->sx = 0;
	priv->sy = 0;
}

static void
nk_patcher_reinit(nk_patcher_t *patch, int source_n, int sink_n)
{
	nk_patcher_deinit(patch);

	patch->source_n = source_n;
	patch->sink_n = sink_n;

	patch->sources = calloc(patch->source_n, sizeof(nk_patcher_port_t));
	patch->sinks = calloc(patch->sink_n, sizeof(nk_patcher_port_t));

	patch->connections = calloc(patch->source_n, sizeof(nk_patcher_connection_t *));
	for(int source_idx=0; source_idx<patch->source_n; source_idx++)
	{
		patch->connections[source_idx] = calloc(patch->sink_n, sizeof(nk_patcher_connection_t));
	}
}

static void
nk_patcher_deinit(nk_patcher_t *patch)
{
	if(patch->connections)
	{
		for(int source_idx=0; source_idx<patch->source_n; source_idx++)
		{
			if(patch->connections[source_idx])
				free(patch->connections[source_idx]);
		}
		free(patch->connections);
	}

	if(patch->sources)
	{
		for(int source_idx=0; source_idx<patch->source_n; source_idx++)
		{
			nk_patcher_port_t *port = &patch->sources[source_idx];

			if(port->label)
				free(port->label);
		}
		free(patch->sources);
	}

	if(patch->sinks)
	{
		for(int sink_idx=0; sink_idx<patch->sink_n; sink_idx++)
		{
			nk_patcher_port_t *port = &patch->sinks[sink_idx];

			if(port->label)
				free(port->label);
		}
		free(patch->sinks);
	}

	patch->source_n = 0;
	patch->sink_n = 0;
}

static inline int
_nk_patcher_source_idx_get(nk_patcher_t *patch, uintptr_t source_id)
{
	for(int source_idx=0; source_idx<patch->source_n; source_idx++)
	{
		if(patch->sources[source_idx].id == source_id)
			return source_idx;
	}

	return -1; // not found
}

static inline int
_nk_patcher_sink_idx_get(nk_patcher_t *patch, uintptr_t sink_id)
{
	for(int sink_idx=0; sink_idx<patch->sink_n; sink_idx++)
	{
		if(patch->sinks[sink_idx].id == sink_id)
			return sink_idx;
	}

	return -1; // not found
}

static int
nk_patcher_connected_set(nk_patcher_t *patch, uintptr_t source_id, intptr_t sink_id,
	bool state, nk_patcher_type_t type)
{
	const int source_idx = _nk_patcher_source_idx_get(patch, source_id);
	const int sink_idx = _nk_patcher_sink_idx_get(patch, sink_id);

	if( (source_idx == -1) || (sink_idx == -1) )
		return -1;

	nk_patcher_connection_t *conn = &patch->connections[source_idx][sink_idx];
	conn->state = state;
	conn->type = type;

	return 0;
}

static int
nk_patcher_source_id_set(nk_patcher_t *patch, int source_idx, uintptr_t source_id)
{
	if( (source_idx < 0) || (source_idx >= patch->source_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sources[source_idx];
	port->id = source_id;

	return 0;
}

static int
nk_patcher_sink_id_set(nk_patcher_t *patch, int sink_idx, uintptr_t sink_id)
{
	if( (sink_idx < 0) || (sink_idx >= patch->sink_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sinks[sink_idx];
	port->id = sink_id;

	return 0;
}

static int
nk_patcher_source_color_set(nk_patcher_t *patch, int source_idx, struct nk_color source_color)
{
	if( (source_idx < 0) || (source_idx >= patch->source_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sources[source_idx];
	port->color = source_color;

	return 0;
}

static int
nk_patcher_sink_color_set(nk_patcher_t *patch, int sink_idx, struct nk_color sink_color)
{
	if( (sink_idx < 0) || (sink_idx >= patch->sink_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sinks[sink_idx];
	port->color = sink_color;

	return 0;
}

static int
nk_patcher_source_label_set(nk_patcher_t *patch, int source_idx, const char *source_label)
{
	if( (source_idx < 0) || (source_idx >= patch->source_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sources[source_idx];
	if(port->label)
		free(port->label);
	port->label = strdup(source_label);

	return 0;
}

static int
nk_patcher_sink_label_set(nk_patcher_t *patch, int sink_idx, const char *sink_label)
{
	if( (sink_idx < 0) || (sink_idx >= patch->sink_n) )
		return -1;

	nk_patcher_port_t *port = &patch->sinks[sink_idx];
	if(port->label)
		free(port->label);
	port->label = strdup(sink_label);

	return 0;
}

static inline void
_rel_to_abs(nk_patcher_priv_t *priv, float ax, float ay, float *_fx, float *_fy)
{
	const float w2 = (float)priv->W/2;
	const float h2 = (float)priv->H/2;

	ay = priv->nrows - ay;
	float fx = priv->x0 + priv->span * ( ax + ay);
	float fy = priv->y0 + priv->span * (-ax + ay);

	fx =  fx*w2 + priv->X + w2;
	fy = -fy*h2 + priv->Y + h2;

	*_fx = fx;
	*_fy = fy;
}

static inline void
_abs_to_rel(nk_patcher_priv_t *priv, float fx, float fy, int *_ax, int *_ay)
{
	const float w2 = (float)priv->W/2;
	const float h2 = (float)priv->H/2;

	fx =  (fx - priv->X - w2) / w2;
	fy = -(fy - priv->Y - h2) / h2;

	float ax = floor( (-priv->x0 + fx + priv->y0 - fy) * priv->span2);
	float ay = floor( (-priv->x0 + fx - priv->y0 + fy) * priv->span2);
	ay = priv->nrows - 1 - ay;

	if( (ax >= 0) && (ax < priv->ncols) && (ay >= 0) && (ay < priv->nrows) )
	{
		// inside-bounds
	}
	else if(ax >= priv->ncols)
	{
		ax = -1; // out-of-bounds

		ay = floor( (-priv->y0 - fy) * priv->span1);
		if( (ay < 0) || (ay >= priv->nrows) )
			ay = -1; // out-of-bounds
	}
	else if(ay >= priv->nrows)
	{
		ax = floor( (priv->y0 - fy) * priv->span1);
		if( (ax < 0) || (ax >= priv->ncols) )
			ax = -1; // out-of-bounds

		ay = -1; // out-of-bounds
	}
	else
	{
		ax = -1; // out-of-bounds
		ay = -1; // out-of-bounds
	}

	*_ax = ax;
	*_ay = ay;
}

static inline void
_screen_to_abs(nk_patcher_priv_t *priv, int sx, int sy, float *_fx, float *_fy)
{
	float fx = sx;
	fx /= priv->W; //TODO precalc
	fx *= 2.f;
	fx -= 1.f;

	float fy = sy;
	fy /= priv->H; //TODO precalc
	fy = 1.f - fy;
	fy *= 2.f;
	fy -= 1.f;

	*_fx = fx;
	*_fy = fy;
}

static inline void
_abs_to_screen(nk_patcher_priv_t *priv, float fx, float fy, int *_sx, int *_sy)
{
	float sx = fx;
	sx += 1.f;
	sx *= 0.5;
	sx *= priv->W;
	
	float sy = fy;
	sy += 1.f;
	sy *= 0.5;
	sy = 1.f - sy;
	sy *= priv->H;

	*_sx = floor(sx);
	*_sy = floor(sy);
}

static void
_precalc(nk_patcher_priv_t *priv)
{
	if(priv->ncols > priv->nrows)
	{
		priv->span = 1.f*priv->scale / priv->ncols;
		const float offset = priv->span * (priv->ncols - priv->nrows) * 0.5;
		priv->x0 = -1.f*priv->scale + offset;
		priv->y0 =  0.f*priv->scale + offset;
	}
	else if(priv->nrows > priv->ncols)
	{
		priv->span = 1.f*priv->scale / priv->nrows;
		const float offset = priv->span * (priv->nrows - priv->ncols) * 0.5;
		priv->x0 = -1.f*priv->scale + offset;
		priv->y0 =  0.f*priv->scale - offset;
	}
	else // priv->nrows == priv->ncols
	{
		priv->span = 1.f*priv->scale / priv->ncols;
		priv->x0 = -1.f*priv->scale;
		priv->y0 =  0.f*priv->scale;
	}

	priv->span1 = 1.f / priv->span;
	priv->span2 = 0.5 / priv->span;
}

static void
nk_patcher_render(nk_patcher_t *patch, struct nk_context *ctx, struct nk_rect bounds,
	nk_patcher_change_t *change, void *data)
{
	nk_patcher_priv_t *priv = &patch->priv;

	priv->ncols = patch->source_n;
	priv->nrows = patch->sink_n;

	nk_widget(&bounds, ctx);

	if(nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
	{
		//TODO
		priv->scale *= 1.0 + ctx->input.mouse.scroll_delta * 0.05;
		if(priv->scale < 0.1)
			priv->scale = 0.1;
		else if(priv->scale > 0.8)
			priv->scale = 0.8;
	}

	if(priv->ncols && priv->nrows)
	{
		struct nk_style *style = &ctx->style;

		priv->w = bounds.w;
		priv->h = bounds.h;

		priv->W = bounds.w > bounds.h ? bounds.w : bounds.h;
		priv->H = bounds.h > bounds.w ? bounds.h : bounds.w;

		priv->x = bounds.x;
		priv->y = bounds.y;

		priv->X = bounds.x - (priv->W - priv->w) / 2;
		priv->Y = bounds.y - (priv->H - priv->h) / 2;

		_precalc(priv);

		int COL = -1; // initialize
		int ROW = -1; // initialize

		if(nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
		{
			float mx = ctx->input.mouse.pos.x;
			float my = ctx->input.mouse.pos.y;

			_abs_to_rel(priv, mx, my, &COL, &ROW);

			// handle state toggling
			if(change && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT))
			{
				if( (COL != -1) && (ROW != -1) )
				{
					nk_patcher_port_t *source_port = &patch->sources[COL];
					nk_patcher_port_t *sink_port = &patch->sinks[ROW];
					nk_patcher_connection_t *conn = &patch->connections[COL][ROW];

					change(data, source_port->id, sink_port->id, !conn->state);
				}
				else if(COL != -1)
				{
					nk_patcher_port_t *source_port = &patch->sources[COL];
					bool state = false;

					for(int row = 0; row < priv->nrows; row++)
					{
						nk_patcher_port_t *sink_port = &patch->sinks[row];
						nk_patcher_connection_t *conn = &patch->connections[COL][row];

						state = state || conn->state;
					}
					for(int row = 0; row < priv->nrows; row++)
					{
						nk_patcher_port_t *sink_port = &patch->sinks[row];
						nk_patcher_connection_t *conn = &patch->connections[COL][row];

						change(data, source_port->id, sink_port->id, !state);
					}
				}
				else if(ROW != -1)
				{
					nk_patcher_port_t *sink_port = &patch->sinks[ROW];
					bool state = false;

					for(int col = 0; col < priv->ncols; col++)
					{
						nk_patcher_port_t *source_port = &patch->sources[col];
						nk_patcher_connection_t *conn = &patch->connections[col][ROW];

						state = state || conn->state;
					}
					for(int col = 0; col < priv->ncols; col++)
					{
						nk_patcher_port_t *source_port = &patch->sources[col];
						nk_patcher_connection_t *conn = &patch->connections[col][ROW];

						change(data, source_port->id, sink_port->id, !state);
					}
				}
			}
		}

		struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	
		// reset patch fields
		for(int col = 0; col < priv->ncols; col++)
		{
			for(int row = 0; row < priv->nrows; row++)
			{
				nk_patcher_connection_t *conn = &patch->connections[col][row];

				conn->enm = 0;
				switch(conn->type)
				{
					case NK_PATCHER_TYPE_DIRECT:
						break;
					case NK_PATCHER_TYPE_FEEDBACK:
						conn->enm |= FEEDBACK;
						break;
					case NK_PATCHER_TYPE_INDIRECT:
						conn->enm |= INDIRECT;
						break;
				}
			}
		}

		if( (COL != -1) && (ROW != -1) )
		{
			for(int col = 0; col < priv->ncols; col++)
			{
				for(int row = 0; row < priv->nrows; row++)
				{
					nk_patcher_connection_t *conn = &patch->connections[col][row];

					if( (row == ROW) && (col >  COL) )
						conn->enm |= HORIZONTAL;
					if( (row == ROW) && (col == COL) )
						conn->enm |= HORIZONTAL_EDGE | VERTICAL_EDGE;
					if( (row >  ROW) && (col == COL) )
						conn->enm |= VERTICAL;
				}
			}
		}
		else if(COL != -1)
		{
			int r = priv->nrows;
			for(int row = r-1; row >= 0; row--)
			{
				if(patch->connections[COL][row].state)
				{
					r = row;

					patch->connections[COL][row].enm |= HORIZONTAL_EDGE;
					for(int col = COL+1; col < priv->ncols; col++)
						patch->connections[col][row].enm |= HORIZONTAL;
				}
			}

			for(int row = r; row < priv->nrows; row++)
				patch->connections[COL][row].enm |= row == r ? VERTICAL_EDGE : VERTICAL;
		}
		else if(ROW != -1)
		{
			int c = priv->ncols;
			for(int col = c-1; col >= 0; col--)
			{
				if(patch->connections[col][ROW].state)
				{
					c = col;

					patch->connections[col][ROW].enm |= VERTICAL_EDGE;
					for(int row = ROW+1; row < priv->nrows; row++)
						patch->connections[col][row].enm |= VERTICAL;
				}
			}

			for(int col = c; col < priv->ncols; col++)
				patch->connections[col][ROW].enm |= col == c ? HORIZONTAL_EDGE : HORIZONTAL;
		}

		for(int col = 0; col < priv->ncols; col++)
		{
			for(int row = 0; row < priv->nrows; row++)
			{
				nk_patcher_connection_t *conn = &patch->connections[col][row];
				float p [8];

				// tile color
				_rel_to_abs(priv, col+0.0, row+0.0, &p[6], &p[7]);
				_rel_to_abs(priv, col+0.0, row+1.0, &p[4], &p[5]);
				_rel_to_abs(priv, col+1.0, row+1.0, &p[2], &p[3]);
				_rel_to_abs(priv, col+1.0, row+0.0, &p[0], &p[1]);
				if(conn->enm & FEEDBACK)
					nk_fill_polygon(canvas, p, 4, style->button.hover.data.color);
				else if(conn->enm & INDIRECT)
					nk_fill_polygon(canvas, p, 4, style->button.active.data.color);
				else
					nk_fill_polygon(canvas, p, 4, style->button.normal.data.color);

				// HORIZONTAL
				if(conn->enm & HORIZONTAL)
				{
					_rel_to_abs(priv, col+0.0, row+0.4, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.0, row+0.6, &p[4], &p[5]);
					_rel_to_abs(priv, col+1.0, row+0.6, &p[2], &p[3]);
					_rel_to_abs(priv, col+1.0, row+0.4, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, patch->sinks[row].color);
				}

				// HORIZONTAL_EDGE
				if(conn->enm & HORIZONTAL_EDGE)
				{
					_rel_to_abs(priv, col+0.6, row+0.4, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.6, row+0.6, &p[4], &p[5]);
					_rel_to_abs(priv, col+1.0, row+0.6, &p[2], &p[3]);
					_rel_to_abs(priv, col+1.0, row+0.4, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, patch->sinks[row].color);
				}

				// VERTICAL
				if(conn->enm & VERTICAL)
				{
					_rel_to_abs(priv, col+0.4, row+0.0, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.4, row+1.0, &p[4], &p[5]);
					_rel_to_abs(priv, col+0.6, row+1.0, &p[2], &p[3]);
					_rel_to_abs(priv, col+0.6, row+0.0, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, patch->sources[col].color);
				}

				// VERTICAL_EDGE
				if(conn->enm & VERTICAL_EDGE)
				{
					_rel_to_abs(priv, col+0.4, row+0.6, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.4, row+1.0, &p[4], &p[5]);
					_rel_to_abs(priv, col+0.6, row+1.0, &p[2], &p[3]);
					_rel_to_abs(priv, col+0.6, row+0.6, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, patch->sources[col].color);
				}

				// CONNECTED
				if(conn->state)
				{
					_rel_to_abs(priv, col+0.2, row+0.2, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.2, row+0.8, &p[4], &p[5]);
					_rel_to_abs(priv, col+0.8, row+0.8, &p[2], &p[3]);
					_rel_to_abs(priv, col+0.8, row+0.2, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, bright);
				}
				// EDGE
				else if(conn->enm & (VERTICAL_EDGE | HORIZONTAL_EDGE) )
				{
					_rel_to_abs(priv, col+0.38, row+0.38, &p[6], &p[7]);
					_rel_to_abs(priv, col+0.38, row+0.62, &p[4], &p[5]);
					_rel_to_abs(priv, col+0.62, row+0.62, &p[2], &p[3]);
					_rel_to_abs(priv, col+0.62, row+0.38, &p[0], &p[1]);

					nk_fill_polygon(canvas, p, 4, style->text.color);
				}
			}
		}

		// HORIZONTAL_LINES
		float yl, xl;
		for(int col = 0; col <= priv->ncols; col++)
		{
			const int row = priv->nrows;

			float p [6];
			_rel_to_abs(priv, col, 0,   &p[0], &p[1]);
			_rel_to_abs(priv, col, row, &p[2], &p[3]);
			p[4] = bounds.x; //-1.f;
			p[5] = p[3];

			float q [6];
			_rel_to_abs(priv, col - 0.2, row, &q[0], &q[1]);
			_rel_to_abs(priv, col - 0.8, row, &q[2], &q[3]);
			q[4] = q[2];
			q[5] = q[1];

			if(col > 0)
			{
				const int c = col - 1;
				const bool active = (COL == c)
					|| ( (COL == -1) && (ROW != -1) && patch->connections[c][ROW].state );

				nk_patcher_port_t *source_port = &patch->sources[c];
				struct nk_rect label = nk_rect(p[4], yl, xl-p[4], p[3]-yl);
				const char *name = source_port->label;
				const size_t len = nk_strlen(name);
				const struct nk_text text = {
					.padding.x = 2,
					.padding.y = 0,
					.background = style->window.background,
					.text = active ? source_port->color : style->text.color
				};

				if(active)
					nk_fill_rect(canvas, label, 0.f, style->button.active.data.color);
				nk_fill_polygon(canvas, q, 3, source_port->color);
				nk_widget_text(canvas, label, name, len, &text,
					NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE, style->font);
			}

			nk_stroke_polyline(canvas, p, 3, 2.f, style->window.border_color);
			xl = p[2];
			yl = p[3];
		}

		// VERTICAL_LINES
		for(int row = 0; row <= priv->nrows; row++)
		{
			const int col = priv->ncols;

			float p [6];
			_rel_to_abs(priv, 0,   row, &p[0], &p[1]);
			_rel_to_abs(priv, col, row, &p[2], &p[3]);
			p[4] = bounds.x + bounds.w; //1.f;
			p[5] = p[3];

			float q[6];
			_rel_to_abs(priv, col, row - 0.2, &q[0], &q[1]);
			_rel_to_abs(priv, col, row - 0.8, &q[2], &q[3]);
			q[4] = q[2];
			q[5] = q[1];

			if(row > 0)
			{
				const int r = row - 1;
				const bool active = (ROW == r)
					|| ( (COL != -1) && (ROW == -1) && patch->connections[COL][r].state );

				nk_patcher_port_t *sink_port = &patch->sinks[r];
				struct nk_rect label = nk_rect(xl, yl, p[4]-xl, p[3]-yl);
				const char *name = sink_port->label;
				const size_t len = nk_strlen(name);
				const struct nk_text text = {
					.padding.x = 2,
					.padding.y = 0,
					.background = style->window.background,
					.text = active ? sink_port->color : style->text.color
				};

				if(active)
					nk_fill_rect(canvas, label, 0.f, style->button.active.data.color);
				nk_fill_polygon(canvas, q, 3, sink_port->color);
				nk_widget_text(canvas, label, name, len, &text,
					NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_MIDDLE, style->font);
			}
	
			nk_stroke_polyline(canvas, p, 3, 2.f, style->window.border_color);
			xl = p[2];
			yl = p[3];
		}

		// HOVER
		if( (COL != -1) && (ROW != -1) )
		{
			const int col = COL;
			const int row = ROW;
			float p [8];

			_rel_to_abs(priv, col+0, row+0, &p[0], &p[1]);
			_rel_to_abs(priv, col+0, row+1, &p[2], &p[3]);
			_rel_to_abs(priv, col+1, row+1, &p[4], &p[5]);
			_rel_to_abs(priv, col+1, row+0, &p[6], &p[7]);

			nk_stroke_polygon(canvas, p, 4, 2.f, bright);
		}
	}
}

static void
nk_patcher_fill(nk_patcher_t *patch, nk_patcher_fill_t fill, void *data)
{
	if(!fill)
		return;

	for(int source_idx=0; source_idx<patch->source_n; source_idx++)
	{
		nk_patcher_port_t *source_port = &patch->sources[source_idx];

		for(int sink_idx=0; sink_idx<patch->sink_n; sink_idx++)
		{
			nk_patcher_port_t *sink_port = &patch->sinks[sink_idx];
			nk_patcher_connection_t *conn = &patch->connections[source_idx][sink_idx];

			fill(data, source_port->id, sink_port->id, &conn->state, &conn->type);
		}
	}
}

#endif // NK_PATCHER_IMPLEMENTATION
