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
//static int act = 0;

/*---------------------------------------------------------------------------*/
#define ARRAY_LEN 5
#define RX_TH_ACT 50
#define RX_TH_DEACT 15
#define NBR_TH_ACT 50
#define NBR_TH_DEACT 15
#define THRESHOLD_HIT 3
#define HOLDDOWN 1
#define RX_METRIC 1
typedef struct candidate_sink {
  struct candidate_sink *next;
  struct sockaddr_in6 node_addr; 
  int activated;
  struct sockaddr_in6 tun_addr; 
  uint16_t rank;
  uint16_t num_neighbor;
  uint32_t tree_size;
  uint32_t longest_hop;
  uint32_t energy;
  uint32_t slip_in;
  uint32_t slip_out;

  int last_arr_index;
  uint16_t last_rx;
  uint16_t arr_rx[ARRAY_LEN];
  uint16_t arr_nbr[ARRAY_LEN];
  int rx_over;
  int rx_over2;
  int rx_over3;
  int rx_under;
  int nbr_over;
  int nbr_over2;
  int nbr_over3;
  int nbr_under;
  int holddown;

} candidate_sink_t;
candidate_sink_t *sink_table = NULL, *last_node = NULL;
#define MAX_ACT_LIST 8
candidate_sink_t *activate_list[MAX_ACT_LIST] = {NULL};
candidate_sink_t *deactivate_sink = NULL;
candidate_sink_t *trigger_node = NULL;
/*---------------------------------------------------------------------------*/
//static int require_num = 0, curr_num = 0;
static int require_num = 0, curr_num = 0, ack_received = 0;
#define MAX_TREE_SIZE 20
#define MAX_LONGEST_HOP 7
#define MAX_RX_TRAFFIC 2000
#define MAX_NBR_TRAFFIC 1000
#define MAX_RANK 1280
//#define THRESHOLD 0.5
/*---------------------------------------------------------------------------*/
void print_sink_list(candidate_sink_t *sink_table){
  char addr_str[INET6_ADDRSTRLEN];
  candidate_sink_t *current = sink_table;
printf("SINK_LIST:\n");
  while (current != NULL) {
    printf("CS: %s ", inet_ntop(AF_INET6, &current->node_addr.sin6_addr, addr_str, INET6_ADDRSTRLEN));
    printf("P: %s ", inet_ntop(AF_INET6, &current->tun_addr.sin6_addr, addr_str, INET6_ADDRSTRLEN));
    printf("A: %d ", current->activated);
    printf("R: %u N: %u S: %u H: %u ", current->rank, current->num_neighbor, current->tree_size, current->longest_hop);
    printf("SI: %u SO: %u E: %u ", current->slip_in, current->slip_out, current->energy);
    printf("I: %d HD: %d\n", current->last_arr_index, current->holddown);
    printf("ARX: %u %u %u %u %u ", current->arr_rx[0], current->arr_rx[1], current->arr_rx[2], current->arr_rx[3], current->arr_rx[4]);
    printf("ANB: %u %u %u %u %u\n", current->arr_nbr[0], current->arr_nbr[1], current->arr_nbr[2], current->arr_nbr[3], current->arr_nbr[4]);

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
int sink_selection_algorithm(candidate_sink_t *node){
  candidate_sink_t *current = sink_table;
  candidate_sink_t *hirank = NULL, *hinbr = NULL;
  int rx_over=0, rx_under=0;
  int rx_over2=0, rx_over3=0;
  int nbr_over=0, nbr_under=0;
  int nbr_over2=0, nbr_over3=0;
  int rank_over=0;
  int sink_total=0, sink_activated=0, sink_fixed=0;
  //uint32_t total_rx = 0, total_nbr = 0;
  int available = 0, unavailable = 0;

  /* update node */
  node->rx_over = 0;
  node->rx_over2 = 0;
  node->rx_over3 = 0;
  node->rx_under = 0;
  node->nbr_over = 0;
  node->nbr_over2 = 0;
  node->nbr_over3 = 0;
  node->nbr_under = 0;
  for (int i=1; i<=THRESHOLD_HIT; i++) {
    if (node->arr_rx[ARRAY_LEN-i] >= RX_TH_ACT) { node->rx_over++; }; 
    if (node->arr_rx[ARRAY_LEN-i] >= RX_TH_ACT*2) { node->rx_over2++; }; 
    if (node->arr_rx[ARRAY_LEN-i] >= RX_TH_ACT*3) { node->rx_over3++; }; 
    if (node->arr_rx[ARRAY_LEN-i] <= RX_TH_DEACT) { node->rx_under++; }; 
    if (node->arr_nbr[ARRAY_LEN-i] >= NBR_TH_ACT) { node->nbr_over++; }; 
    if (node->arr_nbr[ARRAY_LEN-i] >= NBR_TH_ACT*2) { node->nbr_over2++; }; 
    if (node->arr_nbr[ARRAY_LEN-i] >= NBR_TH_ACT*3) { node->nbr_over3++; }; 
    if (node->arr_nbr[ARRAY_LEN-i] <= NBR_TH_DEACT) { node->nbr_under++; }; 
  }

  /* Don't run algorithm if node is under holddown period */
  if (node->holddown > 0) { 
    node->holddown--;
    /* decrement holddown of dynamic sink that is in sensor mode */
    while (current != NULL) {
      if ((current != node)&&(current->activated==0)&&(current->holddown>0)) {
        current->holddown--;
      }
      current = current->next;
    }
    return 0;
  }

  trigger_node = NULL;

  /* run algorithm only if node is not under holddown period
   * We check all non-holddown sinks if their threshold are exceeded.
   */
  while (current != NULL) {
    if (current->activated>0) {
      sink_activated++;
      if (current->activated>1) {
        sink_fixed++;
      }
        if ((current->rx_over >= THRESHOLD_HIT)&&(current->holddown==0)) rx_over++;
        if ((current->rx_over2 >= THRESHOLD_HIT)&&(current->holddown==0)) rx_over2++;
        if ((current->rx_over3 >= THRESHOLD_HIT)&&(current->holddown==0)) rx_over3++;
        if ((current->rx_under >= THRESHOLD_HIT)&&(current->holddown==0)) rx_under++;
        if ((current->nbr_over >= THRESHOLD_HIT)&&(current->holddown==0)) nbr_over++;
        if ((current->nbr_over2 >= THRESHOLD_HIT)&&(current->holddown==0)) nbr_over2++;
        if ((current->nbr_over3 >= THRESHOLD_HIT)&&(current->holddown==0)) nbr_over3++;
        if ((current->nbr_under >= THRESHOLD_HIT)&&(current->holddown==0)) nbr_under++;
    } else {  // current->activated==0
      if (current->holddown==0) {
        activate_list[available] = current;
        available++;
      } else {
	unavailable++;
	current->holddown--;  // decrease holddown of unactivated node
      }
    }
    sink_total++;
    current = current->next;
  } /* while (current != NULL) */

  /* fill unused entry of the activate_list with NULL */
  for (int i=available; i<MAX_ACT_LIST; i++) {
    activate_list[i] = NULL;
  }
    
  /* sort available sinks with the highest rank first */
/*
    candidate_sink_t *temp = NULL;
    for (int i=0; i<available-1; i++) {
      for (int j=0; j < available-i-1; j++) {
        if (activate_list[j]->rank < activate_list[j+1]->rank)
        {
          temp = activate_list[j];
          activate_list[j] = activate_list[j+1];
          activate_list[j+1] = temp;
        }
      } // sort j
    } // sort i 
*/
print_sink_list(sink_table);
  char tmp_addr_str[INET6_ADDRSTRLEN];
  for (int i=0; i<MAX_ACT_LIST; i++) {
    if (activate_list[i] != NULL) {
      printf("%d S:%s A:%d R:%u NB:%u ", i+1, inet_ntop(AF_INET6, &activate_list[i]->node_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN), 
	activate_list[i]->activated, activate_list[i]->rank, activate_list[i]->num_neighbor);
        printf("ARX: %u %u %u %u %u ", activate_list[i]->arr_rx[0], activate_list[i]->arr_rx[1], activate_list[i]->arr_rx[2], activate_list[i]->arr_rx[3], activate_list[i]->arr_rx[4]);
        printf("ANB: %u %u %u %u %u\n", activate_list[i]->arr_nbr[0], activate_list[i]->arr_nbr[1], activate_list[i]->arr_nbr[2], activate_list[i]->arr_nbr[3], activate_list[i]->arr_nbr[4]);
    } else {
      printf("%d: NULL\n", i+1);
    }
  }
  printf("ranko:%d rxo:%d 2:%d 3:%d rxu:%d nbro:%d 2:%d 3:%d nbru:%d total:%d act:%d fix:%d avail:%d\n", rank_over, rx_over, rx_over2, rx_over3, rx_under, nbr_over, nbr_over2, nbr_over3, nbr_under, sink_total, sink_activated, sink_fixed, available);
//fflush(stdout);
  /* if we have an unactive sink, we activate it if needed */
  if (RX_METRIC) {
    if (node->rx_over>0) {
      if (available>0) {
        //node->holddown = HOLDDOWN;
	trigger_node = node;
	return 1;
      } else {
	printf("no available sink to activate\n");
        return 0;
      } 
    } 
  } else {
    if (node->nbr_over>0) {
      if (available>0) {
        //node->holddown = HOLDDOWN;
	trigger_node = node;
	return 1;
      } else {
	printf("no available sink to activate\n");
        return 0;
      } 
    } 
  }

  /* if an active sink is underutilized, we deactivate one dynamic sink that is active */
  //if (RX_METRIC) {
  if (1) {
    if ((node->rx_under>0) && (node->activated>0) && (sink_activated>sink_fixed)) {
      deactivate_sink = NULL;
      if (node->activated==1) {
      /* If this is a dynamic sink that is underutilized, we deactivate it */
        deactivate_sink = node;
      } else {
      /* if static sink is underutilized, we select to deactivate an activate dynamic sink with the lowest traffic */
        current = sink_table;
        while (current != NULL) {
          if ((current->activated==1)&&(current->holddown==0)) {
            if (deactivate_sink == NULL) {
              deactivate_sink = current;
            } else if (current->arr_rx[current->last_arr_index-1] < deactivate_sink->arr_rx[deactivate_sink->last_arr_index-1]) {
              deactivate_sink = current;
            }
          }
          current = current->next;
        }
      }
      if (deactivate_sink != NULL) {
        //node->holddown = HOLDDOWN; 
	trigger_node = node;
        return -1;
      }
    }
/*
  } else {
    if ((node->nbr_under>0) && (node->activated>0) && (sink_activated>sink_fixed)) {
      deactivate_sink = NULL;
      if (node->activated==1) {
      // If this is a dynamic sink that is underutilized, we deactivate it 
        deactivate_sink = node;
      } else {
      // if static sink is underutilized, we select to deactivate an activate dynamic sink with the lowest traffic 
        current = sink_table;
        while (current != NULL) {
          if ((current->activated==1)&&(current->holddown==0)) {
            if (deactivate_sink == NULL) {
              deactivate_sink = current;
            } else if (current->arr_nbr[current->last_arr_index-1] < deactivate_sink->arr_nbr[deactivate_sink->last_arr_index-1]) {
              deactivate_sink = current;
            }
          }
          current = current->next;
        }
      }
      if (deactivate_sink != NULL) {
        //node->holddown = HOLDDOWN; 
	trigger_node = node;
        return -1;
      } 
    }
*/
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
  int sock = 0, msg_len = 0;
  int recv_len = 0, send_len = 0, select_len = 0;
  //ssize_t recv_len, send_len, select_len;
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

  /* for activation/deactivation */
  //struct timeval timeout = {5, 0}; 
  struct timespec timeout = {5, 0}; 
  fd_set read_fd_set;
  //sigset_t emptyset;

  /* socket setup */
  sock = socket(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  if (sock < 0) {
      perror("socket failed");
      exit(1);
  }
  if (debug) printf("sock: %d\n", sock);

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
    time_t tick;
    if (debug) { 
      printf("\nwhile 1: SOCK: %d ", sock);
      tick=time(NULL);
      printf("%s\n", ctime(&tick));
    }

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
    fflush(stdout);

	  recv_len = recvmsg(sock, &msg, 0);
	  //if (debug) printf("recvmsg len: %zd\n", recv_len);
	  if (debug) printf("recvmsg len: %d\n", recv_len);
	  if (recv_len < 0) {
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
	    printf("RECV SRC: %s ", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
	    printf("DST: %s \nDATA: ", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
	    for(int i=0;i<recv_len;i++) { printf("%02hhX ", buf[i]); } printf("\n");
	  }

	  /* process only packets that match the coordinator IPv6 address */
	  if (strcmp(inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN), server)==0) {
	    memcpy(&flags,&buf[4],sizeof(char));
	    if ((flags>>7)==0) {
	      if (debug) printf("Not DIS extension, ignore the packet\n");
	      continue;
	    }

	    candidate_sink_t *node = NULL;
	    switch ((flags &0x30)>>4) {
	      case(0): /* register */

                if (debug) printf("flags: %02hhX register\n", flags);
                memcpy(&sink_addr.sin6_addr, &buf[10], sizeof(sink_addr.sin6_addr));
                sink_addr.sin6_family = AF_INET6;
                if (debug) printf("candidate sink: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                printf("REGISTER: %s ", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                node = find_sink_list(sink_table, &sink_addr);
                if (node == NULL) {
                  printf("added");
                  /* create new sink element */
                  uint16_t tmp;
                  node = malloc(sizeof(candidate_sink_t));
                  node->next = NULL;
                  memcpy(&node->tun_addr, &tmp_addr, sizeof(tmp_addr));
                  memcpy(&node->node_addr, &sink_addr, sizeof(sink_addr));
                  memcpy(&tmp, &buf[6], sizeof(uint16_t));
                  node->rank = (uint16_t) ntohs(tmp);
                  memcpy(&tmp, &buf[8], sizeof(uint16_t));
                  node->num_neighbor = (uint16_t) ntohs(tmp);
                  printf(" rank: %u nbr: %u", node->rank, node->num_neighbor);
                  if (strcmp(inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN), FIXED_SINK)==0) {
                    node->activated = 2;
                    printf(" fixed-sink\n");
                  } else {
                    node->activated = 0;
                    printf("\n");
                  }
		  node->tree_size = 0;
		  node->longest_hop = 0;
		  node->energy = 0;
		  node->slip_in = 0;
		  node->slip_out = 0;
		  node->last_arr_index = 0;
		  node->last_rx = 0;
                  for (int i=0; i<ARRAY_LEN; i++) {
                    node->arr_rx[i] = 0;
                    node->arr_nbr[i] = 0;
                  }
		  node->rx_over = 0;
		  node->rx_over2 = 0;
		  node->rx_over3 = 0;
		  node->rx_under = 0;
		  node->nbr_over = 0;
		  node->nbr_over2 = 0;
		  node->nbr_over3 = 0;
		  node->nbr_under = 0;
		  node->holddown = 0;
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
                  printf("updated");
                  uint16_t tmp;
                  memcpy(&node->tun_addr, &tmp_addr, sizeof(tmp_addr));
                  memcpy(&tmp, &buf[6], sizeof(uint16_t));
                  node->rank = (uint16_t) ntohs(tmp);
                  memcpy(&tmp, &buf[8], sizeof(uint16_t));
                  node->num_neighbor = (uint16_t) ntohs(tmp);
                  printf(" rank: %u nbr: %u\n", node->rank, node->num_neighbor);
                }
      
                /* prepare register ack packet */
                memcpy(reply, buf, 6);      /* copy received DIS message */
                reply[4] |= 0x40;           /* set acknowledgement flag */
                memset(&reply[2], 0, 2);    /* reset checksum to 0 */
      
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
                  printf("REGISTER REPLY:\nSRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  printf("DATA: ");
                  //int i;
                  for(int i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
                }
      
                send_len = sendmsg(sock, &msg, 0);
                if (send_len < 0) {
                  perror("REGISTER sendmsg failed");
                }
                //if (debug) printf("REGISTER SEND LEN: %zd\n", send_len);
                if (debug) printf("REGISTER SEND LEN: %d\n", send_len);

		break;
	      case(1): /* report */

                if (debug) printf("flags: %02hhX report\n", flags);
      
                printf("REPORT: %s ", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                /* update metric on sink_list */
                node = find_sink_list(sink_table, &tmp_addr);
                if (node!=NULL) {
                  int index = 6; /* start of payload */
                  uint8_t f = 0x01;
                  uint16_t tmp16;
                  uint32_t tmp32;
                  uint16_t tmp_rx;
                  uint16_t tmp_nbr;
      
                  /* copy data from the received report */
                  memcpy(&node->tree_size, &buf[index], 1);
                  index++;
                  memcpy(&node->longest_hop, &buf[index], 1);
                  index++;
                  memcpy(&tmp16, &buf[index], 2);
                  tmp_rx = ntohs(tmp16);
                  index+=2;
                  memcpy(&tmp16, &buf[index], 2);
                  tmp_nbr = ntohs(tmp16);
                  index+=2;
                  memcpy(&tmp32, &buf[index], 4);
                  node->energy = ntohs(tmp32);
                  index+=4;
                  memcpy(&tmp32, &buf[index], 4);
                  node->slip_in = ntohs(tmp32);
                  index+=4;
                  memcpy(&tmp32, &buf[index], 4);
                  node->slip_out = ntohs(tmp32);
                  index+=4;
                  printf("tree: %u hop: %u rx: %u nbr: %u enr: %u sin: %u sout: %u\n", node->tree_size, node->longest_hop, tmp_rx, tmp_nbr, node->energy, node->slip_in, node->slip_out);
      
                  /* update node's data array */
                  if (node->last_arr_index < ARRAY_LEN) {
                    //if (debug) printf("node->last_arr_index < ARRAY_LEN\n");
                    node->arr_rx[node->last_arr_index] = (tmp_rx - node->last_rx);
                    node->arr_nbr[node->last_arr_index] = tmp_nbr;
                    node->last_arr_index++;
                  } else {
                    //if (debug) printf("node->last_arr_index >= ARRAY_LEN\n");
                    for (int i=1; i<ARRAY_LEN; i++) {
                      node->arr_rx[i-1] = node->arr_rx[i];
                      node->arr_nbr[i-1] = node->arr_nbr[i];
                    }
                    node->arr_rx[ARRAY_LEN-1] = (tmp_rx - node->last_rx);
                    node->arr_nbr[ARRAY_LEN-1] = tmp_nbr;
                  }
                  node->last_rx = tmp_rx;

                  /* run algorithm to find sinks to activate/deactivate if node is not holddown
                   * We collect samples first and run the algorithm only after we fill the array.
                   * We may activate more than 1 sink but deactivate only one sink at a time.
                   * algorithm return 0: no action, N: activate N sink, and -1: deactivate 1 sink
                   */
                  if (node->last_arr_index >= ARRAY_LEN) {
                    require_num = sink_selection_algorithm(node);
                    curr_num = 0;
                    ack_received = 0;
                    if (debug) printf("require_num: %d\n", require_num);
                  } else {
                    require_num = 0;
                    curr_num = 0;
                    ack_received = 0;
                    if (debug) print_sink_list(sink_table);
                    if (debug) printf("SKIP: last_arr_index: %d\n", node->last_arr_index);
                    //break;
                  }
                } else { /* if (node!=NULL) */
                  printf("ignore report: no match candidate sink\n");
                  break;
                } /* end if (node!=NULL) */

                if (require_num > 0) { /* NEED TO ACTIVATE SINK */
		  while ((require_num > 0) && (!ack_received)) {
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
                    if (debug) {
                      printf("ACTIVATION SRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                      printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                      printf("MSG LEN: 6 DATA: ");
                      for(int i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
                    }
      
                    send_len = sendmsg(sock, &msg, 0);
                    if (send_len < 0) {
                      perror("ACTIVATION sendmsg failed");
                    }
                    //if (debug) printf("ACTIVATION send len: %zd\n", send_len);
                    if (debug) printf("ACTIVATION send len: %d\n", send_len);

                    //memset(&read_fd_set, 0, sizeof(read_fd_set));
		    //timeout.tv_sec = 5;
		    //timeout.tv_usec = 0;
		    //sigemptyset(&emptyset);
  		    FD_ZERO(&read_fd_set);
                    FD_SET(sock, &read_fd_set);
                    select_len = pselect(sock + 1, &read_fd_set, NULL, NULL, &timeout, NULL);
                    //select_len = pselect(sock + 1, &read_fd_set, NULL, NULL, &timeout, &emptyset);
                    //select_len = select(sock + 1, &read_fd_set, NULL, NULL, &timeout);
                    if (select_len == 0) {
                      printf("No reply, fail to activate %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                      // mark sink unreachable
                    } else if (select_len < 0) {
                      perror("Select");
                    } else {
                      /* we get response from activated sink */
                      ack_received = 1;
                      if (debug) printf("RECEIVE ACTIVATION ACK\n");
                    }
		   
                  } else { /* if (activate_list[curr_num] != NULL) */
                    if (debug) printf("not enough candidate sink available\n");
                      //require_num = 0;
		    break;
                  } /* end if (activate_list[curr_num] != NULL) */
		  } /* end while */
		  curr_num = 0;
                } else if (require_num < 0) { /* NEED TO ACTIVATE SINK */
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
                    if (debug) {
                      printf("DEACTIVATION SRC: %s\n", inet_ntop(AF_INET6, &pktinfo->ipi6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                      printf("DST: %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                      printf("MSG LEN: 6 DATA: ");
                      for(int i=0;i<6;i++) { printf("%02hhX ", reply[i]); } printf("\n");
                    }
      
                    send_len = sendmsg(sock, &msg, 0);
                    if (send_len < 0) {
                      perror("DEACTIVATION sendmsg failed");
                    }
                    //if (debug) printf("DEACTIVATION send len: %zd\n", send_len);
                    if (debug) printf("DEACTIVATION send len: %d\n", send_len);
      
                    //memset(&read_fd_set, 0, sizeof(read_fd_set));
		    //timeout.tv_sec = 5;
		    //timeout.tv_usec = 0;
		    //sigemptyset(&emptyset);
  		    FD_ZERO(&read_fd_set);
                    FD_SET(sock, &read_fd_set);
                    select_len = pselect(sock + 1, &read_fd_set, NULL, NULL, &timeout, NULL);
                    //select_len = pselect(sock + 1, &read_fd_set, NULL, NULL, &timeout, &emptyset);
                    //select_len = select(sock + 1, &read_fd_set, NULL, NULL, &timeout);
                    if (select_len == 0) {
                      printf("No reply, fail to deactivate %s\n", inet_ntop(AF_INET6, &sink_addr.sin6_addr, sink_addr_str, INET6_ADDRSTRLEN));
                      // mark sink unreachable
                    } else if (select_len < 0) {
                      perror("Select");
                    } else {
                      /* we get response from activated sink */
                      ack_received = 1;
                      if (debug) printf("RECEIVE DEACTIVATION ACK\n");
                    }

                  } else { /* if (deactivate_sink!=NULL) */
                    if (debug) printf("deactivate_sink==NULL\n");
                    //require_num = 0;
                  }
                } /* else if (require_num < 0) */

		break;
	      case(2): /* activate */

                if (debug) printf("flags: %02hhX activate\n", flags);
                /* check activation ack */
                if (flags & 0x40) {
                  printf("RECEIVE ACTIVATION ACK %s\n", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  node = find_sink_list(sink_table, &tmp_addr);
                  if (node != NULL) {
                    if (node->activated == 0) {
                      printf("Activated!\n");
                      node->activated = 1;
                      node->holddown = HOLDDOWN;
		      if (trigger_node != NULL) {
                        trigger_node->holddown = HOLDDOWN;
                        trigger_node = NULL;
		      }
                      require_num = 0;
                      for (int i=0; i<MAX_ACT_LIST; i++) {
                        activate_list[i] = NULL;
                      }
                    } else {
                      printf("Already activated\n");
                    }
                  } else {
                    /* do nothing here */
                    printf("node does not exist in our sink table\n");
                  }
                } /* if (flags & 0x40) */

		break;
	      case(3): /* deactivate */

                if (debug) printf("flags: %02hhX deactivate\n", flags);
                /* check deactivation ack */
                if (flags & 0x40) {
                  printf("RECEIVE DEACTIVATION ACK %s\n", inet_ntop(AF_INET6, &tmp_addr.sin6_addr, tmp_addr_str, INET6_ADDRSTRLEN));
                  node = find_sink_list(sink_table, &tmp_addr);
                  if (node != NULL) {
                    if (node->activated == 1) {
                      printf("Dectivated!\n");
                      node->activated = 0;
                      node->holddown = HOLDDOWN;
		      if (trigger_node != NULL) {
                        trigger_node->holddown = HOLDDOWN;
                        trigger_node = NULL;
		      }
                      //node->holddown = 0; // since dynamic sink in sensor role sends no report
                      node->rank = 0;
                      require_num = 0;
                      deactivate_sink = NULL;
                    } else if (node->activated > 1) {
                      printf("Cannot deactivate fixed sink\n");
                    } else {
                      printf("Already deactivated\n");
                    }
                  } else {
                    /* do nothing here */
                    printf("node does not exist in our sink table\n");
                  } /* else if (require_num < 0) */
                } /* if (flags & 0x40) */

		break;
	      default:
		printf("unknown flags: %02hhx\n", flags);

	    } /* switch */
	    if (debug) printf("END switch case\n");
	  } else { /* if (strcmp... */
	    if (debug) printf("packet destination is not for the coordinator\n");
	  } /* end if (strcmp... */

  } /* end while(1) */
} /* end process */
/*---------------------------------------------------------------------------*/
void usage(void)
{
  printf("\nRPL virtual root coordinator\n");
  printf("\ncoordinator [-d] [-f file] [-c command ipv6] [-s ipv6]\n");
  printf(" -d               -- debug mode\n");
  printf(" -f filename      -- local logfile. Default is %s\n", LOGFILE);
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
  //int i;
  char *filename = LOGFILE;
  for(int i = 1; (i < argc) && (argv[i][0] == '-'); i++)  {
    if (strncmp(argv[i], "-h", 2) == 0)
      usage();
    else if (strncmp(argv[i], "-d", 2) == 0)
      debug = 1;
    else if (strncmp(argv[i], "-f", 2) == 0)
      filename = argv[++i];
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


