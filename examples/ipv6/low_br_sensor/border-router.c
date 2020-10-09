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
/**
 * \file
 *         border-router
 * \author
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 *         Nicolas Tsiftes <nvt@sics.se>
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#if RPL_WITH_NON_STORING
#include "net/rpl/rpl-ns.h"
#endif /* RPL_WITH_NON_STORING */
#include "net/netstack.h"
#include "dev/button-sensor.h"
#include "dev/slip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//#define DEBUG DEBUG_NONE
#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#include "net/link-stats.h"
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#if WITH_COMPOWER
#include "powertrace.h"
#endif

static uint8_t prefix_set;
/*---------------------------------------------------------------------------*/
#define UDP_CLIENT_PORT 8775
#define UDP_SERVER_PORT 5688

static struct uip_udp_conn *client_conn;
static uip_ipaddr_t server_ipaddr;

#define SEND_INTERVAL_60S       (60 * CLOCK_SECOND)
#define SEND_TIME_60S           (random_rand() % (SEND_INTERVAL_60S))
#define SEND_INTERVAL_VARY      ((60/RATE) * CLOCK_SECOND)
#define SEND_TIME_VARY          (random_rand() % (SEND_INTERVAL_VARY))
#define MAX_PAYLOAD_LEN         80

static char sbuf[MAX_PAYLOAD_LEN];
static int seq_id;
static int ctr;
static int not_done=1;

static uip_ds6_addr_t *global_6lowpan=NULL;
/*---------------------------------------------------------------------------*/
PROCESS(border_router_process, "Candidate sink process");

#if WEBSERVER==0
/* No webserver */
AUTOSTART_PROCESSES(&border_router_process);
#elif WEBSERVER>1
/* Use an external webserver application */
#include "webserver-nogui.h"
AUTOSTART_PROCESSES(&border_router_process,&webserver_nogui_process);
#else
/* Use simple webserver with only one page for minimum footprint.
 * Multiple connections can result in interleaved tcp segments since
 * a single static buffer is used for all segments.
 */
#include "httpd-simple.h"
/* The internal webserver can provide additional information if
 * enough program flash is available.
 */
#define WEBSERVER_CONF_LOADTIME 0
#define WEBSERVER_CONF_FILESTATS 0
#define WEBSERVER_CONF_NEIGHBOR_STATUS 0
/* Adding links requires a larger RAM buffer. To avoid static allocation
 * the stack can be used for formatting; however tcp retransmissions
 * and multiple connections can result in garbled segments.
 * TODO:use PSOCk_GENERATOR_SEND and tcp state storage to fix this.
 */
#define WEBSERVER_CONF_ROUTE_LINKS 0
#if WEBSERVER_CONF_ROUTE_LINKS
#define BUF_USES_STACK 1
#endif

PROCESS(webserver_nogui_process, "Web server");
PROCESS_THREAD(webserver_nogui_process, ev, data)
{
  PROCESS_BEGIN();

  httpd_init();

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
    httpd_appcall(data);
  }

  PROCESS_END();
}
AUTOSTART_PROCESSES(&border_router_process,&webserver_nogui_process);

static const char *TOP = "<html><head><title>ContikiRPL</title></head><body>\n";
static const char *BOTTOM = "</body></html>\n";
#if BUF_USES_STACK
static char *bufptr, *bufend;
#define ADD(...) do {                                                   \
    bufptr += snprintf(bufptr, bufend - bufptr, __VA_ARGS__);      \
  } while(0)
#else
static char buf[256];
static int blen;
#define ADD(...) do {                                                   \
    blen += snprintf(&buf[blen], sizeof(buf) - blen, __VA_ARGS__);      \
  } while(0)
#endif

/*---------------------------------------------------------------------------*/
#if SEND_RETRY
PROCESS(send_retry_process, "send retry process");
#endif
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{

  if (get_operate_mode()==0) {
    PRINTF("send data in sensor mode\n");
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

  } else {
    PRINTF("Do nothing in sink mode\n");
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
      uip_udp_packet_sendto(client_conn, sbuf, strlen(sbuf),
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
/* Current implementation: we send to UDP server at fd00::1 */
uip_ip6addr(&server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0x0001);

  sprintf(sbuf, "%d %02x%02x %u %u %u", seq_id,
         parent.u8[LINKADDR_SIZE-1], parent.u8[LINKADDR_SIZE-2],
         parent_etx, rtmetric, num_neighbors);

  strcat(sbuf, " ");
  while(strlen(sbuf)<64) {
        strcat(sbuf, "0");
  }
//  sprintf(sbuf, "%d %u %u %u", seq_id, parent_etx, rtmetric, num_neighbors);
  PRINTF("SND %d DATA %d %02x%02x %u %u %u\n",
         server_ipaddr.u8[sizeof(server_ipaddr.u8) - 1], seq_id,
         parent.u8[LINKADDR_SIZE-1], parent.u8[LINKADDR_SIZE-2],
         parent_etx, rtmetric, num_neighbors);
  //memcpy(&UIP_IP_BUF->srcipaddr,&global_6lowpan.ipaddr, sizeof(uip_ipaddr_t));
  uip_ipaddr_copy(&UIP_IP_BUF->srcipaddr,&global_6lowpan->ipaddr);
  uip_udp_packet_sendto(client_conn, sbuf, strlen(sbuf),
                        &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));

#if SEND_RETRY
  process_start(&send_retry_process, NULL);
#endif 
}
/*---------------------------------------------------------------------------*/
static void
ipaddr_add(const uip_ipaddr_t *addr)
{
  uint16_t a;
  int i, f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) ADD("::");
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        ADD(":");
      }
      ADD("%x", a);
    }
  }
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(generate_routes(struct httpd_state *s))
{
  static uip_ds6_route_t *r;
#if RPL_WITH_NON_STORING
  static rpl_ns_node_t *link;
#endif /* RPL_WITH_NON_STORING */
  static uip_ds6_nbr_t *nbr;
#if BUF_USES_STACK
  char buf[256];
#endif
#if WEBSERVER_CONF_LOADTIME
  static clock_time_t numticks;
  numticks = clock_time();
#endif

  PSOCK_BEGIN(&s->sout);

  SEND_STRING(&s->sout, TOP);
#if BUF_USES_STACK
  bufptr = buf;bufend=bufptr+sizeof(buf);
#else
  blen = 0;
#endif
  ADD("Neighbors<pre>");

  for(nbr = nbr_table_head(ds6_neighbors);
      nbr != NULL;
      nbr = nbr_table_next(ds6_neighbors, nbr)) {

#if WEBSERVER_CONF_NEIGHBOR_STATUS
#if BUF_USES_STACK
{char* j=bufptr+25;
      ipaddr_add(&nbr->ipaddr);
      while (bufptr < j) ADD(" ");
      switch (nbr->state) {
      case NBR_INCOMPLETE: ADD(" INCOMPLETE");break;
      case NBR_REACHABLE: ADD(" REACHABLE");break;
      case NBR_STALE: ADD(" STALE");break;
      case NBR_DELAY: ADD(" DELAY");break;
      case NBR_PROBE: ADD(" NBR_PROBE");break;
      }
}
#else
{uint8_t j=blen+25;
      ipaddr_add(&nbr->ipaddr);
      while (blen < j) ADD(" ");
      switch (nbr->state) {
      case NBR_INCOMPLETE: ADD(" INCOMPLETE");break;
      case NBR_REACHABLE: ADD(" REACHABLE");break;
      case NBR_STALE: ADD(" STALE");break;
      case NBR_DELAY: ADD(" DELAY");break;
      case NBR_PROBE: ADD(" NBR_PROBE");break;
      }
}
#endif
#else
      ipaddr_add(&nbr->ipaddr);
#endif

      ADD("\n");
#if BUF_USES_STACK
      if(bufptr > bufend - 45) {
        SEND_STRING(&s->sout, buf);
        bufptr = buf; bufend = bufptr + sizeof(buf);
      }
#else
      if(blen > sizeof(buf) - 45) {
        SEND_STRING(&s->sout, buf);
        blen = 0;
      }
#endif
  }
  ADD("</pre>Routes<pre>\n");
  SEND_STRING(&s->sout, buf);
#if BUF_USES_STACK
  bufptr = buf; bufend = bufptr + sizeof(buf);
#else
  blen = 0;
#endif

  for(r = uip_ds6_route_head(); r != NULL; r = uip_ds6_route_next(r)) {

#if BUF_USES_STACK
#if WEBSERVER_CONF_ROUTE_LINKS
    ADD("<a href=http://[");
    ipaddr_add(&r->ipaddr);
    ADD("]/status.shtml>");
    ipaddr_add(&r->ipaddr);
    ADD("</a>");
#else
    ipaddr_add(&r->ipaddr);
#endif
#else
#if WEBSERVER_CONF_ROUTE_LINKS
    ADD("<a href=http://[");
    ipaddr_add(&r->ipaddr);
    ADD("]/status.shtml>");
    SEND_STRING(&s->sout, buf); //TODO: why tunslip6 needs an output here, wpcapslip does not
    blen = 0;
    ipaddr_add(&r->ipaddr);
    ADD("</a>");
#else
    ipaddr_add(&r->ipaddr);
#endif
#endif
    ADD("/%u (via ", r->length);
    ipaddr_add(uip_ds6_route_nexthop(r));
    if(1 || (r->state.lifetime < 600)) {
      ADD(") %lus\n", (unsigned long)r->state.lifetime);
    } else {
      ADD(")\n");
    }
    SEND_STRING(&s->sout, buf);
#if BUF_USES_STACK
    bufptr = buf; bufend = bufptr + sizeof(buf);
#else
    blen = 0;
#endif
  }
  ADD("</pre>");

#if RPL_WITH_NON_STORING
  ADD("Links<pre>\n");
  SEND_STRING(&s->sout, buf);
#if BUF_USES_STACK
  bufptr = buf; bufend = bufptr + sizeof(buf);
#else
  blen = 0;
#endif
  for(link = rpl_ns_node_head(); link != NULL; link = rpl_ns_node_next(link)) {
    if(link->parent != NULL) {
      uip_ipaddr_t child_ipaddr;
      uip_ipaddr_t parent_ipaddr;

      rpl_ns_get_node_global_addr(&child_ipaddr, link);
      rpl_ns_get_node_global_addr(&parent_ipaddr, link->parent);

#if BUF_USES_STACK
#if WEBSERVER_CONF_ROUTE_LINKS
      ADD("<a href=http://[");
      ipaddr_add(&child_ipaddr);
      ADD("]/status.shtml>");
      ipaddr_add(&child_ipaddr);
      ADD("</a>");
#else
      ipaddr_add(&child_ipaddr);
#endif
#else
#if WEBSERVER_CONF_ROUTE_LINKS
      ADD("<a href=http://[");
      ipaddr_add(&child_ipaddr);
      ADD("]/status.shtml>");
      SEND_STRING(&s->sout, buf); //TODO: why tunslip6 needs an output here, wpcapslip does not
      blen = 0;
      ipaddr_add(&child_ipaddr);
      ADD("</a>");
#else
      ipaddr_add(&child_ipaddr);
#endif
#endif

      ADD(" (parent: ");
      ipaddr_add(&parent_ipaddr);
      if(1 || (link->lifetime < 600)) {
        ADD(") %us\n", (unsigned int)link->lifetime); // iotlab printf does not have %lu
        //ADD(") %lus\n", (unsigned long)r->state.lifetime);
      } else {
        ADD(")\n");
      }
      SEND_STRING(&s->sout, buf);
#if BUF_USES_STACK
      bufptr = buf; bufend = bufptr + sizeof(buf);
#else
      blen = 0;
#endif
    }
  }
  ADD("</pre>");
#endif /* RPL_WITH_NON_STORING */

#if WEBSERVER_CONF_FILESTATS
  static uint16_t numtimes;
  ADD("<br><i>This page sent %u times</i>",++numtimes);
#endif

#if WEBSERVER_CONF_LOADTIME
  numticks = clock_time() - numticks + 1;
  ADD(" <i>(%u.%02u sec)</i>",numticks/CLOCK_SECOND,(100*(numticks%CLOCK_SECOND))/CLOCK_SECOND));
#endif

  SEND_STRING(&s->sout, buf);
  SEND_STRING(&s->sout, BOTTOM);

  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
httpd_simple_script_t
httpd_simple_get_script(const char *name)
{

  return generate_routes;
}

#endif /* WEBSERVER */

/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTA("Server IPv6 addresses:\n");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINTA("==>");
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTA("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
void
request_prefix(void)
{
  /* mess up uip_buf with a dirty request... */
  uip_buf[0] = '?';
  uip_buf[1] = 'P';
  uip_len = 2;
  slip_send();
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
set_prefix_64(uip_ipaddr_t *prefix_64)
{
  uip_ipaddr_t ipaddr;
  memcpy(&ipaddr, prefix_64, 16);
  prefix_set = 1;
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

#ifndef ROOT_VIRTUAL
  set_vr_addr();
#endif
}
/*---------------------------------------------------------------------------*/
static void
show_routes(void)
{
  uip_ds6_route_t *r;
  uip_ipaddr_t *ipaddr;
  if((ipaddr = uip_ds6_defrt_choose()) != NULL) {
    //printf("defrt:%02x%02x\n", ipaddr->u8[14], ipaddr->u8[15]);
    printf("defrt:%02x\n", ipaddr->u8[15]);
  } else {
    printf("defrt: NULL\n");
  }
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
PROCESS_THREAD(border_router_process, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;

  PROCESS_BEGIN();

#if WITH_COMPOWER
  powertrace_start(CLOCK_SECOND * 60);
#endif

/* While waiting for the prefix to be sent through the SLIP connection, the future
 * border router can join an existing DAG as a parent or child, or acquire a default
 * router that will later take precedence over the SLIP fallback interface.
 * Prevent that by turning the radio off until we are initialized as a DAG root.
 */
  prefix_set = 0;
  NETSTACK_MAC.off(0);

  PROCESS_PAUSE();

  SENSORS_ACTIVATE(button_sensor);

  PRINTF("Candidate sink started\n");
  printf("nbr:%d routes:%d queuebuf:%d\n", NBR_TABLE_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES, QUEUEBUF_CONF_NUM);
#if 0
   /* The border router runs with a 100% duty cycle in order to ensure high
     packet reception rates.
     Note if the MAC RDC is not turned off now, aggressive power management of the
     cpu will interfere with establishing the SLIP connection */
  NETSTACK_MAC.off(1);
#endif

  /* Request prefix until it has been received */
  while(!prefix_set) {
    etimer_set(&periodic, CLOCK_SECOND);
    request_prefix();
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
  }
  global_6lowpan = uip_ds6_get_global(ADDR_PREFERRED);
//  PRINTF("prefix set\n");

  /* Now turn the radio on, but disable radio duty cycling.
   * Since we are the DAG root, reception delays would constrain mesh throughbut.
   */
//  NETSTACK_MAC.off(1);
  NETSTACK_MAC.on();

#if DEBUG || 1
  print_local_addresses();
#endif

//#if (SINK_ADDITION == 1)
#if (SINK_ADDITION >= 1)
  start_rpl_metric_timer();
#endif
#if SINK_ADDITION
start_rpl_parent_queue();
#endif

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

  etimer_set(&periodic, (SEND_INTERVAL_60S));
  while(not_done) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }

    if(etimer_expired(&periodic)) {
      ctr++;
      if (ctr<=10) { // send rate 1 ppm
        ctimer_set(&backoff_timer, SEND_TIME_60S, send_packet, NULL);
        etimer_reset(&periodic);
//if ((ctr == 3)||(ctr == 8)) {
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
//}
      } else if (ctr<=60) { // send rate 5 ppm
        ctimer_set(&backoff_timer, (random_rand() % (12 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==11) {
          etimer_set(&periodic, (12 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (12 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=160) { // send rate 10 ppm
        ctimer_set(&backoff_timer, (random_rand() % (6 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==61) {
          etimer_set(&periodic, (6 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (6 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=310) { // send rate 15 ppm
        ctimer_set(&backoff_timer, (random_rand() % (4 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==161) {
          etimer_set(&periodic, (4 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (4 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=710) { // send rate 20 ppm
        ctimer_set(&backoff_timer, (random_rand() % (3 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==311) {
          etimer_set(&periodic, (3 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (3 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=860) { // send rate 15 ppm
        ctimer_set(&backoff_timer, (random_rand() % (4 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==711) {
          etimer_set(&periodic, (4 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (4 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=960) { // send rate 10 ppm
        ctimer_set(&backoff_timer, (random_rand() % (6 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==861) {
          etimer_set(&periodic, (6 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (6 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=1010) { // send rate 5 ppm
        ctimer_set(&backoff_timer, (random_rand() % (12 * CLOCK_SECOND)), send_packet, NULL);
        if (ctr==961) {
          etimer_set(&periodic, (12 * CLOCK_SECOND));
printf("RATE INCREASE %lu\n", (12 * CLOCK_SECOND));
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else if (ctr<=TOTAL_SEND) { // send rate 1 ppm
        ctimer_set(&backoff_timer, SEND_TIME_60S, send_packet, NULL);
        if (ctr==1011) {
          etimer_set(&periodic, SEND_INTERVAL_60S);
printf("RATE DECREASE %lu\n",SEND_INTERVAL_60S);
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: - DEPTH: - RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        } else {
          etimer_reset(&periodic);
        }

      } else {
        if (not_done) {
          not_done--;
          etimer_reset(&periodic);
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
  if (default_instance->current_dag->preferred_parent != NULL) {
    printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
  } else {
    printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
  }
}
#endif
printf("MARK %d END\n",ctr);
        }
      }
    } /* etimer_expired(&periodic) */
  } /* while(not_done) */

/* wait 3 more minutes */
while(ctr<=(TOTAL_SEND + 3)) {
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
  if (etimer_expired(&periodic)) {
    ctr++;
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
      if (default_instance->current_dag->preferred_parent != NULL) {
        printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: %02x ETX: %u\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
      } else {
        printf("SIZE: %u DEPTH: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d RANK: %u PRN: 00 ETX: 000\n", default_instance->tree_size, default_instance->longest_hop, get_rx(), get_nbr_highest(), default_instance->energy, rpl_parent_queue_len(), default_instance->current_dag->rank);
      }
    }
    #endif
    printf("MARK %d END\n",ctr);
    etimer_reset(&periodic);
  }
}

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

#if SINK_ADDITION
stop_rpl_parent_queue();
stop_rpl_metric_timer();
#endif

#if SINK_ADDITION
printf("SLIP: %lu %lu \n", slip_get_input_bytes(), slip_get_output_bytes());
if (default_instance != NULL) {
  printf("METRIC: %u %u %lu %lu \n", default_instance->tree_size, default_instance->longest_hop, default_instance->received_traffic, default_instance->highest_traffic);
}
#endif

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
