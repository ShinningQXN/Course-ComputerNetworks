//
// FILE: client.c
//
// UPDATES:
//   2015-09-12 created
//
//  Courtesy to the instructor for sample code
//

#include <stdio.h>
#ifdef WIN32
#	include <winsock2.h>
#	pragma comment(lib, "Ws2_32.lib")
#	define close	closesocket
#	include <time.h>
int
gettimeofday(struct timeval *tp, void *tzp)
{
	time_t clock;
	struct tm tm;
	SYSTEMTIME wtm;

	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	tp->tv_sec = (long)clock;
	tp->tv_usec = wtm.wMilliseconds * 1000;

	return (0);
}

#else
#	include <stdlib.h>
#	include <string.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#	include <netinet/in.h>
#	include <netdb.h>
#	include <unistd.h>
#endif

int main(int argc, char* argv[]) {
#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

	if (argc != 5) {
		fprintf(stderr, "incorrect argument number");
		return -1;
	}

	// retrieve and check input arguments first
	struct hostent *host = gethostbyname(argv[1]);
	unsigned int port = atoi(argv[2]),
		size = atoi(argv[3]),
		count = atoi(argv[4]);

	if (!(
		host &&
		18000 <= port && port <= 18200 &&
		10 <= size &&
		1<= count && count <= 10000)
		) {
		fprintf(stderr, "invalid arguments");
		return -1;
	}

	// allocate buffer in heap (rather than in stack)
	char *sbuf = malloc(size), *rbuf = malloc(size);
	if (!sbuf || !rbuf) {
		perror("failed to allocate buffers");
		return -1;
	}

	// create a socket
	int sock;
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("opening TCP socket");
		return -1;
	}

	// fill in server address
	unsigned long server_addr = *(unsigned long*)host->h_addr_list[0];
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = server_addr;
	sin.sin_port = htons(port);

	// connect to the server
	if (connect(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
		//printf("%d",WSAGetLastError());
		perror("connect to server failed");
		return -1;
	}

	// set up the message to send
	*(short*)sbuf = htons(size);
	int i;
	for (i = 10; i < size; ++i)
		sbuf[i] = (char)i;

	long sum_latency = 0;

	// ping-pong messages
	for (i = 0; i < count; ++i) {
		//printf("\r%d / %d ", i+1, count);

		// encode the timestamp in the message
		struct timeval tv;
		gettimeofday(&tv, 0);
		*(long*)(sbuf + 2) = htonl(tv.tv_sec);
		*(long*)(sbuf + 6) = htonl(tv.tv_usec);
		
		// send the message
		send(sock, sbuf, size, 0);

		// wait and recv the message
		int nrecv = 0;
		while ((nrecv += recv(sock, rbuf + nrecv, size - nrecv, 0)) < size);
		/*if (nrecv < size) {
			perror("message imcomplete");
			abort();
		}*/
/*#ifdef WIN32
		//Sleep(1000);
#endif*/
		// get current time and record the latency
		long tv_sec = ntohl(*(long*)(rbuf + 2)),
			tv_usec = ntohl(*(long*)(rbuf + 6));
		gettimeofday(&tv, 0);
		sum_latency += (tv.tv_sec - tv_sec) * 1000000 + (tv.tv_usec - tv_usec);
	}

	double avg_latency_in_ms = (double)sum_latency / count / 1000;
	printf("average latency = %.3f ms, bandwidth = %f Mbps\n",
		avg_latency_in_ms, size*8*2 / avg_latency_in_ms / 1000);

	close(sock);
	free(rbuf);
	free(sbuf);

#ifdef WIN32
	WSACleanup();
#endif
	return 0;
}
