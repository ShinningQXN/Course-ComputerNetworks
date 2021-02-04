//
// FILE: server.c
//
// UPDATES:
//   2015-09-12 created, finished ping-pong server
//   2015-09-13 added web server part
//   2015-09-13 fixed bugs with large files in WWW mode
//   2015-09-22 added more comments
//
//  Courtesy to the instructor for sample code
//
// PS:
//   1. Some non-C89 features such as variable declaration between codes are used.
//   2. This code can compile on both Windows and Linux environments.
//

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef WIN32
#	include <winsock2.h>
#	pragma comment(lib, "Ws2_32.lib")
#	define close	closesocket
#	define MSG_DONTWAIT	0
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
#	include <fcntl.h>
#	include <arpa/inet.h>	// very important for 'inet_ntoa'
#	include <unistd.h>
#endif

//#define VERBOSE

// A server should be able to handle multiple connections,
// thus we maintain all the clients in a list.
#define BUF_MAX		655360//65536
struct node {
	int socket;
	struct sockaddr_in client_addr;
	enum { idle, pending_read, pending_write } status;
	union {
		char *buf;	// read-write buffer
		short *psize_netorder;
	};
	int total_size;	// total bytes to transmit
	int cur_size;	// number of bytes transmitted
	//FILE *f;
	struct node *next;
};

// Close and dump a socket link, free the memory it consumes.
void close_and_dump(struct node *head, struct node *p) {
	struct node *cur;
	printf("Close connection: sock = %d, IP = %s\n",
		p->socket, inet_ntoa(p->client_addr.sin_addr));
	close(p->socket);
	for (cur = head; cur->next; cur = cur->next) if (cur->next == p) {
		struct node *t = cur->next;
		cur->next = t->next;
		free(t->buf);
		free(t);
		break;
	}
}

// Add a socket link to the list.
int add(struct node *head, int socket, struct sockaddr_in addr) {
	struct node *n;
	n = (struct node *)malloc(sizeof(struct node));
	if (n == 0) {
		perror("malloc error. unable to insert node.");
		return -1;
	}
	n->socket = socket;
	n->client_addr = addr;
	n->status = idle;
	n->buf = malloc(BUF_MAX);
	if (n->buf == 0) {
		perror("malloc error. unable to insert node.");
		free(n);
		return -1;
	}
	n->cur_size = 0;
	n->next = head->next;
	head->next = n;
	return 0;
}

// Entry-point of the program.
int main(int argc, char* argv[]) {
#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

	unsigned int port;
	//char *root_directory = argv[3];
	port = atoi(argv[1]);
	int b_web;

	// run as pingping or web server, decided by input args
	if (argc == 4 && strcmp(argv[2], "www") == 0) {
		// run in web server mode
		//port = 80;	// HTTP port
		b_web = 1;
#ifdef WIN32
		SetCurrentDirectoryA(argv[3]);
#else
		chdir(argv[3]);
#endif
	}
	else if (argc >= 2) {
		// run in ping-pong mode
		if (!(18000 <= port && port <= 18200)) {
			fprintf(stderr, "invalid arguments");
			return -1;
		}
		b_web = 0;
	}
	else {
		fprintf(stderr, "incorrect argument number");
		return -1;
	}

	// Open a socket
	int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		perror("opening TCP socket");
		abort();
	}

	int optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		perror("setting TCP socket option");
		abort();
	}

	// Bind the socket to specific port
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("binding socket to address");
		abort();
	}

	// listening on the port
	if (listen(sock, 5) < 0) {
		perror("listen on socket failed");
		abort();
	}

	//printf(port == 80 ? "www server set up, with port = %d, sock = %d\n" :
	//	"ping-pong server set up, with port = %d, sock = %d\n", port, sock);
	if (b_web)
		printf("www server set up, with port = %d, sock = %d, rootdir = %s\n",
			port, sock, argv[3]);
	else
		printf("ping-pong server set up, with port = %d, sock = %d\n", port, sock);

	struct node head;
	head.next = 0;

	// The main loop
	//   use 'fd_set' to check for data transmission,
	//   dispatch to specific codes(ping-pong/web) when stream data received.
	while (1) {
		fd_set read_set, write_set;
		struct node *cur, *cur_next;

		FD_ZERO(&read_set);
		FD_ZERO(&write_set);

		FD_SET(sock, &read_set);

		int max = sock;
		for (cur = head.next; cur; cur = cur->next) {
			// By design, concurrent reading and writing is not allowed.
			// A socket can either be in reading mode or writing mode,
			// thus, it's okay to only put it in one list.
			if (cur->status == pending_write)
				FD_SET(cur->socket, &write_set);
			else
				FD_SET(cur->socket, &read_set);
			if (cur->socket > max) max = cur->socket;
		}

		// Set the 'select' time-out to 100,000 us.
		struct timeval time_out;
		time_out.tv_sec = 0;
		time_out.tv_usec = 100000;

		int r;
		r = select(max + 1, &read_set, &write_set, NULL, &time_out);
		if (r < 0) {
			perror("select failed");
			abort();
		}
		// No error, also no activity.
		if (r == 0) continue;

		// new incoming connection
		if (FD_ISSET(sock, &read_set)) {
			int new_sock = accept(sock, (struct sockaddr*)&addr, &addr_len);

			if (new_sock < 0) {
				perror("error in accepting connection");
				abort();
			}

#ifdef WIN32
			u_long non_blk = 1;
			ioctlsocket(new_sock, FIONBIO, &non_blk);
#else
			if (fcntl(new_sock, F_SETFL, O_NONBLOCK) < 0) {
				perror("making socket non-blocking");
				abort();
			}
#endif
			printf("Accepted connection. Client IP = %s\n", inet_ntoa(addr.sin_addr));

			// try inserting a new node representing the connection
			if (add(&head, new_sock, addr) < 0) {
				printf("Close connection: sock = %d, IP = %s\n",
					new_sock, inet_ntoa(addr.sin_addr));
				close(new_sock);
			}
		}

		//fprintf(stderr, "debug\n");

		// Check each connection for reading or writing
		for (cur = head.next; cur; cur = cur_next) {
			cur_next = cur->next;

			if (FD_ISSET(cur->socket, &write_set)) {
				// It's okay to write know, send the remaining pending data.

				//int packet_size = ntohs(*cur->psize_netorder);
				int nsent = send(cur->socket, cur->buf + cur->cur_size,
					cur->total_size - cur->cur_size, MSG_DONTWAIT);
				if (nsent < 0) {
					if (errno == EAGAIN)
						nsent = 0;
					else {
						perror("send error");
						close_and_dump(&head, cur);
						continue;
					}
				}
				// Record how many bytes in total we have sent.
				cur->cur_size += nsent;
#ifdef VERBOSE
				printf("Sent %d / %d bytes to sock = %d, IP = %s\n",
					cur->cur_size, cur->total_size, cur->socket, inet_ntoa(cur->client_addr.sin_addr));
#endif
				// Transmission completed. Go back to 'idle' status.
				if (cur->cur_size == cur->total_size) {
					cur->status = idle;
					cur->cur_size = 0;
				}
			}
			else if (FD_ISSET(cur->socket, &read_set)) {
				// There's incoming data, append the received data to the buffer.

				int nrecv = recv(cur->socket, cur->buf + cur->cur_size, BUF_MAX-1 - cur->cur_size, 0);

				if (nrecv <= 0) {
					if (nrecv < 0) perror("recv error");
					close_and_dump(&head, cur);
					continue;
				}
				// 
				cur->cur_size += nrecv;
				cur->status = pending_read;
#ifdef VERBOSE
				printf("Received %d / %d bytes from sock = %d, IP = %s\n",
					cur->cur_size, ntohs(*cur->psize_netorder), cur->socket, inet_ntoa(cur->client_addr.sin_addr));
#endif

				if (b_web) {
					// ========================
					//    www server
					// ========================

					// We have alreay received a string long enough to process.
					if (cur->cur_size > 16 &&
						cur->buf[cur->cur_size - 2] == '\r' && cur->buf[cur->cur_size - 1] == '\n') 
					{
						cur->buf[cur->cur_size - 2] = 0;
						
						/*char *p = strstr(cur->buf, "GET");
						if (p == NULL) {
							continue;
						}*/

						char *p = cur->buf;

						// Check if it's a HTTP GET request, return NOT IMPLEMENTED if not.
						if (memcmp(p, "GET", 3) != 0) {
							strcpy(cur->buf, "HTTP/1.1 501 Not Implemented\r\n"
								"Content-Type: text/html\r\n"
								"Content-Length: 0\r\n"
								"\r\n");
							goto _send_back;
							//continue;
						}

						char *q = strstr(p, "\r\n");
						if (q == NULL)
							continue;

						cur->status = idle;
						cur->cur_size = 0;

						// split the message string
						char filename[512], *httpproto = filename + 256;
						sscanf(p + 4, "%s%s", filename + 1, httpproto);
						filename[0] = '.';

						if (memcmp(httpproto, "HTTP/", 5) == 0) {
							// check path restrictions, no '..' is allowed.
							if (strstr(filename, "../")) {
								// return HTTP 400
								strcpy(cur->buf, "HTTP/1.1 400 Bad Request\r\n"
									"Content-Type: text/html\r\n"
									"Content-Length: 44\r\n"
									"\r\n"
									"HTTP 400<br>Bad request, please check your URL.");
								goto _send_back;
							}

							// Use function 'stat' to check if the path leads to a folder.
							// If it is, load the 'index.html' page under the folder by default.
							struct stat s;
							s.st_mode = 0;
							stat(filename + 1, &s);

							if ((s.st_mode & S_IFMT) == S_IFDIR) {
								strcat(filename, "/index.html");
							}
							//if ((s.st_mode & S_IFMT) == S_IFREG) {
								// If it is regular file, open and send the request file back
							FILE *f = fopen(filename, "r");

							if (f == NULL) {
								/*// is a directory, search for 'index.html'
								switch (errno) {
								case EISDIR:
									strcat(filename, "/index.html");
									f = fopen(filename, "r");
									if (f != NULL)
									break;
								default:*/
								// return HTTP 404 Not Found
								strcpy(cur->buf, "HTTP/1.1 404 Not Found\r\n"
									"Content-Type: text/html\r\n"
									"Content-Length: 50\r\n"
									"\r\n"
									"HTTP 404<br>File not found, please check your URL.");

								cur->total_size = strlen(cur->buf);
								//	break;
								//}
							}
							else {
								// get file size
								fseek(f, 0, SEEK_END);
								long filesize = ftell(f);
								fseek(f, 0, SEEK_SET);

								// ***TODO: more elegant way to deal with
								//  the case when file size > BUF_MAX
								if (filesize > BUF_MAX - 100) {
									//filesize = BUF_MAX - 100;
									char *new_buf = malloc(filesize + 100);
									if (new_buf == 0) {
										strcpy(cur->buf, "HTTP/1.1 500 Internal Server Error\r\n"
											"Content-Type: text/html\r\n"
											"Content-Length: 0\r\n"
											"\r\n");
										goto _send_back;
									}
									free(cur->buf);
									cur->buf = new_buf;
								}
								
								// fill http response header
								sprintf(cur->buf, "HTTP/1.1 200 OK\r\n"
									"Content-Type: text/html\r\n"
									"Content-Length: %u\r\n"
									"\r\n",
									filesize);

								int header_len = strlen(cur->buf);
								fread(cur->buf + header_len, filesize, 1, f);
								fclose(f);

								cur->total_size = header_len + filesize;
								//cur->total_size = header_len + filesize + 1;
								//cur->buf[cur->total_size - 1] = 0;
							}

						_send_back:;
							// Try sending the response, the connection will go to 
							// 'pending_write' status if it fails in sending all the data.
							int nsent = send(cur->socket, cur->buf, cur->total_size, MSG_DONTWAIT);
							if (nsent < 0) {
								if (errno == EAGAIN)
									nsent = 0;
								else {
									perror("send error");
									close_and_dump(&head, cur);
									continue;
								}
							}
#ifdef VERBOSE
							printf("Sent %d / %d bytes to sock = %d, IP = %s\n",
								nsent, cur->total_size, cur->socket, inet_ntoa(cur->client_addr.sin_addr));
#endif			
							if (nsent != cur->total_size) {
								cur->status = pending_write;
								cur->cur_size = nsent;
							}
						}
					}else if (cur->cur_size > BUF_MAX - 64)
						// haven't received a valid command for a long time,
						// discard the entire buffer.
						cur->cur_size = 0;
				}
				else {
					// ==============================
					//     ping-pong server
					// ==============================

					if (cur->cur_size > 2) {
						//int packet_size = ntohs(*cur->psize_netorder);
						cur->total_size = ntohs(*cur->psize_netorder);
						/*if (cur->cur_size > cur->total_size) {
							printf("received %d bytes, more than %d bytes which it should be.\n",
							cur->cur_size, packet_size);
							close_and_dump(&head, cur);
						}
						else*/ 
						if (cur->cur_size == cur->total_size) {
#ifdef VERBOSE
							printf("received a ping from sock = %d, IP = %s, pong back\n",
								cur->socket, inet_ntoa(cur->client_addr.sin_addr));
#endif
							// Try sending the pong message, the connection will go to 
							// 'pending_write' status if it fails in sending all the data.
							int nsent = send(cur->socket, cur->buf, cur->total_size, MSG_DONTWAIT);
							if (nsent < 0) {
								if (errno == EAGAIN)
									nsent = 0;
								else {
									perror("send error");
									close_and_dump(&head, cur);
									continue;
								}
							}
#ifdef VERBOSE
							printf("Sent %d / %d bytes to sock = %d, IP = %s\n",
								nsent, cur->total_size, cur->socket, inet_ntoa(cur->client_addr.sin_addr));
#endif			
							if (nsent != cur->total_size) {
								// Goto 'pending_write' mode
								cur->status = pending_write;
								cur->cur_size = nsent;
							}
							else {
								// Transmission completed. Go back to 'idle' status.
								cur->status = idle;
								cur->cur_size = 0;
							}
						}
					}
				}// end if (b_web)
			}
		}// end for each socket
	}// end while

	close(sock);

#ifdef WIN32
	WSACleanup();
#endif
	return 0;
}
