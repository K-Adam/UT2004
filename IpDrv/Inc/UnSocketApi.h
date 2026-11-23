/*============================================================================
	UnSocketApi.h: The platform's socket API under WinSock names.
	No engine types, so the tests can include it before the engine headers.
============================================================================*/

#ifndef UNSOCKETAPI_H
#define UNSOCKETAPI_H

#if _WIN32
	#define __WINSOCK__ 1

	#pragma pack(push,8)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#pragma pack(pop)

	typedef int					SOCKLEN;
	#define MSG_NOSIGNAL		0
	#define UDP_ERR_PORT_UNREACH WSAECONNRESET
#else
	#include <stdio.h>
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <sys/uio.h>
	#include <sys/ioctl.h>
	#include <sys/time.h>
	#include <errno.h>
	#include <fcntl.h>

	typedef int			SOCKET;
	typedef struct sockaddr		SOCKADDR;
	typedef struct sockaddr_in	SOCKADDR_IN;
	typedef struct linger		LINGER;
	typedef struct timeval		TIMEVAL;
	typedef socklen_t		SOCKLEN;

	#define INVALID_SOCKET		-1
	#define SOCKET_ERROR		-1

	#define UDP_ERR_PORT_UNREACH ECONNREFUSED

	#define WSAEWOULDBLOCK		EAGAIN
	#define WSAEINPROGRESS		EINPROGRESS

	#define WSAENOTSOCK		ENOTSOCK
	#define LPSOCKADDR		sockaddr*

	#define closesocket		close
	#define ioctlsocket		ioctl
	#define WSAGetLastError()	errno
#endif

inline int SetNonBlocking( SOCKET Socket )
{
#if __WINSOCK__
	u_long NoBlock = 1;
	return ioctlsocket( Socket, FIONBIO, &NoBlock ) == 0;
#else
	return fcntl( Socket, F_SETFL, fcntl( Socket, F_GETFL, 0 ) | O_NONBLOCK ) == 0;
#endif
}

#endif
