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
 *         Process for periodic calculation of traffic metric
 * \Author
 *         Voravit Tanyingyong
 */

//#include "net/rpl/rpl-metric-timer.h"
#include <string.h>
#include "contiki-net.h"
#include "net/rpl/rpl-private.h"

#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"

#if SINK_ADDITION
#include "dev/slip.h"
#include "net/rpl/rpl.h"
#include "net/link-stats.h"
#endif
/*---------------------------------------------------------------------------*/
#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF     ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_ICMP_PAYLOAD ((unsigned char *)&uip_buf[uip_l2_l3_icmp_hdr_len])
/*---------------------------------------------------------------------------*/
PROCESS(rpl_metric_timer_process, "RPL traffic metric process");
static uint8_t started = 0;
/*---------------------------------------------------------------------------*/
uint8_t
status_rpl_metric_timer(void)
{
  return started;
}
/*---------------------------------------------------------------------------*/
void
start_rpl_metric_timer(void)
{
  if(started == 0) {
    process_start(&rpl_metric_timer_process, NULL);
    started = 1;
    PRINTF("rpl_metric_timer_process started\n");
  }
}
/*---------------------------------------------------------------------------*/
void
stop_rpl_metric_timer(void)
{
  if(started == 1) {
    process_exit(&rpl_metric_timer_process);
    started = 0;
    PRINTF("rpl_metric_timer_process stoped\n");
  }
}
/*---------------------------------------------------------------------------*/
#if SINK_ADDITION
static int filled = 0;
#endif
#if (SINK_ADDITION >= 2)
static int rxh_filled = 0;
void
reset_filled(void)
{
  filled = 0;
  rxh_filled = 0;
}
#endif
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rpl_metric_timer_process, ev, data)
{

  static struct etimer periodic;
#if SINK_ADDITION
#define ARRAY_LEN 5
#define ARR_RANK_TH 128
#define ARR_ETX_TH 128
#define ARR_RX_TH 150
#define ARR_RX_TH_DEACT 15
  static uint8_t count=0;
  static rpl_rank_t arr_rank[ARRAY_LEN] = { 0 };
  static uint16_t arr_etx[ARRAY_LEN] = { 0 };
  static uint16_t arr_rx[ARRAY_LEN] = { 0 };
  static uip_stats_t last_recv = 0;
  //static rpl_rank_t last_sensor_rank = 0;
  //static uint16_t last_sensor_etx = 0;
  int i;
#endif /* SINK_ADDITION */
#if (SINK_ADDITION == 1)
#if !SINK_FIXED
  //static uip_ipaddr_t *my_addr;
  static rpl_rank_t last_sensor_rank = 0;
  static uip_ds6_defrt_t *defrt=NULL;
  rpl_dag_t *dag;
#endif
#endif /* SINK_ADDITION == 1 */
#if (SINK_ADDITION >= 2)
#define ARR_RXH_TH 50
  static int arr_rxh[ARRAY_LEN] = { 0 };
  char payload[128];
  unsigned char *buffer;
#endif  

  PROCESS_BEGIN();
  PROCESS_PAUSE();

  etimer_set(&periodic, (60*CLOCK_SECOND));
  while(1) {
    PROCESS_WAIT_EVENT();
    if(etimer_expired(&periodic)) {
/*---------------------------------------------------------------------------*/
#if SINK_ADDITION
      count++;
      rpl_calculate_traffic_metric();
      int arr_rank_over = 0;
      int arr_etx_over = 0;
      int arr_rx_over = 0;
      int arr_rx_under = 0;
      uip_stats_t curr_recv = uip_stat.ip.recv;
      if (default_instance != NULL) {
        if ((count>1) && (filled < ARRAY_LEN)) {
          //arr_rank[filled] = (int) (default_instance->current_dag->rank - last_sensor_rank);
          arr_rank[filled] = default_instance->current_dag->rank;
          if (default_instance->current_dag->preferred_parent != NULL) {
            arr_etx[filled] = rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx;
	  } else {
            arr_etx[filled] = 128;
	  }
          arr_rx[filled] = (curr_recv - last_recv);
          filled++;
        }
        if (filled >= ARRAY_LEN) {
          for (i=0; i < ARRAY_LEN-1; i++) {
            arr_rank[i] = arr_rank[i+1];
            arr_etx[i] = arr_etx[i+1];
            arr_rx[i] = arr_rx[i+1];
          }
          arr_rank[ARRAY_LEN-1] = default_instance->current_dag->rank;
          if (default_instance->current_dag->preferred_parent != NULL) {
            arr_etx[ARRAY_LEN-1] = rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx;
	  } else {
            arr_etx[ARRAY_LEN-1] = 128;
	  }
          arr_rx[ARRAY_LEN-1] = (curr_recv - last_recv);

          for (i=1; i < ARRAY_LEN; i++) { if ((int)(arr_rank[i] - arr_rank[0]) >= (int) ARR_RANK_TH) { arr_rank_over++; } }

          for (i=1; i < ARRAY_LEN; i++) { if ((int)(arr_etx[i] - arr_etx[0]) >= (int) ARR_ETX_TH) { arr_etx_over++; } }

          for (i=0; i < ARRAY_LEN; i++) { if (arr_rx[i] >= ARR_RX_TH) { arr_rx_over++; } }
          for (i=0; i < ARRAY_LEN; i++) { if (arr_rx[i] <= ARR_RX_TH_DEACT) { arr_rx_under++; } }

        }
        //last_sensor_rank = default_instance->current_dag->rank;
        //last_sensor_etx = rpl_get_parent_link_stats(default_instance->current_dag->preferred_parent)->etx;
        last_recv = curr_recv;

        printf("%d ARR_RANK: TH: %d A: %u %u %u %u %u OVER: %d\n", count, ARR_RANK_TH, arr_rank[0], arr_rank[1], arr_rank[2], arr_rank[3], arr_rank[4], arr_rank_over);
        printf("%d ARR_ETX: TH: %d A: %u %u %u %u %u OVER: %d\n", count, ARR_ETX_TH, arr_etx[0], arr_etx[1], arr_etx[2], arr_etx[3], arr_etx[4], arr_etx_over);
        printf("%d ARR_RX: TH: %d DTH: %d A: %d %d %d %d %d OVER: %d UNDER: %d\n", count, ARR_RX_TH, ARR_RX_TH_DEACT, arr_rx[0], arr_rx[1], arr_rx[2], arr_rx[3], arr_rx[4], arr_rx_over, arr_rx_under);

      }
#endif
/*---------------------------------------------------------------------------*/
#if (SINK_ADDITION >= 2)
      /* highest RX load on one of the neighbor */
      int arr_rxh_over = 0;
      if (default_instance != NULL) {
        if (rxh_filled <= ARRAY_LEN) {
          arr_rxh[filled-1] = get_rx_highest();
        }
        if (rxh_filled > ARRAY_LEN) {
          for (i=0; i < ARRAY_LEN-1; i++) {
            arr_rxh[i] = arr_rxh[i+1];
          }
          arr_rxh[ARRAY_LEN-1] = get_rx_highest();

          for (i=0; i < ARRAY_LEN; i++) { if (arr_rxh[i] >= ARR_RXH_TH) { arr_rxh_over++; } }
        }
        printf("%d ARR_RXH: TH: %d A: %d %d %d %d %d OVER: %d\n", count, ARR_RXH_TH, arr_rxh[0], arr_rxh[1], arr_rxh[2], arr_rxh[3], arr_rxh[4], arr_rxh_over);
      }
#endif
/*---------------------------------------------------------------------------*/
#if (SINK_ADDITION == 3) /* COORDINATED: send report to the coordinator */
      if (get_operate_mode() > OPERATE_AS_SENSOR) {
        if (default_instance != NULL) {
          uint16_t tmp16;
          uint32_t tmp32;

          payload[0] = 0x90;
          payload[1] = 0x00;

          memcpy(&payload[2], &default_instance->tree_size, 1); 
          memcpy(&payload[3], &default_instance->longest_hop, 1); 
	  tmp16 = uip_htons(last_recv);
          memcpy(&payload[4], &tmp16, 2); 
	  tmp16 = uip_htons((uint16_t)get_rx_highest());
          memcpy(&payload[6], &tmp16, 2); 
	  tmp32 = uip_htonl(default_instance->energy);
          memcpy(&payload[8], &tmp32, 4); 
          tmp32 = uip_htonl(slip_get_input_bytes());
          memcpy(&payload[12], &tmp32, 4); 
          tmp32 = uip_htonl(slip_get_output_bytes());
          memcpy(&payload[16], &tmp32, 4); 

          uip_clear_buf();
          buffer = UIP_ICMP_PAYLOAD;
          memcpy(buffer, &payload, 20);

#if RPL_CONF_STATS
  RPL_STAT(rpl_stats.dis_ext_out++);
#endif /* RPL_CONF_STATS */
          uip_icmp6_send_src(rpl_get_src_addr(), get_coordinator_addr(), ICMP6_RPL, RPL_CODE_DIS, 20);
        }
      }
#endif /* SINK_ADDITION == 3 */
/*---------------------------------------------------------------------------*/
#if (SINK_ADDITION == 2) /* UNCOORDINATED: find a candidate sink  and send activation */
      uip_ipaddr_t *target_node = NULL;
      if (default_instance != NULL) {
        /* we ignore the first counter since it tends to be over the threshold */
	if ((filled >= ARRAY_LEN) && (arr_rx_over >= 3)) {
	//if ((filled >= ARRAY_LEN) && (arr_rxh_over >= 3)) {
	  /* find sink to activate */
          target_node = activate_sink();

          if (target_node != NULL) {
	    printf("ACTIVATE: %02x%02x\n", target_node->u8[14], target_node->u8[15]);
	    /* send activation */
            //my_addr = rpl_get_src_addr();
            payload[0] = 0xA0;
            payload[1] = 0x00;

            uip_clear_buf();
            buffer = UIP_ICMP_PAYLOAD;
            memcpy(buffer,&payload,2);
#if RPL_CONF_STATS
  RPL_STAT(rpl_stats.dis_ext_out++);
#endif /* RPL_CONF_STATS */
            uip_icmp6_send_src(rpl_get_src_addr(), target_node, ICMP6_RPL, RPL_CODE_DIS, 2);
            //rxh_filled = 0;
          } else {
            printf("NO SINK TO ACTIVATE\n");
          }
        } else {
          if ((filled >= ARRAY_LEN) && (arr_rx_under >= 3)) {
          //if (default_instance->received_traffic < (SINK_METRIC_RX_TRAFFIC*SINK_METRIC_THRESHOLD)) {
	    /* find sink to deactivate */
            target_node = deactivate_sink();

            if (target_node != NULL) {
	      printf("DEACTIVATE: %02x%02x\n", target_node->u8[14], target_node->u8[15]);
	      /* send deactivation */
              //my_addr = rpl_get_src_addr();
              payload[0] = 0xB0;
              payload[1] = 0x00;

              uip_clear_buf();
              buffer = UIP_ICMP_PAYLOAD;
              memcpy(buffer,&payload,2);
#if RPL_CONF_STATS
  RPL_STAT(rpl_stats.dis_ext_out++);
#endif /* RPL_CONF_STATS */
              uip_icmp6_send_src(rpl_get_src_addr(), target_node, ICMP6_RPL, RPL_CODE_DIS, 2);
            } else {
              printf("NO SINK TO DEACTIVATE\n");
            }
          }
          //rxh_filled = 0;
        }
/*
	// Enhancement: activate a sink located far from active sink	
        target_node = activate_high_rank_sink();
        if (target_node != NULL) {
	  printf("ACTIVATE: %02x%02x\n", target_node->u8[14], target_node->u8[15]);
          //my_addr = rpl_get_src_addr();
          payload[0] = 0xA0;
          payload[1] = 0x00;

          uip_clear_buf();
          buffer = UIP_ICMP_PAYLOAD;
          memcpy(buffer,&payload,2);
#if RPL_CONF_STATS
  RPL_STAT(rpl_stats.dis_ext_out++);
#endif // RPL_CONF_STATS 
          uip_icmp6_send_src(rpl_get_src_addr(), target_node, ICMP6_RPL, RPL_CODE_DIS, 2);
        }
*/
      } /* default_instance != NULL */
#endif /* SINK_ADDITION == 2 */
/*---------------------------------------------------------------------------*/
#if (SINK_ADDITION == 1) /* AUTONOMOUS: activate sink functionalities */
#if !SINK_FIXED
      //my_addr = rpl_get_src_addr();
//if ((count == (3+10)) && ((my_addr->u8[15]) <= 1)) {
if ((filled >= ARRAY_LEN) && (arr_rank_over >= 3)) {
//if ((filled >= ARRAY_LEN) && (arr_etx_over >= 3)) {
//if ((filled >= ARRAY_LEN) && (arr_rx_over >= 3)) {
      if (get_operate_mode() == OPERATE_AS_SENSOR) {
        if (default_instance != NULL) {
printf("AUTO ACTIVATION ETX: %u\n", default_instance->current_dag->rank);
	    /* activate sink functionalities */
            dao_output(default_instance->current_dag->preferred_parent, RPL_ZERO_LIFETIME);
            defrt = uip_ds6_defrt_lookup(uip_ds6_defrt_choose());
            if (defrt != NULL) {
              uip_ds6_defrt_rm(defrt);
            }
            printf("%lu ", clock_time());
            printf("ROUTE ADD: ");
            uip_debug_ipaddr_print(rpl_get_src_addr());
            printf("\n");
#ifdef ROOT_VIRTUAL
            uip_ds6_addr_add(get_vr_addr(), 0, ADDR_MANUAL);
#endif
            last_sensor_rank = default_instance->current_dag->rank;
            dag = rpl_set_root(RPL_DEFAULT_INSTANCE, get_vr_addr());
            if(dag != NULL) {
              rpl_set_prefix(dag, get_vr_addr(), 64);
              PRINTF("created a new RPL dag\n");
            }
            set_operate_mode(SINK_ADDITION);
            printf("operate mode: %d\n", get_operate_mode());
            NETSTACK_MAC.off(1);
	} /* default_instance != NULL */
        filled = 0;
      }
}
//if ((count == (3+10+10+1)) && ((my_addr->u8[15]) <= 1)) {
if ((filled >= ARRAY_LEN) && (arr_rx_under >= 3)) {
        /* OPERATE_AS_SINK */
      if (get_operate_mode() > OPERATE_AS_SENSOR) {
        if (default_instance != NULL) {
printf("AUTO DEACTIVATION ETX: %u\n", default_instance->current_dag->rank);
	    /* deactivate sink functionalities */
            default_instance->current_dag->min_rank = last_sensor_rank + RPL_MIN_HOPRANKINC;
            default_instance->current_dag->rank = last_sensor_rank + RPL_MIN_HOPRANKINC;
            rpl_remove_routes(default_instance->current_dag);
            rpl_recalculate_ranks();
            dio_output(default_instance, NULL);

            printf("%lu ", clock_time());
            printf("ROUTE REMOVE: ");
            uip_debug_ipaddr_print(rpl_get_src_addr());
            printf("\n");
  
#ifdef ROOT_VIRTUAL
            uip_ds6_addr_rm(uip_ds6_addr_lookup(get_vr_addr()));
#endif
            rpl_reset_periodic_timer();
            set_operate_mode(OPERATE_AS_SENSOR);
            printf("operate mode: %d\n", get_operate_mode());
            NETSTACK_MAC.on();
        }
        filled = 0;
      }
}
#endif /* !SINK_FIXED */
#endif /* SINK_ADDITION == 1 */
/*---------------------------------------------------------------------------*/
#if SENSOR_PRINT
      rpl_calculate_traffic_metric();
#endif
      etimer_reset(&periodic);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/