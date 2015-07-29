#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>

#define TIMEOUT (5 * 1000)
#define MAX_SIZE 1021
#define BACKLOG 10
#define BUFFER_SIZE 1000

/*
input: <"server" or "client">  <port-number> <host-name> <msg to send (if client)>
*/
int main(int argc, char *argv[])
{
	if (strcmp(argv[1], "server") == 0)
		return server_func(argv[2]);

	else
		return client_func(argv[4], argv[3], argv[2]);
}
// --------- THE server.c CODE ---------

int server_func(const char *port)
{
	int listen_sock, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct pollfd fds[MAX_SIZE+1];
	char buffer[BUFFER_SIZE], response[BUFFER_SIZE + 10];
	const int yes = 1;
	int rv, bytes_received, bytes_sent;
	int i, poll_rv, curr_size=0, num_of_fds=1, flag = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, port, &hints, &servinfo) != 0) {
		perror("getaddrinfo failed");
		return -1;	
	}
	for (p = servinfo; p != NULL; p = p->ai_next) {

		listen_sock=socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(listen_sock == -1) {
			perror("server: socket");
			continue;
		}
		rv = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		if (rv == -1) {
			perror("server: setsockopt");
			close(listen_sock);
			freeaddrinfo(servinfo);
			return -1;
		}
		if (bind(listen_sock, p->ai_addr, p->ai_addrlen) == -1) {
 			close(listen_sock);
			perror("server: bind");
			continue;
		}

		break; //if we got here - we binded successfully
	}
	if (p == NULL) { //if all binds failed
		fprintf(stderr, "server: failed to bind\n");
		return -1;
	}
	freeaddrinfo(servinfo);
	if (listen(listen_sock, BACKLOG) == -1) {
		perror("listen");
		close(listen_sock);
		return -1;
	}

	memset(fds, 0, sizeof(fds));
	fds[0].fd = listen_sock;
	fds[0].events = POLLIN;	

	printf("server: listening...\n");
	while (flag == 0) {
		poll_rv = poll(fds, num_of_fds, TIMEOUT);
		if (poll_rv < 0) {
			perror("server: poll");
			flag = -1;
		}
		if (poll_rv == 0) {
			printf("poll timed out\n");
			continue;
		}
		curr_size = num_of_fds;
		for (i = 0; i < curr_size; i++) {
			if(fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
				if(fds[i].fd == listen_sock) {
					flag = -1;
					printf("problem with listening socket. terminating\n");
					break;
				}
				printf("problem with connection number %d. closing socket\n",i);
				close(fds[i].fd);
				fds[i].fd = -1;
			}
			else {
				if ((fds[i].fd == listen_sock) && (fds[i].revents & POLLIN) && (num_of_fds < (MAX_SIZE + 1))) {
					new_fd = accept(listen_sock, NULL, NULL);
					if (new_fd < 0) {
						perror("server: accept");
						flag = -1;
						break;
					}
					printf("new connection\n");
					fds[num_of_fds].fd = new_fd;
					fds[num_of_fds].events = POLLIN;
					num_of_fds++;				
				}
				else {
					if (fds[i].revents & POLLIN) {
						memset(buffer, '\0', sizeof(buffer));
						bytes_received=recv(fds[i].fd, buffer, BUFFER_SIZE-1, 0);
						if (bytes_received == -1) {
							return -1;
							break;
						}
						memset(response, '\0', sizeof(response));
						strcpy(response, "received: ");
						strcat(response, buffer);
						bytes_sent = send(fds[i].fd, response, 10 + bytes_received, 0);
						if (bytes_sent == -1) {
							perror("send");
							flag = -1;
							break;
						} //FIXME: handle case where not all were sent?

					}
/*					if (fds[i].revents & POLLOUT) {
						memset(response, '\0', sizeof(response));
						strcpy(response, "received: ");
						strcat(response, buffer);
						bytes_sent = send(new_fd, response, 10 + bytes_received, 0);
						if (bytes_sent == -1) {
							perror("send");
							close(new_fd);
							close(listen_sock); //FIXME: more sockets may be open
							return -1;
						} //FIXME: handle case where not all were sent?
					}*/
				} 
			}
			fds[i].revents = 0;
		}
	}
	for (i = 0; i < num_of_fds; i++) 
		if (fds[i].fd != -1)
			close(fds[i].fd);
	return flag;
}

// --------- THE client.c CODE ----------

int client_func(const char *msg, const char *serv_addr, const char *port)
{
	int msg_len=strlen(msg);
	int sockfd, bytes_sent, bytes_received;
	char buffer[BUFFER_SIZE];
	struct addrinfo hints, *servinfo, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(serv_addr, port, &hints, &servinfo) != 0) {
		perror("getaddrinfo failed");
		return -1;
	}
	
	for(p = servinfo; p != NULL; p = p->ai_next) {
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1) {
			perror("client: socket");
			continue;
		}
		
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}
		
		break;
	}
	
	if(p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return -1;
	}
	///
	freeaddrinfo(servinfo);

	while (1) {
		bytes_sent=send(sockfd, msg, msg_len, 0);
		printf("sent\n");//FIXME: !!!!!!!!!!!!!!!!!!	
		//FIXME: handle case where not all were sent (or -1), and is 0 ok?
		bytes_received=recv(sockfd, buffer, BUFFER_SIZE-1, 0);
		//FIXME: is 0 ok? check for -1
		printf("\nreply from server\n '%*.*s'\n\n",bytes_received, bytes_received, buffer);
		sleep(10);
	}
	close(sockfd);
	return 0;
}
