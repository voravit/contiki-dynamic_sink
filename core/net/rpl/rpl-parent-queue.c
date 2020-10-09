/*
 * Copyright (c) 2019, KTH Royal Institute of Technology
 * All rights reserved.
 *
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
 */

/**
 * \file
 *         Process for periodic calculation of MAC queue length
 * \Author
 *         Voravit Tanyingyong
 */

//#include "net/rpl/rpl-metric-timer.h"

#include <string.h>

#include "net/mac/csma.c"

#include "contiki-net.h"
#include "net/rpl/rpl-private.h"

#undef DEBUG
#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"

#if SINK_ADDITION || SENSOR_PRINT
#include "net/link-stats.h"
/*---------------------------------------------------------------------------*/
#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF     ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_ICMP_PAYLOAD ((unsigned char *)&uip_buf[uip_l2_l3_icmp_hdr_len])
/*---------------------------------------------------------------------------*/
PROCESS(rpl_parent_queue_process, "RPL traffic metric process");
static uint8_t started = 0;
static int qema = 0;
#define ALPHA 0.7 /* give high weight to the new value */
/*---------------------------------------------------------------------------*/
int
rpl_parent_queue_len(void)
{
  return qema;
}
/*---------------------------------------------------------------------------*/
uint8_t
status_rpl_parent_queue(void)
{
  return started;
}
/*---------------------------------------------------------------------------*/
void
start_rpl_parent_queue(void)
{
  if(started == 0) {
    process_start(&rpl_parent_queue_process, NULL);
    started = 1;
    PRINTF("rpl_parent_queue_process started\n");
  }
}
/*---------------------------------------------------------------------------*/
void
stop_rpl_parent_queue(void)
{
  if(started == 1) {
    process_exit(&rpl_parent_queue_process);
    started = 0;
    PRINTF("rpl_parent_queue_process stoped\n");
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rpl_parent_queue_process, ev, data)
{

  static struct etimer periodic;
  static int ctr = 0;
  static int q_now = 0;
  static rpl_parent_t *last = NULL;

  PROCESS_BEGIN();
  PROCESS_PAUSE();

  etimer_set(&periodic, (1*CLOCK_SECOND));
  while(1) {
    PROCESS_WAIT_EVENT();
    if(etimer_expired(&periodic)) {
      ctr++;
      if (default_instance != NULL) {
	if (default_instance->current_dag->preferred_parent != NULL) {
          q_now = neighbor_queue_length(rpl_get_parent_lladdr(default_instance->current_dag->preferred_parent));
          if (default_instance->current_dag->preferred_parent != last) {
            last = default_instance->current_dag->preferred_parent;
            qema = q_now;
          } else {
            qema = ALPHA*q_now + (1-ALPHA)*qema;
          }
        } else {
	  q_now = 0;
          qema = ALPHA*q_now + (1-ALPHA)*qema;
        }
#if UIP_CONF_STATISTICS
if (default_instance->current_dag->preferred_parent != NULL) {
  printf("QN: %d QNOW: %d QEMA: %d RANK: %u PRN: %02x RX: %u TX: %u FW: %u ETX: %u\n", ctr, q_now, qema, default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], uip_stat.ip.recv, uip_stat.ip.sent, uip_stat.ip.forwarded, rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
} else {
  printf("QN: %d QNOW: %d QEMA: %d RANK: %u PRN: 00 RX: %u TX: %u FW: %u ETX: 000\n", ctr, q_now, qema, default_instance->current_dag->rank, uip_stat.ip.recv, uip_stat.ip.sent, uip_stat.ip.forwarded);
}
#else
printf("QN: %d QNOW: %d QEMA: %d RANK: %u PRN: %02x ETX: %u\n", ctr, q_now, qema, default_instance->current_dag->rank, rpl_get_parent_ipaddr(default_instance->current_dag->preferred_parent)->u8[15], rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx);
#endif

        //printf("parent qema: %d\n", qema);
/*
        if ((ctr%12)==0) {
          printf("qema: %d\n", qema);
        }
*/
      }
      etimer_reset(&periodic);
    }
  }

  PROCESS_END();
}

#endif
/*---------------------------------------------------------------------------*/
