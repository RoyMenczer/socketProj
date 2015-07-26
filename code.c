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

#define PORT "1771" //arbitrary port number
#define BACKLOG 10
#define BUFFER_SIZE 1000


int main(int argc, char *argv[]) //1 variable? 2?
{
	return 0;	
}
// --------- THE server.c CODE ---------

int server_func()
{
	int listen_sock, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage client_addr;
	socklen_t sin_size;
	char buffer[BUFFER_SIZE];
	///

	memseset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
		fprintf("getaddrinfo failed\n");
		return -1;	
	}
	for (p = servinfo; p != NULL; p = p->ai_next) {

		listen_sock=socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(listen_fd == -1) {
			perror("server: socket");
			continue;
		}
		///
		if(bind(listen_sock, p->ai_addr, p->ai_addrlen) == -1) {
 			close(listen_sock);
			perror("server: bind");
			continue;
		}

		break; //if we got here - we binded successfully
	}
	if(p == NULL) { //if all binds failed
		fprintf(stderr, "server: failed to bind\n");
		return -1;
	}
	freeaddrinfo(servinfo);
	if(listen(listen_sock, BACKLOG) == -1) {
		perror("listen");
		close(listen_sock);
		return -1;
	}
	///
	printf("server: listening...\n");
	
	while(1) { //accept() loop
		sin_size = sizeof(client_addr);
		new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
		if(new_fd == -1) {
			perror("accept");
			continue;
		}
	}
	
	return 0;
}

// --------- THE client.c CODE ----------


