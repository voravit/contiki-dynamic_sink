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
static int interval_ctr=0;
#if WITH_COMPOWER
#include "powertrace.h"
#endif

static uip_ipaddr_t prefix;
static uint8_t prefix_set;

PROCESS(border_router_process, "Border router process");

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
//PROCESS(metric_timer_process, "metric timer process");
//AUTOSTART_PROCESSES(&border_router_process,&webserver_nogui_process,&metric_timer_process);

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
  rpl_dag_t *dag;
  uip_ipaddr_t ipaddr;
  memcpy(&prefix, prefix_64, 16);
  memcpy(&ipaddr, prefix_64, 16);
  prefix_set = 1;
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

#ifdef ROOT_VIRTUAL
  uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0x0010);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
#else
  set_vr_addr();
#endif

  dag = rpl_set_root(RPL_DEFAULT_INSTANCE, &ipaddr);
  if(dag != NULL) {
    rpl_set_prefix(dag, &prefix, 64);
    PRINTF("created a new RPL dag\n");
  }
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
static void
tcpip_handler(void)
{
//  uint8_t *appdata;

  PRINTF("tcpip_handler: do nothing\n");
/*
  if(uip_newdata()) {
    appdata = (uint8_t *)uip_appdata;

    PRINTF("RCV %d HOP %d DATA %.*s\n",
            UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1],
            uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1, uip_datalen(), appdata);
  }
*/
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(border_router_process, ev, data)
{
  static struct etimer et;
//  static int retry=3;

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

  PRINTF("RPL-Border router started\n");
  PRINTF("nbr:%d routes:%d queuebuf:%d\n", NBR_TABLE_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES, QUEUEBUF_CONF_NUM);
#if 0
   /* The border router runs with a 100% duty cycle in order to ensure high
     packet reception rates.
     Note if the MAC RDC is not turned off now, aggressive power management of the
     cpu will interfere with establishing the SLIP connection */
  NETSTACK_MAC.off(1);
#endif

  /* Request prefix until it has been received */
  while(!prefix_set) {
    etimer_set(&et, CLOCK_SECOND);
    request_prefix();
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }
//  PRINTF("prefix set\n");
#if SINK_ADDITION
  set_operate_mode(SINK_ADDITION);
#endif

  /* Now turn the radio on, but disable radio duty cycling.
   * Since we are the DAG root, reception delays would constrain mesh throughbut.
   */
  NETSTACK_MAC.off(1);

#if DEBUG || 1
  print_local_addresses();
#endif

#if (SINK_ADDITION >= 1)
//  start_rpl_metric_timer();
  start_rpl_parent_queue();
#endif

  etimer_set(&et, 60*CLOCK_SECOND);
  while(interval_ctr<(2 + CONF_TOTAL_SEND + 1)) {
    PROCESS_YIELD();
    if (ev == sensors_event && data == &button_sensor) {
      PRINTF("Initiating global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    } else if(ev == tcpip_event) {
//PRINTF("EVENT: tcpip_event\n");
      tcpip_handler();
    } else if (etimer_expired(&et)) {
	interval_ctr++;
//PRINTF("interval_ctr:%d\n", interval_ctr);
if (((interval_ctr-2)==3)||((interval_ctr-2)==8)||
    ((interval_ctr-2)==13)||((interval_ctr-2)==18)||
    ((interval_ctr-2)==23)||((interval_ctr-2)==28)) {
printf("MARK %d BEGIN\n",(interval_ctr-2));
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
  printf("RANK: %u RX: %lu NBR: %lu ENR: %lu QLEN: %d\n", default_instance->current_dag->rank, default_instance->received_traffic, default_instance->highest_traffic, default_instance->energy, rpl_parent_queue_len());
}
#endif
printf("MARK %d END\n",(interval_ctr-2));
}
	etimer_reset(&et);
    }
  }

/* wait 10s before printing out */
etimer_set(&et, (10*CLOCK_SECOND));
PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  show_routes();
#if RPL_CONF_STATS
  show_rpl_stats();
  print_link_stats();
#endif

#if WITH_COMPOWER
  powertrace_stop();
#endif

#if (SINK_ADDITION >= 1)
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