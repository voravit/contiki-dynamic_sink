/*
 * Copyright (c) 2015, SICS Swedish ICT.
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
 *
 *
 * Authors: Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "sys/clock.h"
#include "net/packetbuf.h"
#include "net/nbr-table.h"
#include "net/link-stats.h"
#include <stdio.h>

#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

/* Half time for the freshness counter, in minutes */
#define FRESHNESS_HALF_LIFE             20
/* Statistics are fresh if the freshness counter is FRESHNESS_TARGET or more */
#define FRESHNESS_TARGET                 4
/* Maximum value for the freshness counter */
#define FRESHNESS_MAX                   16
/* Statistics with no update in FRESHNESS_EXPIRATION_TIMEOUT is not fresh */
#define FRESHNESS_EXPIRATION_TIME       (10 * 60 * (clock_time_t)CLOCK_SECOND)

/* EWMA (exponential moving average) used to maintain statistics over time */
#define EWMA_SCALE            100
#define EWMA_ALPHA             15
#define EWMA_BOOTSTRAP_ALPHA   30

/* ETX fixed point divisor. 128 is the value used by RPL (RFC 6551 and RFC 6719) */
#define ETX_DIVISOR     LINK_STATS_ETX_DIVISOR
/* Number of Tx used to update the ETX EWMA in case of no-ACK */
#define ETX_NOACK_PENALTY                   10
/* Initial ETX value */
#define ETX_INIT                             2

/* Per-neighbor link statistics table */
NBR_TABLE(struct link_stats, link_stats);

/* Called every FRESHNESS_HALF_LIFE minutes */
struct ctimer periodic_timer;

/* Used to initialize ETX before any transmission occurs. In order to
 * infer the initial ETX from the RSSI of previously received packets, use: */
/* #define LINK_STATS_CONF_INIT_ETX(stats) guess_etx_from_rssi(stats) */

#ifdef LINK_STATS_CONF_INIT_ETX
#define LINK_STATS_INIT_ETX(stats) LINK_STATS_CONF_INIT_ETX(stats)
#else /* LINK_STATS_INIT_ETX */
#define LINK_STATS_INIT_ETX(stats) (ETX_INIT * ETX_DIVISOR)
#endif /* LINK_STATS_INIT_ETX */

static uint32_t tx_unique;
static uint32_t tx_all;
#if SINK_ADDITION || SENSOR_PRINT
static uint32_t rx_sum;
static uint32_t rx_highest;
static uint32_t rx_highest_ctr;
#endif
/*---------------------------------------------------------------------------*/
/* Returns the neighbor's link stats */
const struct link_stats *
link_stats_from_lladdr(const linkaddr_t *lladdr)
{
  return nbr_table_get_from_lladdr(link_stats, lladdr);
}
/*---------------------------------------------------------------------------*/
/* Are the statistics fresh? */
int
link_stats_is_fresh(const struct link_stats *stats)
{
  return (stats != NULL)
      && clock_time() - stats->last_tx_time < FRESHNESS_EXPIRATION_TIME
      && stats->freshness >= FRESHNESS_TARGET;
}
/*---------------------------------------------------------------------------*/
uint16_t
guess_etx_from_rssi(const struct link_stats *stats)
{
  if(stats != NULL) {
    if(stats->rssi == 0) {
      return ETX_INIT * ETX_DIVISOR;
    } else {
      /* A rough estimate of PRR from RSSI, as a linear function where:
       *      RSSI >= -60 results in PRR of 1
       *      RSSI <= -90 results in PRR of 0
       * prr = (bounded_rssi - RSSI_LOW) / (RSSI_DIFF)
       * etx = ETX_DIVOSOR / ((bounded_rssi - RSSI_LOW) / RSSI_DIFF)
       * etx = (RSSI_DIFF * ETX_DIVOSOR) / (bounded_rssi - RSSI_LOW)
       * */
#define ETX_INIT_MAX 3
#define RSSI_HIGH -60
#define RSSI_LOW  -90
#define RSSI_DIFF (RSSI_HIGH - RSSI_LOW)
      uint16_t etx;
      int16_t bounded_rssi = stats->rssi;
      bounded_rssi = MIN(bounded_rssi, RSSI_HIGH);
      bounded_rssi = MAX(bounded_rssi, RSSI_LOW + 1);
      etx = RSSI_DIFF * ETX_DIVISOR / (bounded_rssi - RSSI_LOW);
      return MIN(etx, ETX_INIT_MAX * ETX_DIVISOR);
    }
  }
  return 0xffff;
}
/*---------------------------------------------------------------------------*/
/* Packet sent callback. Updates stats for transmissions to lladdr */
void
link_stats_packet_sent(const linkaddr_t *lladdr, int status, int numtx)
{
  struct link_stats *stats;
  uint16_t packet_etx;
  uint8_t ewma_alpha;

  stats = nbr_table_get_from_lladdr(link_stats, lladdr);
  if(stats == NULL) {
    /* Add the neighbor */
    stats = nbr_table_add_lladdr(link_stats, lladdr, NBR_TABLE_REASON_LINK_STATS, NULL);
    if(stats != NULL) {
      stats->etx = LINK_STATS_INIT_ETX(stats);
    } else {
      return; /* No space left, return */
    }
  }

  /* Update total count of TX packets, and total TX send+resend attempts */
  stats->tx_tot_cnt++;
  stats->tx_num_sum += numtx;
  tx_unique++;
  tx_all += numtx;

  if(status != MAC_TX_OK && status != MAC_TX_NOACK) {
    /* Do not penalize the ETX when collisions or transmission errors occur. */
    if(status == MAC_TX_COLLISION)
      stats->tx_collision++;
    if(status == MAC_TX_DEFERRED)
      stats->tx_deferred++;
    if(status == MAC_TX_ERR || status == MAC_TX_ERR_FATAL)
      stats->tx_error++;
    return;
  }

  /* Update last timestamp and freshness */
  stats->last_tx_time = clock_time();
  stats->freshness = MIN(stats->freshness + numtx, FRESHNESS_MAX);

  /* Update successful TX count */
  if (status == MAC_TX_OK)
      stats->tx_ok_cnt++;
  if (status == MAC_TX_NOACK)
      stats->tx_noack++; 

  /* ETX used for this update */
  packet_etx = ((status == MAC_TX_NOACK) ? ETX_NOACK_PENALTY : numtx) * ETX_DIVISOR;
  /* ETX alpha used for this update */
  ewma_alpha = link_stats_is_fresh(stats) ? EWMA_ALPHA : EWMA_BOOTSTRAP_ALPHA;

  /* Compute EWMA and update ETX */
  stats->etx = ((uint32_t)stats->etx * (EWMA_SCALE - ewma_alpha) +
      (uint32_t)packet_etx * ewma_alpha) / EWMA_SCALE;
}
/*---------------------------------------------------------------------------*/
/* Packet input callback. Updates statistics for receptions on a given link */
void
link_stats_input_callback(const linkaddr_t *lladdr)
{
  struct link_stats *stats;
  int16_t packet_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  stats = nbr_table_get_from_lladdr(link_stats, lladdr);
  if(stats == NULL) {
    /* Add the neighbor */
    stats = nbr_table_add_lladdr(link_stats, lladdr, NBR_TABLE_REASON_LINK_STATS, NULL);
    if(stats != NULL) {
      /* Initialize */
      stats->rssi = packet_rssi;
      stats->etx = LINK_STATS_INIT_ETX(stats);
      stats->rx_bytes += packetbuf_totlen();
      stats->rx++;
    }
    return;
  }

  /* Update RSSI EWMA */
  stats->rssi = ((int32_t)stats->rssi * (EWMA_SCALE - EWMA_ALPHA) +
      (int32_t)packet_rssi * EWMA_ALPHA) / EWMA_SCALE;
  stats->rx_bytes += packetbuf_totlen();
  stats->rx++;
}
/*---------------------------------------------------------------------------*/
/* Periodic timer called every FRESHNESS_HALF_LIFE minutes */
static void
periodic(void *ptr)
{
  /* Age (by halving) freshness counter of all neighbors */
  struct link_stats *stats;
  ctimer_reset(&periodic_timer);
  for(stats = nbr_table_head(link_stats); stats != NULL; stats = nbr_table_next(link_stats, stats)) {
    stats->freshness >>= 1;
  }
}
/*---------------------------------------------------------------------------*/
/* Initializes link-stats module */
void
link_stats_init(void)
{
  nbr_table_register(link_stats, NULL);
  ctimer_set(&periodic_timer, 60 * (clock_time_t)CLOCK_SECOND * FRESHNESS_HALF_LIFE,
      periodic, NULL);
}
/*---------------------------------------------------------------------------*/
void
print_link_stats(void)
{
  struct link_stats *stats;
  linkaddr_t *lladdr;
  linkaddr_t local_lladdr = {{0}};
  uint32_t sum_rx = 0;
  uint32_t sum_rxb = 0;
  
  for(stats = nbr_table_head(link_stats); stats != NULL; stats = nbr_table_next(link_stats, stats)) {
    //void *key = key_from_item(link_stats, stats);
    lladdr = nbr_table_get_lladdr(link_stats, stats);
    if (lladdr == NULL) { 
      //printf("LLADDR: NULL\n");
    } else if (linkaddr_cmp(lladdr, &local_lladdr)) {
      //printf("LLADDR: LOCAL\n");
      //printf("0000 ");
      printf("00 ");
      printf("cnt: %lu sum: %lu ok: %lu col: %lu noack: %lu defer: %lu err: %lu rxb: %lu rx: %lu rssi: %u etx: %u\n",
        stats->tx_tot_cnt, stats->tx_num_sum, stats->tx_ok_cnt, stats->tx_collision,
        stats->tx_noack, stats->tx_deferred, stats->tx_error, stats->rx_bytes, stats->rx, stats->rssi, stats->etx);
      sum_rx+=stats->rx;
      sum_rxb+=stats->rx_bytes;
    } else {
      uip_lladdr_t *addr = (uip_lladdr_t *) lladdr;
/*
      unsigned int i;
      for(i = 0; i < LINKADDR_SIZE; i++) {
        if(i > 0) {
          printf(":");
        }
        printf("%02x", addr->addr[i]);
      }
      printf("\n");
*/
//      printf("%x%02x ", addr->addr[LINKADDR_SIZE-2], addr->addr[LINKADDR_SIZE-1]);
      printf("%02x ", addr->addr[LINKADDR_SIZE-1]);
      printf("cnt: %lu sum: %lu ok: %lu col: %lu noack: %lu defer: %lu err: %lu rxb: %lu rx: %lu rssi: %d etx: %u\n",
        stats->tx_tot_cnt, stats->tx_num_sum, stats->tx_ok_cnt, stats->tx_collision,
        stats->tx_noack, stats->tx_deferred, stats->tx_error, stats->rx_bytes, stats->rx, stats->rssi, stats->etx);
      sum_rx+=stats->rx;
      sum_rxb+=stats->rx_bytes;
    }
  }
  printf("tx_unique: %lu tx_all: %lu tx_rexmit: %lu rx: %lu rxb: %lu\n", tx_unique, tx_all, tx_all-tx_unique, sum_rx, sum_rxb);
}
/*---------------------------------------------------------------------------*/
#if SINK_ADDITION || SENSOR_PRINT
void
calculate_traffic_metric(void)
{
  struct link_stats *stats;
  linkaddr_t *lladdr;
  linkaddr_t local_lladdr = {{0}};
  uint32_t highest_rx = 0;
  uint32_t sum_rx = 0;
  uint32_t highest_rx_ctr = 0;

  for(stats = nbr_table_head(link_stats); stats != NULL; stats = nbr_table_next(link_stats, stats)) {
    lladdr = nbr_table_get_lladdr(link_stats, stats);
    if (lladdr == NULL) {
      printf("LLADDR: NULL\n");
    } else if (linkaddr_cmp(lladdr, &local_lladdr)) {
      printf("LLADDR: LOCAL\n");
    } else {
/*
      uip_lladdr_t *addr = (uip_lladdr_t *) lladdr;
      printf("%x%02x ", addr->addr[LINKADDR_SIZE-2], addr->addr[LINKADDR_SIZE-1]);
*/
      if (highest_rx < (stats->rx_bytes-stats->rx_bytes_last)) {
        highest_rx = stats->rx_bytes-stats->rx_bytes_last;
      }	
      sum_rx += (stats->rx_bytes-stats->rx_bytes_last);
      stats->rx_bytes_last = stats->rx_bytes;

      if (highest_rx_ctr < (stats->rx-stats->rx_last)) {
        highest_rx_ctr = stats->rx-stats->rx_last;
      }	
      stats->rx_last = stats->rx;
      	
    }
  }
  rx_sum = sum_rx;
  rx_highest = highest_rx;
  rx_highest_ctr = highest_rx_ctr;
/*
  if (default_instance != NULL) {
    default_instance->received_traffic = sum_rx;
    default_instance->highest_traffic = highest_rx;
    printf("received_traffic: %lu highest_traffic: %lu\n", sum_rx, highest_rx);
  }
*/
}
/*---------------------------------------------------------------------------*/
uint32_t
get_received_traffic(void)
{
  return rx_sum;
}
/*---------------------------------------------------------------------------*/
uint32_t
get_highest_traffic(void)
{
  return rx_highest;
}
/*---------------------------------------------------------------------------*/
uint32_t
get_rx_highest(void)
{
  return rx_highest_ctr;
}
/*---------------------------------------------------------------------------*/
uint32_t
get_tx_unique(void)
{
  return tx_unique;
}
/*---------------------------------------------------------------------------*/
uint32_t
get_tx_all(void)
{
  return tx_all;
}
/*---------------------------------------------------------------------------*/
uint32_t
get_tx_rexmit(void)
{
  return tx_all-tx_unique;
}
#endif
/*---------------------------------------------------------------------------*/
