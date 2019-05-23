/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-udp-packet.h"
#include "net/rpl/rpl.h"
#include "dev/serial-line.h"
#if CONTIKI_TARGET_Z1
#include "dev/uart0.h"
#else
#include "dev/uart1.h"
#endif

#include <stdio.h>
#include <string.h>

#define UDP_CLIENT_PORT 8775
#define UDP_SERVER_PORT 5688

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#include "lib/random.h"
#include "sys/ctimer.h"
#include <stdlib.h>

#if RPL_CONF_STATS
#include "net/rpl/rpl-private.h"
#endif

#if WITH_COMPOWER
#include "powertrace.h"
#endif

#include "net/link-stats.h"

static struct uip_udp_conn *client_conn;
static uip_ipaddr_t server_ipaddr;

#define SEND_INTERVAL_60S       (60 * CLOCK_SECOND)
#define SEND_TIME_60S           (random_rand() % (SEND_INTERVAL_60S))
#define SEND_INTERVAL_VARY      ((60/RATE) * CLOCK_SECOND)
#define SEND_TIME_VARY          (random_rand() % (SEND_INTERVAL_VARY))
#define MAX_PAYLOAD_LEN         80
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

static char buf[MAX_PAYLOAD_LEN];
static int seq_id;
static int ctr=0;
static int not_done=1;
/*---------------------------------------------------------------------------*/
PROCESS(sender_process, "sender process");
AUTOSTART_PROCESSES(&sender_process);
/*---------------------------------------------------------------------------*/
#if SEND_RETRY
PROCESS(send_retry_process, "send retry process");
#endif
/*---------------------------------------------------------------------------*/
static void
show_routes(void)
{
  uip_ds6_route_t *r;
  uip_ipaddr_t *ipaddr;
  if((ipaddr = uip_ds6_defrt_choose()) != NULL) {
//    printf("defrt:%02x%02x\n", ipaddr->u8[14], ipaddr->u8[15]);
    printf("defrt:%02x\n", ipaddr->u8[15]);
  } else {
    printf("defrt: NULL\n");
  }
//  printf("ROUTES:\n");
  r = uip_ds6_route_head();
  if (r != NULL)
  {
    for(r = uip_ds6_route_head(); r != NULL; r = uip_ds6_route_next(r)) {
//        printf("dst:%02x%02x via:%02x%02x valid:%lu\n",
        printf("dst:%02x via:%02x valid:%lu\n",
//          r->ipaddr.u8[14],
          r->ipaddr.u8[15],
//          uip_ds6_route_nexthop(r)->u8[14],
          uip_ds6_route_nexthop(r)->u8[15],
          r->state.lifetime
        );
    }
  }
}
/*---------------------------------------------------------------------------*/
#if UIP_CONF_STATISTICS
static void
show_uip_stats(void)
{
  printf("UIP STATS: %u %u %u %u\n",
        uip_stat.ip.recv, uip_stat.ip.sent, uip_stat.ip.forwarded, uip_stat.ip.drop);
}
#endif
/*---------------------------------------------------------------------------*/
#if RPL_CONF_STATS
static void
show_rpl_stats(void)
{
  printf("RPL STATS: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
        rpl_stats.mem_overflows,
        rpl_stats.local_repairs,
        rpl_stats.global_repairs,
        rpl_stats.malformed_msgs,
        rpl_stats.resets,
        rpl_stats.parent_switch,
        rpl_stats.forward_errors,
        rpl_stats.loop_errors,
        rpl_stats.loop_warnings,
        rpl_stats.root_repairs,
	default_instance->dio_totint,
	default_instance->dio_totsend,
	default_instance->dio_totrecv,
        rpl_stats.dio_uc,
        rpl_stats.dio_mc,
        rpl_stats.dao_add,
        rpl_stats.dao_remove,
        rpl_stats.dis_out,
        rpl_stats.dis_ext_in,
        rpl_stats.dis_ext_out);
}
#endif /* #if RPL_CONF_STATS */
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
#if SERVER_REPLY
  char *str;
  int ack;
#endif

  if(uip_newdata()) {
    /* Ignore incoming data */
#if SERVER_REPLY
    /* if SERVER_REPLY, sink replies with seq_id as incoming data */
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    ack = atoi(str);
    PRINTF("ACK %d HOP %d\n", ack, uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1);

#if SEND_RETRY
   process_post(&send_retry_process, PROCESS_EVENT_CONTINUE, NULL);
#endif /* #if SEND_RETRY */

#endif /* #if SERVER_REPLY */
  }
}
/*---------------------------------------------------------------------------*/
#if SEND_RETRY
PROCESS_THREAD(send_retry_process, ev, data)
{
  static struct etimer periodic;
  static int retry;

  PROCESS_BEGIN();
 
  retry = SEND_RETRY;
  
  etimer_set(&periodic, 3*CLOCK_SECOND);
  
  while(retry>0) {
    PROCESS_WAIT_EVENT();

    if(etimer_expired(&periodic)) {
      /* resend packet to the same server_ipaddr */
      uip_udp_packet_sendto(client_conn, buf, strlen(buf),
                        &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
      retry--;
PRINTF("RET %d SEQ %d\n", SEND_RETRY-retry, seq_id);
      etimer_reset(&periodic);
    } else if(ev == PROCESS_EVENT_CONTINUE) {
      PROCESS_EXIT();
    }
  }
  
  PROCESS_END();
}
#endif
/*---------------------------------------------------------------------------*/
static void
send_packet(void *ptr)
{
//  char buf[MAX_PAYLOAD_LEN];
  uint16_t parent_etx;
  uint16_t rtmetric;
  uint16_t num_neighbors;
  rpl_parent_t *preferred_parent;
  linkaddr_t parent;
  rpl_dag_t *dag;

  seq_id++;

  linkaddr_copy(&parent, &linkaddr_null);
  parent_etx = 0;

  /* assume we have only one RPL instance */
  dag = rpl_get_any_dag();
  if(dag != NULL) {
    preferred_parent = dag->preferred_parent;
    if(preferred_parent != NULL) {
      uip_ds6_nbr_t *nbr;
      nbr = uip_ds6_nbr_lookup(rpl_get_parent_ipaddr(preferred_parent));
      if(nbr != NULL) {
        /* Use parts of the IPv6 address as the parent address, in reversed byte order. */
        parent.u8[LINKADDR_SIZE - 1] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 2];
        parent.u8[LINKADDR_SIZE - 2] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 1];
        parent_etx = rpl_get_parent_link_metric(preferred_parent);
      }
    }
    rtmetric = dag->rank;
    num_neighbors = uip_ds6_nbr_num();
  } else {
    rtmetric = 0;
    num_neighbors = 0;
  }

  /* use DODAG ID as the destination server ip address */
/*
  if (!uip_ipaddr_cmp(&server_ipaddr, &dag->dag_id)) {
    uip_ipaddr_copy(&server_ipaddr, &dag->dag_id);
  }
*/
uip_ip6addr(&server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0x0001);

  sprintf(buf, "%d %02x%02x %u %u %u", seq_id, 
         parent.u8[LINKADDR_SIZE-1], parent.u8[LINKADDR_SIZE-2], 
	 parent_etx, rtmetric, num_neighbors);

  strcat(buf, " ");
  while(strlen(buf)<64) {
	strcat(buf, "0");
  }
//  sprintf(buf, "%d %u %u %u", seq_id, parent_etx, rtmetric, num_neighbors);
  PRINTF("SND %d DATA %d %02x%02x %u %u %u\n",
         server_ipaddr.u8[sizeof(server_ipaddr.u8) - 1], seq_id, 
         parent.u8[LINKADDR_SIZE-1], parent.u8[LINKADDR_SIZE-2], 
	 parent_etx, rtmetric, num_neighbors);
  uip_udp_packet_sendto(client_conn, buf, strlen(buf),
                        &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));

#if SEND_RETRY
  process_start(&send_retry_process, NULL);
#endif 
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;
  int i;
  uint8_t state;

  uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  printf("IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      printf("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sender_process, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  PRINTF("sender_process started\n");

#if WITH_COMPOWER
  powertrace_start(CLOCK_SECOND * 60);
#endif

  set_global_address();

//  print_local_addresses();
/*
PRINTF("nbr:%d routes:%d queuebuf:%d\n", NBR_TABLE_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES, QUEUEBUF_CONF_NUM);
printf("Ticks per second: %u\n", RTIMER_SECOND);
printf("clock_time_t size:%d\n", sizeof(clock_time_t));
printf("unsigned long:%d\n", sizeof(unsigned long));
printf("unsigned long long:%d\n", sizeof(unsigned long long));
printf("uint32_t:%d\n", sizeof(uint32_t));
printf("int:%d\n", sizeof(int));
*/

#if RESCUE
  start_rpl_metric_timer();
#endif 

/* wait 2 min for RPL topology to form */
etimer_set(&periodic, (120*CLOCK_SECOND));
PROCESS_YIELD();
if(etimer_expired(&periodic)) {
/* wait 2 minutes before starting connection to server */
//    /* just continue after timer expired */
}

  /* new connection with remote host */
  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
        UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

/* loop to send periodic data */
etimer_set(&periodic, SEND_INTERVAL_60S);
  while(not_done) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
    if(etimer_expired(&periodic)) {
      ctr++;
//PRINTF("ctr:%d\n", ctr);
      if (ctr<=10) { // send rate 1 pkt/60s
	ctimer_set(&backoff_timer, SEND_TIME_60S, send_packet, NULL);
        etimer_reset(&periodic);
if ((ctr == 3)||(ctr == 8)) {
printf("MARK %d BEGIN\n",ctr);
  show_routes();
  show_uip_stats();
  print_link_stats();
#if SINK_ADDITION
if (default_instance != NULL) {
  printf("TOPO: %u %u\n", default_instance->tree_size, default_instance->longest_hop);
}
#endif
#if SINK_ADDITION || SENSOR_PRINT
if (default_instance != NULL) {
  printf("RANK: %u TRAFFIC: %lu %lu \n", default_instance->current_dag->rank, default_instance->received_traffic, default_instance->highest_traffic);
}
#endif
printf("MARK %d END\n",ctr);
}
      } else if (ctr<=(10+(10*RATE))) { // send rate 1 pkt/30s
	ctimer_set(&backoff_timer, SEND_TIME_VARY, send_packet, NULL);
        if (ctr==11) {
          etimer_set(&periodic, SEND_INTERVAL_VARY);
        } else {
          etimer_reset(&periodic);
if ((ctr == (10+(2*RATE)+1)) || (ctr == (10+(7*RATE)+1))) {
printf("MARK %d BEGIN\n",ctr);
  show_routes();
  show_uip_stats();
  print_link_stats();
#if SINK_ADDITION
if (default_instance != NULL) {
  printf("TOPO: %u %u\n", default_instance->tree_size, default_instance->longest_hop);
}
#endif
#if SINK_ADDITION || SENSOR_PRINT
if (default_instance != NULL) {
  printf("RANK: %u TRAFFIC: %lu %lu \n", default_instance->current_dag->rank, default_instance->received_traffic, default_instance->highest_traffic);
}
#endif
printf("MARK %d END\n",ctr);
}
        }
      } else if (ctr<=TOTAL_SEND) { // send rate 1 pkt/60s
	ctimer_set(&backoff_timer, SEND_TIME_60S, send_packet, NULL);
        if (ctr==(10+(10*RATE)+1)) {
          etimer_set(&periodic, SEND_INTERVAL_60S);
        } else {
          etimer_reset(&periodic);
if ((ctr == (10+(10*RATE)+3))||(ctr == (10+(10*RATE)+8))) {
printf("MARK %d BEGIN\n",ctr);
  show_routes();
  show_uip_stats();
  print_link_stats();
#if SINK_ADDITION
if (default_instance != NULL) {
  printf("TOPO: %u %u\n", default_instance->tree_size, default_instance->longest_hop);
}
#endif
#if SINK_ADDITION || SENSOR_PRINT
if (default_instance != NULL) {
  printf("RANK: %u TRAFFIC: %lu %lu \n", default_instance->current_dag->rank, default_instance->received_traffic, default_instance->highest_traffic);
}
#endif
printf("MARK %d END\n",ctr);
}
        }
      } else {
        if (not_done) {
          not_done--;
          etimer_reset(&periodic);
        }
      }
    } /* etimer_expired(&periodic) */
  } /* end while */

/* wait 10s before printing out */
etimer_set(&periodic, 10*CLOCK_SECOND);
PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
  show_routes();
#if RPL_CONF_STATS
  show_rpl_stats();
  print_link_stats();
#endif

#if WITH_COMPOWER
  powertrace_stop();
#endif

#if RESCUE
stop_rpl_metric_timer();
#endif

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
