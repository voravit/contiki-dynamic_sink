#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/uio.h>

#define PORT 5688
#define BUF_LEN 1024
#define SERVADDR "fd00::1"

int main()
{
  int sock, client, pid, recv_len;
  struct sockaddr_in6 s_addr, c_addr;
  socklen_t c_len = sizeof(c_addr);
  char buf[BUF_LEN];
  char c_ip_str[INET6_ADDRSTRLEN];
  const char space = ' ';
  char ack[5];
  char hop[5];
  int ttl = 1;
  socklen_t ttl_len = sizeof(ttl);

struct sockaddr_storage src_addr;

struct msghdr message;
struct iovec iov[1];
//memset(iov, 0, sizeof(iov));
memset(&message, '\0', sizeof(message));
message.msg_iov=iov;
message.msg_iovlen=1;
iov[0].iov_base=buf;
iov[0].iov_len=sizeof(buf);
//iov[0].iov_base=buffer;
//iov[0].iov_len=sizeof(buffer);
message.msg_name = &src_addr;
message.msg_namelen = sizeof(src_addr);

int *ttlptr=NULL;
int received_ttl = 0;

int cmsg_size = sizeof(struct cmsghdr)+sizeof(received_ttl); // NOTE: Size of header + size of data
char buf2[CMSG_SPACE(sizeof(received_ttl))];
message.msg_control = buf2; // Assign buffer space for control header + header data/value
message.msg_controllen = sizeof(buf2); //just initializing it

  sock = socket(PF_INET6, SOCK_DGRAM, 0);

  memset(&s_addr, 0, sizeof(struct sockaddr_in6));
  s_addr.sin6_family = AF_INET6;
//  s_addr.sin6_addr = in6addr_any;
  inet_pton(AF_INET6, SERVADDR, &s_addr.sin6_addr);
  s_addr.sin6_port = htons(PORT);

  if (bind(sock, (struct sockaddr *)&s_addr, sizeof(s_addr))<0) {
    perror("bind failed");
    exit(1);
  }
  printf("socket ready\n");

  if (setsockopt(sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &ttl, sizeof(ttl))<0) {
    perror("getsockopt failed");
    exit(1);
  }


  while(1) {
    memset((char *) &buf, 0, sizeof(buf));
    memset((char *) &ack, 0, sizeof(ack));
    memset((char *) &hop, 0, sizeof(hop));

    fflush(stdout);

  ssize_t count=recvmsg(sock,&message,0);
  if (count==-1) {
    perror("recvmsg failed");
    exit(1);
  } else if (message.msg_flags&MSG_TRUNC) {
    perror("datagram too large for buffer: truncated");
  } else {
    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL; cmsg = CMSG_NXTHDR(&message,cmsg)) {
          //if ((cmsg->cmsg_level == IPPROTO_IP) && (cmsg->cmsg_type == IP_TTL) && (cmsg->cmsg_len)){
          if ((cmsg->cmsg_level == IPPROTO_IPV6) && (cmsg->cmsg_type == IPV6_HOPLIMIT)){
                ttlptr = (int *) CMSG_DATA(cmsg);
                received_ttl = *ttlptr;
                //printf("received_ttl = %p and %d \n", ttlptr, received_ttl); 
               break;
           }
    }

    if (src_addr.ss_family == AF_INET6) {
	struct sockaddr_in6 *a = (struct sockaddr_in6 *) &src_addr;
	memcpy(&c_addr, a, sizeof(c_addr));
    } else {
	printf("NOT AF_INET6\n");
    }

    printf("RCV %s HOP %d DATA %s\n", inet_ntop(AF_INET6, &c_addr.sin6_addr, c_ip_str, INET6_ADDRSTRLEN), received_ttl, buf);

    strncpy(ack, buf, (int)(strchr(buf,space) - buf));
    strncpy(hop, strrchr(buf,space), strlen(buf)-(int)(strrchr(buf,space) - buf));

    sendto(sock, ack, strlen(ack), 0, (struct sockaddr *)&c_addr, sizeof(c_addr));
    printf("RPY %s SEQ %s\n", inet_ntop(AF_INET6, &c_addr.sin6_addr, c_ip_str, INET6_ADDRSTRLEN), ack);

  }

  } /* end while(1) */
} /* end main */

