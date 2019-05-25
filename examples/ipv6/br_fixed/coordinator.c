/*
 * Copyright GPL by Voravit Tanyingyong <voravit@kth.se>
 * Created : 2019-03-23
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/icmp6.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>
#include <net/if.h>
#include <inttypes.h>
//#include <linux/ipv6.h>

#define BUF_LEN 1024
#define FIXED_SINK "fd00::200:0:0:1"
#define SERVADDR "fd00::2"
#define VIRTUALROOT "fd00::10"
#define NUM_ACTIVATE 1

#define LOGFILE "./coordinator.log"
int file_fd;
char *server = SERVADDR;
int debug = 0, daemonized = 1;
char *cmd = NULL;
//static uint8_t key_metric = 0x01; // tree size
//static uint8_t key_metric = 0x02; // longest hop
static uint8_t key_metric = 0x04; // received traffic
//static uint8_t key_metric = 0x08; // highest traffic
static int act = 0;
//static int deact = 0;

/*---------------------------------------------------------------------------*/
typedef struct candidate_sink {
  struct candidate_sink *next;
  struct sockaddr_in6 node_addr; 
  int activated;
  int skip;
  //struct sockaddr_in6 tun_addr; 
  uint16_t rank;
  uint16_t num_neighbor;
  uint32_t tree_size;
  uint32_t longest_hop;
  uint32_t rx_traffic;
  uint32_t nbr_traffic;
  uint32_t power;
  uint32_t slip_in;
  uint32_t slip_out;
} candidate_sink_t;
candidate_sink_t *sink_table = NULL, *last_node = NULL;
#define MAX_ACT_LIST 4
candidate_sink_t *activate_list[MAX_ACT_LIST] = {NULL};
candidate_sink_t *deactivate_sink = NULL;
/*---------------------------------------------------------------------------*/
static int require_num = 0, curr_num = 0;
#define MAX_TREE_SIZE 20
#define MAX_LONGEST_HOP 7
#define MAX_RX_TRAFFIC 2000
#define MAX_NBR_TRAFFIC 1000
#define MAX_RANK 1280
#define THRESHOLD 0.5
/*---------------------------------------------------------------------------*/
void print_sink_list(candidate_sink_t *sink_table){
  char addr_str[INET6_ADDRSTRLEN];
  candidate_sink_t *current = sink_table;
//printf("SINK_LIST:\n");
  while (current != NULL) {
    printf("sink: %s ", inet_ntop(AF_INET6, &current->node_addr.sin6_addr, addr_str, INET6_ADDRSTRLEN));
    //printf("tun: %s ", inet_ntop(AF_INET6, &current->tun_addr.sin6_addr, addr_str, INET6_ADDRSTRLEN));
    printf("act: %d skip: %d ", current->activated, current->skip);
    printf("rank: %u num_neighbor: %u ", current->rank, current->num_neighbor);
    printf("size: %u hop: %u rx: %u nbr: %u ", current->tree_size, current->longest_hop, current->rx_traffic, current->nbr_traffic);
    printf("slip_in: %u slip_out: %u power: %u\n", current->slip_in, current->slip_out, current->power);

    current = current->next;
  }
}
/*---------------------------------------------------------------------------*/
candidate_sink_t *find_sink_list(candidate_sink_t *sink_table, struct sockaddr_in6 *addr){
  candidate_sink_t *current = sink_table;
  while (current != NULL) {
    if (memcmp(&current->node_addr.sin6_addr, &addr->sin6_addr, sizeof(addr->sin6_addr))==0) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
int sink_addition_algorithm(candidate_sink_t *sink_table){
  candidate_sink_t *current = sink_table;
  candidate_sink_t *hirank = NULL, *hinbr = NULL;
  int rx_over=0, rx_under=0;
  int rank_over=0;
  int skip=0;
  int sink_total=0, sink_activated=0, sink_fixed=0;
  uint32_t total_rx = 0;
  int available = 0;
  /* process through information */
  while (current != NULL) {
    if (current->activated>0) {
      total_rx += current->rx_traffic;
      if ((current->rx_traffic >= MAX_RX_TRAFFIC)&&(current->skip==0)) rx_over++;
      if (current->rx_traffic < MAX_RX_TRAFFIC*THRESHOLD) rx_under++;
      sink_activated++;
      if (current->activated>1) sink_fixed++;
    } else {
      if (current->rank >= MAX_RANK) rank_over++;
      activate_list[available] = current;
      available++;
    }
    //if (current->skip==1) current->skip = 0;
    sink_total++;
    current = current->next;
  } /* while (current != NULL) */

  /* fill unused entry of the activate_list with NULL */
  for (int i=available; i<MAX_ACT_LIST; i++) {
    activate_list[i] = NULL;
  }
    
  /* sort available sinks with the highest rank first */
    candidate_sink_t *temp = NULL;
    for (int i=0; i<available-1; i++) {
      for (int j=0; j < available-i-1; j++) {
        if (activate_list[j]->rank < activate_list[j+1]->rank)
        {
          temp = activate_list[j];
          activate_list[j] = activate_list[j+1];
          activate_list[j+1] = temp;
        }
      } /* sort j */
    } /* sort i */

/*
print_sink_list(sink_table);
  char tmp_addr_str[INET6_ADDRSTRLEN];
  for (int i=0; i<MAX_ACT_LIST; i++) {
    if (activate_list[i] != NULL) {
      printf("%d S:%s K:%d A:%d R:%u NB:%u\n", i+1, inet_ntop(AF_INET6, &activate_list[i]->node_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN), 
	activate_list[i]->skip, activate_list[i]->activated, activate_list[i]->rank, activate_list[i]->num_neighbor);
    } else {
      printf("%d: NULL\n", i+1);
    }
  }
  printf("rank_over:%d rx_over:%d rx_under:%d total:%d act:%d fix:%d avail:%d\n", rank_over, rx_over, rx_under, sink_total, sink_activated, sink_fixed, available);
*/    

  /* if we have an unactive sink, we activate it if needed */
  //if ((rank_over>0)&&(sink_total>sink_activated)) return rank_over;
  if (rank_over>0) return rank_over;
  if ((rx_over>0) && (rx_over==sink_activated) && (available>0)) return 1;

  /* If there are more than one sink underutilize, we deactivate one */
  if ((sink_activated>sink_fixed) && (rx_under>1)) {
    /* select one with the lowest traffic */
    current = sink_table;
    while (current != NULL) {
      if (current->activated==1) {
	if (deactivate_sink == NULL) { 
          deactivate_sink = current;
        } else if (current->rx_traffic < deactivate_sink->rx_traffic) {
          deactivate_sink = current;
        }
      } 
      current = current->next;
    }
    return -1;
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
candidate_sink_t *select_activate_sink(candidate_sink_t *sink_table, int metric, candidate_sink_t *last){
  candidate_sink_t *current = sink_table;
  if (last!=NULL) {
    current = last->next;
  } else {
    current = sink_table;
  }
  while (current != NULL) {
    if (current->activated==0) {
      //return &current->node_addr;
      return current;
    }
    current = current->next;
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
candidate_sink_t *select_deactivate_sink(candidate_sink_t *sink_table, candidate_sink_t *last) {
  candidate_sink_t *current = sink_table;
  if (last!=NULL) {
    current = last->next;
  } else {
    current = sink_table;
  }
  while (current != NULL) {
    if (current->activated==1) {
      //return &current->node_addr;
      return current; 
    } 
    current = current->next;
  }
  return NULL;
} 
/*---------------------------------------------------------------------------*/
int process(void)
{
  int sock, msg_len;
  struct sockaddr_in6 tmp_addr;
  char tmp_addr_str[INET6_ADDRSTRLEN];

  struct icmp6_filter filter;
  int on = 1;

  char buf[BUF_LEN];
  char reply[BUF_LEN];
  unsigned short csum;

  /* data structure for ancillary data */
  struct msghdr msg;
  struct iovec iov[1];
  struct icmp6_hdr *icmp6;
  struct cmsghdr *cmsg;
  char cmsg_data[1024];

  int cmsg_space;
  struct in6_pktinfo *pktinfo;

  struct sockaddr_storage src_addr;

  /* temporary ICMPv6 payload key data */
  uint8_t flags;
  struct sockaddr_in6 sink_addr;
  char sink_addr_str[INET6_ADDRSTRLEN];

  //candidate_sink_t *sink_ptr = NULL;
  static int ctr=0;
  static int skip=0;

  /* for activation/deactivation */
  struct timeval timeout = {5, 0}; 
  fd_set read_set;

  /* socket setup */
  sock = socket(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  if (sock < 0) {
      perror("socket failed");
      exit(1);
  }

  /* filter to get only RPL messages (ICMPV6 type 155) */
  ICMP6_FILTER_SETBLOCKALL(&filter);
  ICMP6_FILTER_SETPASS(155, &filter);

  if (setsockopt(sock, IPPROTO_ICMPV6, ICMP6_FILTER, &filter, sizeof (filter))<0) {
    close (sock);
    perror("getsockopt IPPROTO_ICMPV6 failed");
    exit(1);
  }
  if (setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on))<0) {
    close (sock);
    perror("getsockopt IPPROTO_IPV6 failed");
    exit(1);
  }

  /* process loop */
  while(1) {

    memset((char *) &buf, 0, sizeof(buf));
    memset((char *) &reply, 0, sizeof(buf));
    memset(&msg, '\0', sizeof(msg));
    iov[0].iov_base = buf;
    iov[0].iov_len = sizeof(buf);
    msg.msg_name = &src_addr;
    msg.msg_namelen = sizeof (src_addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_data;
    msg.msg_controllen = sizeof (cmsg_data);
    msg.msg_flags = 0;

    memset((char *) &buf, 0, sizeof(buf));
    fflush(stdout);

    msg_len = recvmsg(sock, &msg, 0);
    if (debug) printf("\nrecvmsg len: %d\n", msg_len);
    if (msg_len < 0) {
      perror("recvmsg failed");
      exit(1);
    }

    for (cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL; cmsg = CMSG_NXTHDR (&msg, cmsg)) {
      if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO) {
          pktinfo = ((struct in6_pktinfo*)CMSG_DATA(cmsg));
          break;
        }
    }

    memcpy(&tmp_addr, &src_addr, sizeof(src_addr));
    if (debug) {
      printf("SRC: %s ", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
      printf("DST: %s \nDATA: ", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
      int i;
      for(i=0;i<msg_len;i++) { printf("%02hhX ", buf[i]); } printf("\n");
    }

    /* process only packets that match the coordinator IPv6 address */
    if (strcmp(inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN), server)==0) {

      memcpy(&flags,&buf[4],sizeof(char));
      if ((flags>>7)==0) {
        if (debug) printf("Not DIS extension, ignore the packet\n");
        continue;
      } 

      switch ((flags &0x30)>>4) {
        case(0): /* register */
          if (debug) printf("flags: %02hhX register\n", flags);
          memcpy(&sink_addr.sin6_addr, &buf[10], sizeof(sink_addr.sin6_addr));
          sink_addr.sin6_family = AF_INET6;
          if (debug) printf("candidate sink: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));

printf("REGISTER: %s ", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
          if (find_sink_list(sink_table, &sink_addr)==NULL) {
printf("added");
            /* create new sink element */
	    uint16_t tmp;
            candidate_sink_t *node = NULL;
            node = malloc(sizeof(candidate_sink_t));
            node->next = NULL;
            memcpy(&node->node_addr, &sink_addr, sizeof(sink_addr));
            memcpy(&tmp, &buf[6], sizeof(uint16_t));
            node->rank = (uint16_t) ntohs(tmp);
            memcpy(&tmp, &buf[8], sizeof(uint16_t));
            node->num_neighbor = (uint16_t) ntohs(tmp);
printf(" rank: %u nbr: %u", node->rank, node->num_neighbor);
            node->skip = 1;
            //if (strcmp(inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN), FIXED_SINK)==0) {
            if (memcmp(&sink_addr.sin6_addr,&tmp_addr.sin6_addr, sizeof(sink_addr.sin6_addr))==0) {
              node->activated = 2; 
printf(" fixed-sink\n");
            } else {
printf("\n");
            }
            /* add new sink element to the end of sink_list */
            if (sink_table == NULL) {
              sink_table = node;
              last_node = node;
            } else {
              last_node->next = node;
              last_node = node;
            }
              if (debug) print_sink_list(sink_table);
            } else {
              /* sink is already in sink_list */
printf("exist\n");
              if (debug) printf("%s is already registered\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
            }

            /* prepare register ack packet */
            memcpy(reply, buf, 6);	/* copy received DIS message */
            reply[4] |= 0x40;		/* set acknowledgement flag */
            memset(&reply[2], 0, 2);	/* reset checksum to 0 */

            iov[0].iov_base = reply;
            iov[0].iov_len = 6; //sizeof(reply);
            msg.msg_name = &sink_addr; 
            msg.msg_namelen = sizeof(sink_addr); 
            msg.msg_iov = iov;
            msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
            msg.msg_control = cmsg_data;
            msg.msg_controllen = sizeof(cmsg_data);
            msg.msg_flags = 0;
            cmsg_space = 0;
            cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = IPPROTO_IPV6;
            cmsg->cmsg_type = IPV6_PKTINFO;
            cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
            *(struct in6_pktinfo*)CMSG_DATA(cmsg) = *pktinfo;
            cmsg_space += CMSG_SPACE(sizeof(struct in6_pktinfo));
            msg.msg_controllen = cmsg_space;

            if (debug) { 
              printf("REPLY:\nSRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN)); 
              printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
              printf("DATA: ");
              int i;
              for(i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
            }

            msg_len = sendmsg(sock, &msg, 0);
            if (msg_len < 0) {
              perror("sendmsg failed");
            }
            if (debug) printf("SEND LEN: %d\n", msg_len);
          break;

        case(2): /* activate */
          if (debug) printf("flags: %02hhX activate\n", flags);
          /* check activation ack */
          if (flags & 0x40) {
            printf("RECEIVE ACTIVATION ACK %s\n", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));

            candidate_sink_t *node = NULL;
            node = find_sink_list(sink_table, &tmp_addr);
            if (node != NULL) {
              if (node->activated == 0) {
                node->activated = 1;
                act++;
                if (require_num <= act) {
                  require_num = 0;
                  act = 0;
                  activate_list[0] = NULL;
                  activate_list[1] = NULL;
                  activate_list[2] = NULL;
                  activate_list[3] = NULL;
                }
              } else {
                printf("Already activated\n");
              }
            } else {
              /* do nothing here */
              printf("node does not exist in our sink table\n");
            }

            if (require_num > 0) goto activate_now;
            break;
          } /* if (flags & 0x40) */

          /* receive activation message */
          /* this should not happen! do nothing */
          break;

        case(3): /* deactivate */
          if (debug) printf("flags: %02hhX deactivate\n", flags);
          /* check deactivation ack */
          if (flags & 0x40) {
            //printf("RECEIVE DEACTIVATION ACK\n");
            printf("RECEIVE DEACTIVATION ACK %s\n", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
          
            candidate_sink_t *node = NULL;
            node = find_sink_list(sink_table, &tmp_addr);
            if (node != NULL) {
              if (node->activated > 0) {
                node->activated = 0;
                node->rank = 0;
                //if (require_num < 0) { require_num++; deactivate_sink = NULL; }
		require_num = 0;
                deactivate_sink = NULL;
              } else {
                printf("Already deactivated\n");
              } 
            } else {
              /* do nothing here */
              printf("node does not exist in our sink table\n");
            }

            if (require_num < 0) goto deactivate_now;
            break;

          } /* if (flags & 0x40) */

          /* receive deactivation message */
          /* this should not happen! do nothing */
          break;

        case(1): /* report */
          if (debug) printf("flags: %02hhX report\n", flags);

printf("REPORT: %s ", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
          /* update metric on sink_list */
          candidate_sink_t *node = NULL;
          uint8_t metric_flags;
          memcpy(&metric_flags, &buf[6], sizeof(metric_flags));

	  /* special case: the active sink report for candidate sink */
	  if ((metric_flags&0x08)==0x08) {
	    memcpy(&tmp_addr.sin6_addr, &buf[11], sizeof(tmp_addr.sin6_addr));
            //memcpy(&tmp_addr, &buf[11], sizeof(tmp_addr));
printf("FOR SINK: %s ", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
          }

          node = find_sink_list(sink_table, &tmp_addr);
          if (node!=NULL) {
            int index = 7; /* start of payload */
            uint8_t f = 0x01;
            uint32_t tmp_value;
            uint16_t tmp;
            int periodic_print = 0;
printf("M:%02hhX\n", metric_flags);
            while ((f!=0x00)&&(metric_flags!=0xff)) {
//if (debug) printf("metric_flags: %02hhX f: %02hhX\n", metric_flags, f);
	      switch (metric_flags&f) {
		case(0x01):
		  memcpy(&node->tree_size, &buf[index], 1);
		  index++;
		  memcpy(&node->longest_hop, &buf[index], 1);
		  index++;
		  break;
		case(0x02):
		  memcpy(&tmp_value, &buf[index], 4);
		  node->rx_traffic = ntohl(tmp_value);
		  index+=4;
		  memcpy(&tmp_value, &buf[index], 4);
		  node->nbr_traffic = ntohl(tmp_value);
		  index+=4;
		  memcpy(&tmp_value, &buf[index], 4);
		  node->slip_in = ntohl(tmp_value);
		  index+=4;
		  memcpy(&tmp_value, &buf[index], 4);
		  node->slip_out = ntohl(tmp_value);
		  index+=4;
		  break;
		case(0x04):
		  memcpy(&tmp_value, &buf[index], 4);
		  node->power = ntohl(tmp_value);
		  index+=4;
                  ctr++; 
                  if (ctr%5==0) periodic_print=1;
		  break;
		case(0x08):
		  memcpy(&node->rank, &buf[index], 2);
		  index+=2;
		  memcpy(&node->num_neighbor, &buf[index], 2);
		  index+=2;
		  break;
		default:
		  break;
	      } /* switch (metric_flags&f) */
	      f = f<<1;
            } /* while ((f!=0x00)&&(metric_flags!=0xff)) */

            if (periodic_print) {
              printf("CTR:%d\n", ctr);
              print_sink_list(sink_table);
              periodic_print = 0;
            }

            if (((metric_flags&0x02)==0x02) && (node->skip)) {
              /* first traffic report tends to be too high over our threshold
               * so we don't run algorithm on this to avoid unnecessary sink activation */
              printf("We don't run the algorithm on the first traffic report\n");
              node->skip=0;
	      skip = 1;
              break;
            }
          } else {
            printf("ignore report: no match candidate sink\n");
            break;
          }

          /* run algorithm to find sinks to activate/deactivate 
           * we may activate more than 1 sink but deactivate only one sink at a time 
           * algorithm return 0: no action, N: activate N sink, and -1: deactivate 1 sink
           */
          if (skip) {
	    skip = 0;
	  } else {
          if ((require_num == 0) && (act == 0)) {
            require_num = sink_addition_algorithm(sink_table);
            curr_num = 0;
printf("require_num: %d\n", require_num);
          }
	  }

if (debug) printf("require number of sink: %d\n", require_num);

          if (require_num > 0) {
activate_now:
            while (require_num > curr_num) {
              //struct timeval timeout = {5, 0};
              //fd_set read_set;

	      if (activate_list[curr_num] != NULL) {
                memcpy(&sink_addr,&activate_list[curr_num]->node_addr, sizeof(sink_addr));
		curr_num++;

                reply[0]=0x9b;
                reply[1]=0x00;
                reply[2]=0x00;
                reply[3]=0x00;
                reply[4]=0xA0;
                reply[5]=0x00;

                iov[0].iov_base = reply;
                iov[0].iov_len = 6; //sizeof(reply);
                /* set dst address to the candidate sink on msg.msg_name */
                msg.msg_name = &sink_addr; 
                msg.msg_namelen = sizeof(sink_addr); 
                msg.msg_iov = iov;
                msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
                msg.msg_control = cmsg_data;
                msg.msg_controllen = sizeof(cmsg_data);
                msg.msg_flags = 0;
                cmsg_space = 0;
                cmsg = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_level = IPPROTO_IPV6;
                cmsg->cmsg_type = IPV6_PKTINFO;
                cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
                *(struct in6_pktinfo*)CMSG_DATA(cmsg) = *pktinfo;
                cmsg_space += CMSG_SPACE(sizeof(struct in6_pktinfo));
                msg.msg_controllen = cmsg_space;

printf("SEND ACTIVATION: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                //if (debug) printf("ACTIVATION REMAIN: %d\n", require_num);
                if (debug) {
                  printf("SRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("MSG LEN: %d DATA: ", msg_len);
                  int i;
                  for(i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
                }

                msg_len = sendmsg(sock, &msg, 0);
                if (msg_len < 0) {
                  perror("sendmsg failed");
                }
                if (debug) printf("send len: %d\n", msg_len);

                memset(&read_set, 0, sizeof read_set);
                FD_SET(sock, &read_set);
                msg_len = select(sock + 1, &read_set, NULL, NULL, &timeout);
                if (msg_len == 0) {
                  printf("No reply, fail to activate %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                  // mark sink unreachable
                } else if (msg_len < 0) {
                    perror("Select");
                } else {
                    /* we get response from activated sink */
                    if (debug) printf("RECEIVE ACTIVATION ACK\n");
                    //require_num--;
                }
              } else { /* if (activate_list[curr_num] != NULL) */
                if (debug) printf("not enough candidate sink available\n");
                break;
              }
            } /* while (require_num) */
	    curr_num = 0;
            break;
          } else if (require_num < 0) {
deactivate_now:
              //struct timeval timeout = {5, 0}; 
              //fd_set read_set;

              if (deactivate_sink!=NULL) {
                memcpy(&sink_addr,&deactivate_sink->node_addr, sizeof(sink_addr));

                reply[0]=0x9b;
                reply[1]=0x00;
                reply[2]=0x00;
                reply[3]=0x00;
                reply[4]=0xB0;
                reply[5]=0x00;

                iov[0].iov_base = reply;
                iov[0].iov_len = 6; //sizeof(reply);
                /* set dst address to the candidate sink on msg.msg_name */
                msg.msg_name = &sink_addr; 
                msg.msg_namelen = sizeof(sink_addr); 
                msg.msg_iov = iov;
                msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
                msg.msg_control = cmsg_data;
                msg.msg_controllen = sizeof(cmsg_data);
                msg.msg_flags = 0;
                cmsg_space = 0;
                cmsg = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_level = IPPROTO_IPV6;
                cmsg->cmsg_type = IPV6_PKTINFO;
                cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
                *(struct in6_pktinfo*)CMSG_DATA(cmsg) = *pktinfo;
                cmsg_space += CMSG_SPACE(sizeof(struct in6_pktinfo));
                msg.msg_controllen = cmsg_space;

printf("SEND DEACTIVATION: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                // if (debug) printf("ACTIVATION REMAIN: %d\n", require_num);
                if (debug) {
                  printf("SRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("MSG LEN: %d DATA: ", msg_len);
                  int i;
                  for(i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
                }

                msg_len = sendmsg(sock, &msg, 0); 
                if (msg_len < 0) {
                  perror("sendmsg failed");
                }
                if (debug) printf("send len: %d\n", msg_len);

                memset(&read_set, 0, sizeof read_set);
                FD_SET(sock, &read_set);
                msg_len = select(sock + 1, &read_set, NULL, NULL, &timeout);
                if (msg_len == 0) {
                  printf("No reply, fail to activate %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                  // mark sink unreachable
                } else if (msg_len < 0) {
                    perror("Select");
                } else {
                    /* we get response from deactivated sink */
                    if (debug) printf("RECEIVE DEACTIVATION ACK\n");
                }
              } else { /* if (sink_ptr!=NULL) */
                if (debug) printf("already check through sink list\n");
                break;
              } 
            //} /* while (require_num<0) */
            break;
          }
          break;

        default:
          printf("unknown flags: %02hhx\n", flags);

      } /* switch */
    } else {
    /* packet destination is not coordinator IPv6 address */
      if (debug) printf("packet destination is not for the coordinator\n");
    }

  } /* end while(1) */
} /* end process */
/*---------------------------------------------------------------------------*/
void usage(void)
{
  printf("\nRPL virtual root coordinator\n");
  printf("\ncoordinator [-d] [-f file] [-c command ipv6] [-s ipv6]\n");
  printf(" -d               -- debug mode\n");
  printf(" -f filename      -- local logfile. Default is %s\n", LOGFILE);
  printf(" -m metric        -- 1.tree_size 2.longest_hop 3.rx_traffic 4.nbr_traffic\n");
  printf(" -c command ipv6  -- send a command to a server ipv6 address\n");
  printf(" -s ipv6          -- specify server IPv6 address\n");
//  printf("IMPORTANT:\n");
//  printf("\nBy default, the kernel does not allow you to create ICMP socket\n");
//  printf("\nThus, you must enable it with the command:\n");
//  printf("\n  sysctl -w net.ipv4.ping_group_range=\"0 0\"\n");
//  printf("\nThis will allow root (uid 0) to create ICMP socket\n");
  exit(-1);
}
/*---------------------------------------------------------------------------*/
int main(int argc,char **argv)
{
  int i;
  char *filename = LOGFILE;
  for(i = 1; (i < argc) && (argv[i][0] == '-'); i++)  {
    if (strncmp(argv[i], "-h", 2) == 0)
      usage();
    else if (strncmp(argv[i], "-d", 2) == 0)
      debug = 1;
    else if (strncmp(argv[i], "-f", 2) == 0)
      filename = argv[++i];
    else if (strncmp(argv[i], "-m", 2) == 0)
      key_metric = (uint8_t)atoi(argv[++i]);
    else if (strncmp(argv[i], "-s", 2) == 0)
      server = argv[++i];
    else if (strncmp(argv[i], "-c", 2) == 0) {
      cmd = argv[++i];
      server = argv[++i];
      daemonized = 0;
    }
  }

  if(filename) {
    file_fd = open(filename, O_CREAT|O_RDWR|O_APPEND, 0644);
    if(file_fd < 0) {
      fprintf(stderr, "Failed to open '%s'\n", filename);
      exit(2);
    }
  }

  if(daemonized) {
    if (debug) printf("Start as a server\n");
    process();
  } else {
    if (debug) printf("Start as a client\n");
  }
  return 0;
}
/*---------------------------------------------------------------------------*/



