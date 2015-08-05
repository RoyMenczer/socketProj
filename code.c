#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>

#include <rdma/rsocket.h>
#include <infiniband/ib.h>

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
input: <"server" or "client">  <port-number> <host-gid> <msg to send (if client)>
*/
int main(int argc, char *argv[])
{
	if (strcmp(argv[1], "server") == 0)
		return server_func(argv[2], argv[3]);

	else
		return client_func(argv[4], argv[3], argv[2]);
}


uint64_t htonll(uint64_t n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    return n; 
#else
    return (((uint64_t)htonl(n)) << 32) + htonl(n >> 32);
#endif
}

// --------- THE server.c CODE ---------

int server_func(const char *port, const char *address)
{
	int listen_rsock = -1, new_fd = -1;
	struct session sessions[MAX_SESSIONS + 1];
	struct pollfd fds[MAX_SESSIONS+1];
	const int yes = 1;
	int rv;
	int i, poll_rv, curr_size=0, num_of_fds=1, flag = 0;
	uint16_t pkey = 0xffff;	//from admin_connect_init
	struct sockaddr_ib addr;
	int port_num;
	union ibv_gid dgid;

	listen_rsock=rsocket(AF_IB, SOCK_STREAM, 0);
	if(listen_rsock == -1) {
		perror("server: rsocket");
		return -1;
	}
	if (fcntl(listen_rsock, F_SETFL, O_NONBLOCK) < 0) {
		perror("server: listen_rsock fcntl");
		rclose(listen_rsock);
		return -1;
	}
	rv = rsetsockopt(listen_rsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (rv == -1) {
		perror("server: rsetsockopt");
		rclose(listen_rsock);
		return -1;
	}
	
	port_num = strtol(port, NULL, 10);
	printf("port-num=%d\n",port_num);

	addr.sib_family = AF_IB;
	addr.sib_pkey = pkey;
	addr.sib_flowinfo = 0;
	addr.sib_sid = htonll(((uint64_t) RDMA_PS_TCP << 16) + port_num);
	addr.sib_sid_mask = htonll(RDMA_IB_IP_PS_MASK | RDMA_IB_IP_PORT_MASK);
	addr.sib_scope_id = 0;
	
	rv = inet_pton(AF_INET6, address, &dgid);
	if (rv <=0) {
		perror("server: inet_pton");
		rclose(listen_rsock);
		return -1;
	}
	memcpy(&addr.sib_addr, &dgid, sizeof(dgid)); //FIXME: might cause problems

	if (rbind(listen_rsock, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
 		rclose(listen_rsock);
		perror("server: rbind");
		return -1;
	}

	if (rlisten(listen_rsock, BACKLOG) == -1) {
		perror("rlisten");
		rclose(listen_rsock);
		return -1;
	}

	memset(fds, 0, sizeof(fds));
	fds[0].fd = listen_rsock;
	fds[0].events = POLLIN;	
	for (i = 1; i < MAX_SESSIONS + 1; i++) {
		fds[i].fd = -1;
		sessions[i].state = -1;
		sessions[i].s_index = 0;
		sessions[i].bytes_received = 0;
		memset(sessions[i].s_msg, '\0', sizeof(sessions[i].s_msg));
		memset(sessions[i].r_msg, '\0', sizeof(sessions[i].r_msg));
	}
	printf("server: listening...\n");
	while (flag == 0) {
		poll_rv = rpoll(fds, num_of_fds, TIMEOUT);
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
				if(fds[i].fd == listen_rsock) {
					flag = -1;
					printf("problem with listening rsocket. terminating\n");
					break;
				}
				printf("problem with connection number %d. closing socket\n",i);
				close(fds[i].fd);
				fds[i].fd = -1;
				sessions[i].state = -1;
			}
			else {
				if ((fds[i].fd == listen_rsock) && (fds[i].revents & POLLIN) && (num_of_fds < (MAX_SESSIONS + 1))) {
					new_fd = raccept(listen_rsock, NULL, NULL);
					if (new_fd < 0 && (errno != EWOULDBLOCK)) {
						perror("server: raccept");
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
						rv = rrecv(fds[i].fd, (sessions[i].r_msg + sessions[i].bytes_received), MSG_SIZE + 1 - sessions[i].bytes_received, 0);
						if (rv == -1) {
							if (errno == EWOULDBLOCK) {
								fds[i].revents = 0;
								continue;
							}
							else {
								flag = -1;
								break;
							}
						}
						sessions[i].bytes_received += rv;
						if (sessions[i].r_msg[sessions[i].bytes_received - 1] == '0') {
							sessions[i].state = 1;
							fds[i].events = POLLOUT;
							strcpy(sessions[i].s_msg, "received: ");
							strcat(sessions[i].s_msg, sessions[i].r_msg);
							memset(sessions[i].r_msg, '\0', sizeof(sessions[i].r_msg));
						}
						fds[i].revents = 0;
						continue;
					}
					if ((fds[i].revents == POLLOUT) && (sessions[i].state == 1)) {
						rv = rsend(fds[i].fd, (sessions[i].s_msg + sessions[i].s_index), 10 + sessions[i].bytes_received - sessions[i].s_index, 0);
						if (rv == -1) {
							if (errno == EWOULDBLOCK) {
								fds[i].revents = 0;
								continue;
							}
							else {
								flag = -1;
								break;
							}
						}
						sessions[i].s_index += rv;
						if (sessions[i].s_msg[sessions[i].s_index -1] == '0') {
							sessions[i].state = 0;
							fds[i].events = POLLIN;
							sessions[i].s_index = 0;
							memset(sessions[i].s_msg, '\0', sizeof(sessions[i].s_msg));
							sessions[i].bytes_received = 0;
						}
						fds[i].revents = 0;
						continue;
					}
					else {
						printf("unexpected event at session number %d. closing rsocket\n",i);
						rclose(fds[i].fd);
						fds[i].fd = -1;
						sessions[i].state = -1;
					}
				}
			}
		}
	}
	for (i = 0; i < num_of_fds; i++) 
		if (fds[i].fd != -1)
			rclose(fds[i].fd);
	return flag;
}



// --------- THE client.c CODE ----------

int client_func(const char *msg, const char *serv_addr, const char *port)
{
	int msg_len=strlen(msg), rv, success = 1, res = 0, flag = 0;
	int rsockfd = -1, bytes_sent, bytes_received, port_num;
	char buffer[MSG_SIZE + 11];
	char s_msg[MSG_SIZE + 1];
	uint16_t pkey = 0xffff;	//from admin_connect_init
	struct sockaddr_ib dst_addr;
	union ibv_gid dgid;

	rsockfd = rsocket(AF_IB, SOCK_STREAM, 0);
	if (rsockfd == -1) {
		perror("client: rsocket");
		return -1;
	}
	if (fcntl(rsockfd, F_SETFL, O_NONBLOCK) < 0) {
		perror("client: fcntl");
		rclose(rsockfd);
		return -1;
	}

	port_num = strtol(port, NULL, 10);
	printf("port-num = %d\n", port_num);

	dst_addr.sib_family = AF_IB;
	dst_addr.sib_pkey = htons(pkey);
	dst_addr.sib_flowinfo = 0;
	dst_addr.sib_sid = 
		htonll(((uint64_t) RDMA_PS_TCP << 16) + port_num);
	dst_addr.sib_sid_mask = htonll(RDMA_IB_IP_PS_MASK);
	dst_addr.sib_scope_id = 0;

	rv = inet_pton(AF_INET6, serv_addr, &dgid);
	if (rv <= 0) {
		perror("client: unet_pton");
		rclose(rsockfd);
		return -1;
	}
	
	memcpy(&dst_addr.sib_addr, &dgid, sizeof(dgid));

	while (1) {
		rv = rconnect(rsockfd, (const struct sockaddr *) &dst_addr,
			sizeof(dst_addr));
		if (rv == -1) {
			if (errno != EWOULDBLOCK) {
				perror("client: connect");
				rclose(rsockfd);
				return -1;
			}
		}
		else {
			success = 1;
			break;
		}
	}

	
	while (flag == 0) {
		bytes_sent = 0;
		bytes_received = 0;
		memset(s_msg, '\0', MSG_SIZE + 1);
		strcpy(s_msg, msg);
		strcat(s_msg, "0");
		memset(buffer, '\0', MSG_SIZE + 10);
		while (s_msg[bytes_sent -1] != '0') {
			res = rsend(rsockfd, (s_msg + bytes_sent), msg_len + 1 - bytes_sent, 0);
			if (res == -1) {
				perror("client:");
				if (errno != EWOULDBLOCK) {
					perror("client: rsend");
					flag == -1;
					break;
				}
			}
			else
				bytes_sent += res;
			sleep(5);
		}
		if (flag == -1)
			break;
		printf("sent\n");
		while (buffer[bytes_received -1] != '0') {
			res = rrecv(rsockfd, (buffer + bytes_received), MSG_SIZE + 11 - bytes_received, 0);
			if (res == -1) {
				perror("");
				if(errno != EWOULDBLOCK) {
					perror("client: recv");
					flag == -1;
					break;
				}
			}
			else
				bytes_received += res;
//			sleep(5);
		}
		if (flag == -1)
			break;
		buffer[bytes_received - 1] = '\0';
		printf("\nreply from server\n '%s'\n\n", buffer);
//		sleep(5);
	}
	rclose(rsockfd);
	return flag;
}


