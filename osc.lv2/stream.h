/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef LV2_OSC_STREAM_H
#define LV2_OSC_STREAM_H

#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <osc.lv2/osc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *
(*LV2_OSC_Stream_Write_Request)(void *data, size_t minimum, size_t *maximum);

typedef void
(*LV2_OSC_Stream_Write_Advance)(void *data, size_t written);

typedef const void *
(*LV2_OSC_Stream_Read_Request)(void *data, size_t *toread);

typedef void
(*LV2_OSC_Stream_Read_Advance)(void *data);

typedef struct _LV2_OSC_Address LV2_OSC_Address;
typedef struct _LV2_OSC_Driver LV2_OSC_Driver;
typedef struct _LV2_OSC_Stream LV2_OSC_Stream;

struct _LV2_OSC_Address {
	socklen_t len;
	union {
		struct sockaddr in;
		struct sockaddr_in in4;
		struct sockaddr_in6 in6;
	};
};

struct _LV2_OSC_Driver {
	LV2_OSC_Stream_Write_Request write_req;
	LV2_OSC_Stream_Write_Advance write_adv;
	LV2_OSC_Stream_Read_Request read_req;
	LV2_OSC_Stream_Read_Advance read_adv;
};

struct _LV2_OSC_Stream {
	int socket_family;
	int socket_type;
	int protocol;
	bool server;
	bool slip;
	int sock;
	int fd;
	LV2_OSC_Address self;
	LV2_OSC_Address peer;
	const LV2_OSC_Driver *driv;
	void *data;
};

typedef enum _LV2_OSC_Enum {
	LV2_OSC_NONE = (0 << 0),
	LV2_OSC_SEND = (1 << 0),
	LV2_OSC_RECV = (1 << 1)
} LV2_OSC_Enum;

static const char *udp_prefix = "osc.udp://";
static const char *tcp_prefix = "osc.tcp://";
static const char *tcp_slip_prefix = "osc.slip.tcp://";
static const char *tcp_prefix_prefix = "osc.prefix.tcp://";
//FIXME serial

static int
lv2_osc_stream_init(LV2_OSC_Stream *stream, const char *url,
	const LV2_OSC_Driver *driv, void *data)
{
	memset(stream, 0x0, sizeof(LV2_OSC_Stream));

	char *dup = strdup(url);
	assert(dup);
	char *ptr = dup;
	char *tmp;

	if(strncmp(ptr, udp_prefix, strlen(udp_prefix)) == 0)
	{
		stream->slip = false;
		stream->socket_family = AF_INET;
		stream->socket_type = SOCK_DGRAM;
		stream->protocol = IPPROTO_UDP;
		ptr += strlen(udp_prefix);
	}
	else if(strncmp(ptr, tcp_prefix, strlen(tcp_prefix)) == 0)
	{
		stream->slip = true;
		stream->socket_family = AF_INET;
		stream->socket_type = SOCK_STREAM;
		stream->protocol = IPPROTO_TCP;
		ptr += strlen(tcp_prefix);
	}
	else if(strncmp(ptr, tcp_slip_prefix, strlen(tcp_slip_prefix)) == 0)
	{
		stream->slip = true;
		stream->socket_family = AF_INET;
		stream->socket_type = SOCK_STREAM;
		stream->protocol = IPPROTO_TCP;
		ptr += strlen(tcp_prefix);
	}
	else if(strncmp(ptr, tcp_prefix_prefix, strlen(tcp_prefix_prefix)) == 0)
	{
		stream->slip = false;
		stream->socket_family = AF_INET;
		stream->socket_type = SOCK_STREAM;
		stream->protocol = IPPROTO_TCP;
		ptr += strlen(tcp_prefix);
	}
	else
	{
		assert(false);
	}

	assert(ptr[0]);

	const char *node = NULL;
	const char *iface = NULL;
	const char *service = NULL;

	char *colon = strrchr(ptr, ':');

	// optional IPv6
	if(ptr[0] == '[')
	{
		stream->socket_family = AF_INET6;
		++ptr;
	}

	node = ptr;

	// optional IPv6
	if( (tmp = strchr(ptr, '%')) )
	{
		assert(stream->socket_family == AF_INET6);
		ptr = tmp;
		ptr[0] = '\0';
		iface = ++ptr;
	}

	// optional IPv6
	if( (tmp = strchr(ptr, ']')) )
	if(ptr)
	{
		assert(stream->socket_family == AF_INET6);
		ptr = tmp;
		ptr[0] = '\0';
		++ptr;
	}

	// mandatory IPv4/6
	ptr = strchr(ptr, ':');
	assert(ptr);
	ptr[0] = '\0';

	service = ++ptr;

	if(strlen(node) == 0)
	{
		node = NULL;
		stream->server = true;
	}

	fprintf(stderr, "%s , %s , %s\n", node, iface, service);

	stream->sock = socket(stream->socket_family, stream->socket_type,
		stream->protocol);
	assert(stream->sock >= 0);
	fcntl(stream->sock, F_SETFL, O_NONBLOCK);

	stream->driv = driv;
	stream->data = data;

	if(stream->socket_family == AF_INET) // IPv4
	{
		if(stream->server)
		{
			// resolve self address
			struct addrinfo hints;
			memset(&hints, 0x0, sizeof(struct addrinfo));
			hints.ai_family = stream->socket_family;
			hints.ai_socktype = stream->socket_type;
			hints.ai_protocol = stream->protocol;

			struct addrinfo *res;
			assert(getaddrinfo(node, service, &hints, &res) == 0);
			assert(res->ai_addrlen == sizeof(stream->peer.in4));

			stream->self.len = res->ai_addrlen;
			stream->self.in = *res->ai_addr;
			stream->self.in4.sin_addr.s_addr = htonl(INADDR_ANY); //FIXME

			freeaddrinfo(res);

			fprintf(stdout, "binding as IPv4 server\n");
			assert(bind(stream->sock, &stream->self.in, stream->self.len) == 0);
		}
		else // client
		{
			stream->self.len = sizeof(stream->self.in4);
			stream->self.in4.sin_family = stream->socket_family;
			stream->self.in4.sin_port = htons(0);
			stream->self.in4.sin_addr.s_addr = htonl(INADDR_ANY);

			fprintf(stdout, "binding as IPv4 client\n");
			assert(bind(stream->sock, &stream->self.in, stream->self.len) == 0);

			// resolve peer address
			struct addrinfo hints;
			memset(&hints, 0x0, sizeof(struct addrinfo));
			hints.ai_family = stream->socket_family;
			hints.ai_socktype = stream->socket_type;
			hints.ai_protocol = stream->protocol;

			struct addrinfo *res;
			assert(getaddrinfo(node, service, &hints, &res) == 0);
			assert(res->ai_addrlen == sizeof(stream->peer.in4));

			stream->peer.len = res->ai_addrlen;
			stream->peer.in = *res->ai_addr;

			freeaddrinfo(res);
		}

		if(stream->socket_type == SOCK_DGRAM)
		{
			//FIXME
		}
		else if(stream->socket_type == SOCK_STREAM)
		{
			const int flag = 1;
			assert(setsockopt(stream->sock, stream->protocol,
				TCP_NODELAY, &flag, sizeof(int)) == 0);

			if(stream->server)
			{
				assert(listen(stream->sock, 1) == 0);
			}
			else // client
			{
				connect(stream->sock, &stream->peer.in, stream->peer.len);
			}
		}
		else
		{
			assert(false);
		}
	}
	else if(stream->socket_family == AF_INET6) // IPv6
	{
		if(stream->server)
		{
			// resolve self address
			struct addrinfo hints;
			memset(&hints, 0x0, sizeof(struct addrinfo));
			hints.ai_family = stream->socket_family;
			hints.ai_socktype = stream->socket_type;
			hints.ai_protocol = stream->protocol;

			struct addrinfo *res;
			assert(getaddrinfo(node, service, &hints, &res) == 0);
			assert(res->ai_addrlen == sizeof(stream->peer.in6));

			stream->self.len = res->ai_addrlen;
			stream->self.in = *res->ai_addr;
			stream->self.in6.sin6_addr = in6addr_any; //FIXME
			if(iface)
			{
				stream->self.in6.sin6_scope_id = if_nametoindex(iface);
			}

			freeaddrinfo(res);

			fprintf(stdout, "binding as IPv6 server\n");
			assert(bind(stream->sock, &stream->self.in, stream->self.len) == 0);
		}
		else // client
		{
			stream->self.len = sizeof(stream->self.in6);
			stream->self.in6.sin6_family = stream->socket_family;
			stream->self.in6.sin6_port = htons(0);
			stream->self.in6.sin6_addr = in6addr_any;
			if(iface)
			{
				stream->self.in6.sin6_scope_id = if_nametoindex(iface);
			}

			fprintf(stdout, "binding as IPv6 client\n");
			assert(bind(stream->sock, &stream->self.in, stream->self.len) == 0);

			// resolve peer address
			struct addrinfo hints;
			memset(&hints, 0x0, sizeof(struct addrinfo));
			hints.ai_family = stream->socket_family;
			hints.ai_socktype = stream->socket_type;
			hints.ai_protocol = stream->protocol;

			struct addrinfo *res;
			assert(getaddrinfo(node, service, &hints, &res) == 0);
			assert(res->ai_addrlen == sizeof(stream->peer.in6));
			stream->peer.len = res->ai_addrlen;
			stream->peer.in = *res->ai_addr;
			if(iface)
			{
				stream->peer.in6.sin6_scope_id = if_nametoindex(iface);
			}

			freeaddrinfo(res);
		}

		if(stream->socket_type == SOCK_DGRAM)
		{
			//FIXME
		}
		else if(stream->socket_type == SOCK_STREAM)
		{
			if(stream->server)
			{
				assert(listen(stream->sock, 1) == 0);
			}
			else // client
			{
				connect(stream->sock, &stream->peer.in, stream->peer.len);
			}
		}
		else
		{
			assert(false);
		}
	}
	else
	{
		//FIXME
		assert(false);
	}

	free(dup);

	return 0;
}

static LV2_OSC_Enum
lv2_osc_stream_run(LV2_OSC_Stream *stream)
{
	LV2_OSC_Enum ev = LV2_OSC_NONE;

	// handle connections
	if( (stream->socket_type == SOCK_STREAM)
		&& (stream->server)
		&& (stream->fd <= 0)) // no peerr
	{
		stream->peer.len = sizeof(stream->peer.in);
		stream->fd = accept(stream->sock, &stream->peer.in, &stream->peer.len);

		if(stream->fd > 0)
		{
			fprintf(stderr, "accept: %i\n", stream->fd);
		}
	}

	// send everything
	if(stream->socket_type == SOCK_DGRAM)
	{
		if(stream->peer.len) // has a peer
		{
			const uint8_t *buf;
			size_t tosend;

			while( (buf = stream->driv->read_req(stream->data, &tosend)) )
			{
				const ssize_t sent = sendto(stream->sock, buf, tosend, 0,
					&stream->peer.in, stream->peer.len);

				if(sent == -1)
				{
					fprintf(stderr, "sendto: %s\n", strerror(errno));
					break;
				}
				if(sent != (ssize_t)tosend)
				{
					fprintf(stderr, "only sent %zi of %zu bytes", sent, tosend);
					break;
				}

				fprintf(stderr, "sent %zi bytes\n", sent);
				stream->driv->read_adv(stream->data);
				ev |= LV2_OSC_SEND;
			}
		}
	}
	else if(stream->socket_type == SOCK_STREAM)
	{
		const int fd = stream->server
			? stream->fd
			: stream->sock;

		if(fd > 0)
		{
			const uint8_t *buf;
			size_t tosend;

			while( (buf = stream->driv->read_req(stream->data, &tosend)) )
			{
				const ssize_t sent = send(fd, buf, tosend, 0);

				if(sent == -1)
				{
					fprintf(stderr, "sendto: %s\n", strerror(errno));
					break;
				}
				if(sent != (ssize_t)tosend)
				{
					fprintf(stderr, "only sent %zi of %zu bytes", sent, tosend);
					break;
				}

				fprintf(stderr, "sent %zi bytes\n", sent);
				stream->driv->read_adv(stream->data);
				ev |= LV2_OSC_SEND;
			}
		}
	}

	// recv everything
	if(stream->socket_type == SOCK_DGRAM)
	{
		uint8_t *buf;
		const size_t min_len = 1024;
		size_t max_len;

		while( (buf = stream->driv->write_req(stream->data, min_len, &max_len)) )
		{
			struct sockaddr in;
			socklen_t in_len = sizeof(in);

			const ssize_t recvd = recvfrom(stream->sock, buf, max_len, 0,
				&in, &in_len);

			if(recvd == -1)
			{
				if(errno == EAGAIN)
				{
					//fprintf(stderr, "recv: no messages\n");
					break;
				}

				fprintf(stderr, "recv: %s\n", strerror(errno));
				break;
			}
			else if(recvd == 0)
			{
				fprintf(stderr, "recv: peer shutdown\n");
				break;
			}

			stream->peer.len = in_len;
			stream->peer.in = in;

			fprintf(stderr, "received %zi bytes\n", recvd);
			stream->driv->write_adv(stream->data, recvd);
			ev |= LV2_OSC_RECV;

			break; //FIXME
		}
	}
	else if(stream->socket_type == SOCK_STREAM)
	{
		const int fd = stream->server
			? stream->fd
			: stream->sock;

		if(fd > 0)
		{
			uint8_t *buf;
			const size_t min_len = 1024;
			size_t max_len;

			while( (buf = stream->driv->write_req(stream->data, min_len, &max_len)) )
			{
				const ssize_t recvd = recv(fd, buf, 16, 0); //FIXME
				//const ssize_t recvd = recv(fd, buf, max_len, 0);

				if(recvd == -1)
				{
					if(errno == EAGAIN)
					{
						//fprintf(stderr, "recv: no messages\n");
						break;
					}

					fprintf(stderr, "recv: %s\n", strerror(errno));
					break;
				}
				else if(recvd == 0)
				{
					fprintf(stderr, "recv: peer shutdown\n");
					if(stream->fd)
					{
						close(stream->fd);
						stream->fd = 0;
					}
					break;
				}

				fprintf(stderr, "received %zi bytes\n", recvd);
				stream->driv->write_adv(stream->data, recvd);
				ev |= LV2_OSC_RECV;

				break; //FIXME
			}
		}
	}

	return ev;
}

static int
lv2_osc_stream_deinit(LV2_OSC_Stream *stream)
{
	if(stream->fd >= 0)
	{
		close(stream->fd);
		stream->fd = 0;
	}

	if(stream->sock >= 0)
	{
		close(stream->sock);
		stream->sock = 0;
	}

	//FIXME
	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LV2_OSC_STREAM_H
