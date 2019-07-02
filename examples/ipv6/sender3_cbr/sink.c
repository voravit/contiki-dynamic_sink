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
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/uip.h"
#include "net/rpl/rpl.h"
#include "net/linkaddr.h"

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include "dev/serial-line.h"
#if CONTIKI_TARGET_Z1
#include "dev/uart0.h"
#else
#include "dev/uart1.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT 8775
#define UDP_SERVER_PORT 5688

#if RPL_CONF_STATS
#include "net/rpl/rpl-private.h"
#endif

#define TIMER_INTERVAL           (60 * CLOCK_SECOND)

static struct uip_udp_conn *server_conn;
static int interval_ctr=0;
static int finish=0;

PROCESS(sink_process, "sink process");
AUTOSTART_PROCESSES(&sink_process);
/*---------------------------------------------------------------------------*/
static void
show_routes(void)
{
  uip_ds6_route_t *r;
  uip_ipaddr_t *ipaddr;
  if((ipaddr = uip_ds6_defrt_choose()) != NULL) {
    printf("defrt:%02x%02x\n", ipaddr->u8[14], ipaddr->u8[15]);
  } else {
    printf("defrt: NULL\n");
  }
  r = uip_ds6_route_head();
  if (r != NULL)
  {
    for(r = uip_ds6_route_head(); r != NULL; r = uip_ds6_route_next(r)) {
        printf("dst:%02x%02x via:%02x%02x valid:%lu\n",
          r->ipaddr.u8[14],
          r->ipaddr.u8[15],
          uip_ds6_route_nexthop(r)->u8[14],
          uip_ds6_route_nexthop(r)->u8[15],
          r->state.lifetime
        );
    }
  }
}
/*---------------------------------------------------------------------------*/
#if RPL_CONF_STATS
static void
show_rpl_stats(void)
{
  printf("RPL STATS: %u %u %u %u %u %u %u %u %u %u\n",
        rpl_stats.mem_overflows,
        rpl_stats.local_repairs,
        rpl_stats.global_repairs,
        rpl_stats.malformed_msgs,
        rpl_stats.resets,
        rpl_stats.parent_switch,
        rpl_stats.forward_errors,
        rpl_stats.loop_errors,
        rpl_stats.loop_warnings,
        rpl_stats.root_repairs);
}
#endif /* #if RPL_CONF_STATS */
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  uint8_t *appdata;

  if(uip_newdata()) {
    appdata = (uint8_t *)uip_appdata;

    PRINTF("RCV %d HOP %d DATA %.*s\n",
            UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1],
            uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1, uip_datalen(), appdata);

#if SERVER_REPLY
    char buf[4] = "";
    uint8_t *ptr;
    int ctr=0;
    ptr = appdata;

    /* seqno is the first data (number string) before the first space (0x20) */
    while ((*ptr != 0x20) && (ctr<uip_datalen())) {
      //PRINTF("CTR: %d PTR: %c\n", ctr, (char) *ptr);
      buf[ctr] = (char)(*ptr);
      ctr++;
      ptr = ptr + 1;
    }
    buf[ctr]='\0';

    PRINTF("RPY %d SEQ %s\n",
            UIP_IP_BUF->srcipaddr.u8[sizeof(UIP_IP_BUF->srcipaddr.u8) - 1], buf); 
    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    uip_udp_packet_send(server_conn, buf, sizeof(char)*strlen(buf));
    uip_create_unspecified(&server_conn->ripaddr);
#endif

  }
}
/*---------------------------------------------------------------------------*/
static uip_ipaddr_t *
set_global_address(void)
{
  static uip_ipaddr_t ipaddr;
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

  return &ipaddr;
}
/*---------------------------------------------------------------------------*/
static void
create_rpl_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if;

  root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    uip_ipaddr_t prefix;

    rpl_set_root(RPL_DEFAULT_INSTANCE, ipaddr);
    dag = rpl_get_any_dag();
    uip_ip6addr(&prefix, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, &prefix, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sink_process, ev, data)
{
  uip_ipaddr_t *ipaddr;
  static struct etimer periodic;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  SENSORS_ACTIVATE(button_sensor);

  PRINTF("sink_process started\n");

  ipaddr = set_global_address();

  create_rpl_dag(ipaddr);

  printf("nbr:%d routes:%d\n", NBR_TABLE_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

  /* The data sink runs with a 100% duty cycle in order to ensure high
     packet reception rates. */
//  NETSTACK_RDC.off(1);

  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));

  etimer_set(&periodic, TIMER_INTERVAL);
  while(!finish) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } else if (ev == sensors_event && data == &button_sensor) {
      PRINTF("Initiating global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    } else if (etimer_expired(&periodic)) {
      interval_ctr++;
      /* Keep running the loop for 2+TOTAL_SEND+2 minutes:
	 2		sensors waiting time before establish connection
	 TOTAL_SEND:	number of data sent by each sensor
	 2		wait 2min more to ensure all sensors finish sending
      */
      if (interval_ctr<(2+TOTAL_SEND+2)) {
        etimer_reset(&periodic);
      } else {
	finish = 1;
      }
    }
  }

/* wait 30s to ensure non-root nodes has finished printing out */
etimer_set(&periodic, (30*CLOCK_SECOND));
PROCESS_YIELD();
if(etimer_expired(&periodic)) {
  show_routes();
#if RPL_CONF_STATS
  show_rpl_stats();
#endif
}

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
