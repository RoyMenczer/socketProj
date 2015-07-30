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
#include <fcntl.h>

#define TIMEOUT (5 * 1000)
#define MAX_SESSIONS 1021
#define BACKLOG 10
#define MSG_SIZE 64

struct session {
	int state;
	char r_msg[MSG_SIZE];
	char s_msg[MSG_SIZE + 11];
	int s_index;
	int bytes_received;
};

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
	struct session sessions[MAX_SESSIONS + 1];
	struct pollfd fds[MAX_SESSIONS+1];
	const int yes = 1;
	int rv;
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
		if (fcntl(listen_sock, F_SETFL, O_NONBLOCK) < 0) {
			perror("server: listen_sock fcntl");
			close(listen_sock);
			return -1;
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
	for (i = 1; i < MAX_SESSIONS + 1; i++) {
		fds[i].fd = -1;
		sessions[i].state = -1;
		sessions[i].s_index = -1;
	}
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
			if(fds[i].revents == 0)
				continue;
			if(fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
				if(fds[i].fd == listen_sock) {
					flag = -1;
					printf("problem with listening socket. terminating\n");
					break;
				}
				printf("problem with connection number %d. closing socket\n",i);
				close(fds[i].fd);
				fds[i].fd = -1;
				sessions[i].state = -1;
			}
			else {
				if ((fds[i].fd == listen_sock) && (fds[i].revents & POLLIN) && (num_of_fds < (MAX_SESSIONS + 1))) {
					new_fd = accept(listen_sock, NULL, NULL);
					if (new_fd < 0) {
						perror("server: accept");
						flag = -1;
						break;
					}
					if (fcntl(new_fd, F_SETFL, O_NONBLOCK) < 0) {
						perror("server: fcntl");
						flag = -1;
						break;
					}
					printf("new connection\n");
					fds[num_of_fds].fd = new_fd;
					fds[num_of_fds].events = POLLIN;
					sessions[num_of_fds].state = 0;
					num_of_fds++;
				}
				else {
					if ((fds[i].revents == POLLIN) && (sessions[i].state == 0)) {
						memset(sessions[i].r_msg, '\0', sizeof(MSG_SIZE));
						sessions[i].bytes_received = recv(fds[i].fd, sessions[i].r_msg, MSG_SIZE - 1, 0);
						if (sessions[i].bytes_received == -1) {
							flag = -1;
							break;
						}
						sessions[i].state = 1;
						fds[i].events = POLLOUT;
						fds[i].revents = 0;
						continue;
					}
					if ((fds[i].revents == POLLOUT) && (sessions[i].state == 1)) {
						memset(sessions[i].s_msg, '\0', sizeof(MSG_SIZE + 10));
						strcpy(sessions[i].s_msg, "received: ");
						strcat(sessions[i].s_msg, sessions[i].r_msg);
						strcat(sessions[i].s_msg, "0");
						sessions[i].s_index = send(fds[i].fd, sessions[i].s_msg, 10 + sessions[i].bytes_received, 0);
						if (sessions[i].s_index == -1) {
							perror("send");
							flag = -1;
							break;
						} //FIXME: handle case where not all were sent?
						sessions[i].state = 0;
						fds[i].events = POLLIN;
						fds[i].revents = 0;
						continue;
					}
					else {
						printf("unexpected event at session number %d. closing socket\n",i);
						close(fds[i].fd);
						fds[i].fd = -1;
						sessions[i].state = -1;
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
	int msg_len=strlen(msg), rv, success = 1;
	int sockfd, bytes_sent, bytes_received;
	char buffer[MSG_SIZE + 11];
	char s_msg[MSG_SIZE + 1];
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
		if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0) {
			perror("client: fcntl");
			continue;
		}
		while (1) {
			rv = connect(sockfd, p->ai_addr, p->ai_addrlen);
			if (rv == -1) {
				if (errno != EWOULDBLOCK) {
					perror("client: connect");
					break;
				}
			}
			else {
				success = 1;
				break;
			}
		}
		if (success == 1)
			break;
	}
	
	if(p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return -1;
	}
	///
	freeaddrinfo(servinfo);

	while (1) {
		memset(s_msg, '\0', MSG_SIZE + 1);
		strcpy(s_msg, msg);
		strcat(s_msg, "0");
		memset(buffer, '\0', MSG_SIZE + 10);
		bytes_sent=send(sockfd, s_msg, msg_len + 1, 0);
		printf("sent\n");//FIXME: !!!!!!!!!!!!!!!!!!	
		//FIXME: handle case where not all were sent (or -1), and is 0 ok?
		bytes_received=recv(sockfd, buffer, MSG_SIZE - 1, 0);
		buffer[bytes_received - 1] = '\0';
		//FIXME: is 0 ok? check for -1
		printf("\nreply from server\n '%s'\n\n", buffer);
		sleep(5);
	}
	close(sockfd);
	return 0;
}
