/*
  lbmmoncache.c: example LBM monitoring application.

  Copyright (c) 2005-2014 Informatica Corporation  Permission is granted to licensees to use
  or alter this software for any purpose, including commercial applications,
  according to the terms laid out in the Software License Agreement.

  This source code example is provided by Informatica for educational
  and evaluation purposes only.

  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES 
  EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF 
  NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR 
  PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE 
  UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES, BE 
  LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR 
  INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE 
  TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF 
  THE LIKELIHOOD OF SUCH DAMAGES.

  This application monitors both source notifications and source/receiver statistics.
  As notifications and statistics arrive, they are pooled in to a single cache.
  A menu allows interrigation of the cache for data.

  The cache times out entries and has a maximum size set - see defines below.

  Since there are no notifications for when sources go inactive or are deleted,
  this tool only presents data about *active* sources. To help detect sources
  being deleted or going inactive, the context for source notifications is
  periodically deleted and recreated - active sources will refresh the cache
  as fresh source notifications arrive.

  Long term, seperate caches should be used for scalability, but this version
  uses one big cache.
*/

/* Recreate the context every 5 minutes */
#define DEFAULT_CTX_TIMEOUT 300

/* Age out entries after 10 minutes - should be larger than context timeout to enable refresh */
#define DEFAULT_AGE_TIMEOUT 600

/* Max entries in the cache */
#define MONCACHE_DEFAULT_SIZE 10000
int moncache_default_size = MONCACHE_DEFAULT_SIZE;

/* Standard system defines */
#ifdef __VOS__
#define _POSIX_C_SOURCE 200112L 
#include <sys/time.h>
#endif
#if defined(__TANDEM) && defined(HAVE_TANDEM_SPT)
	#include <ktdmtyp.h>
	#include <spthread.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <sys/timeb.h>
	#define strcasecmp stricmp
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <signal.h>
	#if defined(__TANDEM)
		#include <strings.h>
		#if defined(HAVE_TANDEM_SPT)
			#include <spthread.h>
		#else
			#include <pthread.h>
		#endif
	#else
		#include <pthread.h>
	#endif
#endif
#include "replgetopt.h"
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "monmodopts.h"
#include "lbm-example-util.h"

static const char *rcsid_example_lbmmoncache = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmoncache.c#2 $";

#if defined(_WIN32)
#   define SLEEP_SEC(x) Sleep((x)*1000)
#   define SLEEP_MSEC(x) Sleep(x)
#else
#   define SLEEP_SEC(x) sleep(x)
#   define SLEEP_MSEC(x) \
		do{ \
			if ((x) >= 1000){ \
				sleep((x) / 1000); \
				usleep((x) % 1000 * 1000); \
			} \
			else{ \
				usleep((x)*1000); \
			} \
		}while (0)
#endif /* _WIN32 */

/* Generic locking helpers */

#if defined(_WIN32)
typedef CRITICAL_SECTION mutex_t;
#define MUTEX_INIT(m) InitializeCriticalSection(&m)
#define MUTEX_LOCK(m) EnterCriticalSection(&m)
#define MUTEX_UNLOCK(m) LeaveCriticalSection(&m)
#define MUTEX_DELETE(m) DeleteCriticalSection(&m)
#else
typedef pthread_mutex_t mutex_t;
#define MUTEX_INIT(m) pthread_mutex_init(&m,NULL);
#define MUTEX_LOCK(m) pthread_mutex_lock(&m);
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&m);
#define MUTEX_DELETE(m) pthread_mutex_destroy(&m);
#endif

const char Purpose[] = "Purpose: Example LBM statistics monitoring application.";
const char Usage[] =
"Usage: %s [options]\n"
"Available options:\n"
"  -c, --config=FILE          Use LBM configuration file FILE.\n"
"                             Multiple config files are allowed.\n"
"                             Example:  '-c file1.cfg -c file2.cfg'\n"
"  -C, --cache-size=size      Set the cache size to 'size' entries\n"
"  -h, --help                 display this help and exit\n"
"  -t, --transport=TRANS      use transport module TRANS\n"
"                             TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
"      --transport-opts=OPTS  use OPTS as transport module options\n"
"  -f, --format=FMT           use format module FMT\n"
"                             FMT may be `csv'\n"
"      --format-opts=OPTS     use OPTS as format module options\n"
MONMODULEOPTS_RECEIVER;

const char * OptionString = "C:c:f:ht:";
const struct option OptionTable[] =
{
	{ "config", required_argument, NULL, 'c' },
	{ "cache-size", required_argument, NULL, 'C' },
	{ "help", no_argument, NULL, 'h' },
	{ "transport", required_argument, NULL, 't' },
	{ "transport-opts", required_argument, NULL, 0 },
	{ "format", required_argument, NULL, 'f' },
	{ "format-opts", required_argument, NULL, 1 },
	{ NULL, 0, NULL, 0 }
};
int log_callback(int Level, const char * Message, void * ClientData);

#define RCV_STATS 1
#define SRC_STATS 2
#define EVQ_STATS 3 /* Not currently used */
#define CTX_STATS 4 /* Not currently used */
#define SRC_NOTIF 5

/* Structures used in caching */
typedef struct moncache_source_notif_stct {
	char topic[256];
	char source[256];
} moncache_source_notif_t;

union lbm_stats_structs {
	lbm_rcv_transport_stats_t rcv;
	lbm_src_transport_stats_t src;
	lbm_event_queue_stats_t evq;
	lbm_context_stats_t ctx;
	moncache_source_notif_t srcnotif;
};

struct cache_stat_elem_t;
struct cache_stat_elem_t {
	struct cache_stat_elem_t *nxt;
	struct cache_stat_elem_t *prv;
	char stat_type;
	struct timeval lasttv;
	union lbm_stats_structs stats;
	struct in_addr addr;
	char addr_str[24];
	char appid[256];
	time_t timestamp;
	unsigned long entry_hash;
	char *protocol;
	char *srcip;
	char *srcport;
	char *transport;
	char *destip;
	char *destport;
};
typedef struct cache_stat_elem_t cache_stat;

typedef struct moncache_stct {
	mutex_t lock;
	struct cache_stat_elem_t *head,*tail;
	int max_cache_size,curr_cache_size,curr_cache_ivl_added;
} moncache_t;

/* The great big cache and settings for controlling periodic output */
moncache_t *globalmoncache;
int dump_cache_stat;
int dump_verbose;
int dump_src_notifs;
int waitingforinput;
char saved_searchterm[128];


/* Prototypes for cache functions */
moncache_t *moncache_new();
void free_cache_elem(struct cache_stat_elem_t *oldelem);
int get_curr_cache_size(moncache_t *);
int add_stat_to_cache(moncache_t *, const void * AttributeBlock, const union lbm_stats_structs *stats, int stat_type);
int get_curr_ivl_cache_added(moncache_t *);
void reset_cache_ivl(moncache_t *);
void trim_cache_to_size(moncache_t *);
void cache_calc_hash(moncache_t *cache,struct cache_stat_elem_t *new_elem);
struct cache_stat_elem_t *cache_search_dup(moncache_t *cache,struct cache_stat_elem_t *new_elem);
void cache_calc_hashes(moncache_t *cache,struct cache_stat_elem_t *new_elem);
void ageout_cache(moncache_t *cache);
void lock_cache(moncache_t *);
void unlock_cache(moncache_t *);

void dump_srcs_by_substr(char *);
void dump_srcs_by_srcip(char *srcip);
void dump_srcs_by_destip(char *srcip);
void dump_srcs_by_rcvrip(char *rcvrip);
void dump_stats_by_substr(char *);
void dump_srctopics(char *topic);
void dump_rcvrsrcs();

/* Source Notification Context related data and functions */
lbm_context_t *sctx;
lbm_context_attr_t *cattr;
int ctx_timeout = DEFAULT_CTX_TIMEOUT;
void moncache_create_ctx();

void
dump_rcv_statistics(struct cache_stat_elem_t *cache_stat)
{
	lbm_rcv_transport_stats_t * Statistics = &cache_stat->stats.rcv;
#if 0
	struct in_addr addr;
	char appid[256];
	time_t timestamp;
	char * time_string;
	lbm_ulong_t source = LBMMON_ATTR_SOURCE_NORMAL;
	lbm_ulong_t objectid = 0;
	lbm_ulong_t processid = 0;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}
	switch (source)
	{
		case LBMMON_ATTR_SOURCE_IM:
			printf("\nContext IM receiver statistics received");
			break;
		default:
			printf("\nReceiver statistics received");
			break;
	}
	if (lbmmon_attr_get_appsourceid(AttributeBlock, appid, sizeof(appid)) == 0)
	{
		printf(" from %s", appid);
	}
	if (lbmmon_attr_get_ipv4sender(AttributeBlock, (lbm_uint_t *) &(addr.s_addr)) == 0)
	{
		printf(" at %s", inet_ntoa(addr));
	}
	if (lbmmon_attr_get_processid(AttributeBlock, &(processid)) == 0)
	{
		printf(", process ID=%0lx", processid);
	}
	if (lbmmon_attr_get_objectid(AttributeBlock, &(objectid)) == 0)
	{
		printf(", object ID=%0lx", objectid);
	}
	if (lbmmon_attr_get_timestamp(AttributeBlock, &timestamp) == 0)
	{
		time_string = ctime(&timestamp);
		printf(", sent %s", time_string);
		/* Reminder: ctime() returns a string terminated with a newline */
	}
	else
	{
		printf("\n");
	}
	printf("Source: %s\n", Statistics->source);
	printf("Transport: %s\n", translate_transport(Statistics->type));
#endif
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			printf("\tLBT-TCP bytes received                                    : %lu\n", Statistics->transport.tcp.bytes_rcved);
			printf("\tLBM messages received                                     : %lu\n", Statistics->transport.tcp.lbm_msgs_rcved);
			printf("\tLBM messages received with uninteresting topic            : %lu\n", Statistics->transport.tcp.lbm_msgs_no_topic_rcved);
			printf("\tLBM requests received                                     : %lu\n", Statistics->transport.tcp.lbm_reqs_rcved);
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			printf("\tLBT-RM datagrams received                                 : %lu\n", Statistics->transport.lbtrm.msgs_rcved);
			printf("\tLBT-RM datagram bytes received                            : %lu\n", Statistics->transport.lbtrm.bytes_rcved);
			printf("\tLBT-RM NAK packets sent                                   : %lu\n", Statistics->transport.lbtrm.nak_pckts_sent);
			printf("\tLBT-RM NAKs sent                                          : %lu\n", Statistics->transport.lbtrm.naks_sent);
			printf("\tLost LBT-RM datagrams detected                            : %lu\n", Statistics->transport.lbtrm.lost);
			printf("\tNCFs received (ignored)                                   : %lu\n", Statistics->transport.lbtrm.ncfs_ignored);
			printf("\tNCFs received (shed)                                      : %lu\n", Statistics->transport.lbtrm.ncfs_shed);
			printf("\tNCFs received (retransmit delay)                          : %lu\n", Statistics->transport.lbtrm.ncfs_rx_delay);
			printf("\tNCFs received (unknown)                                   : %lu\n", Statistics->transport.lbtrm.ncfs_unknown);
			printf("\tLoss recovery minimum time                                : %lums\n", Statistics->transport.lbtrm.nak_stm_min);
			printf("\tLoss recovery mean time                                   : %lums\n", Statistics->transport.lbtrm.nak_stm_mean);
			printf("\tLoss recovery maximum time                                : %lums\n", Statistics->transport.lbtrm.nak_stm_max);
			printf("\tMinimum transmissions per individual NAK                  : %lu\n", Statistics->transport.lbtrm.nak_tx_min);
			printf("\tMean transmissions per individual NAK                     : %lu\n", Statistics->transport.lbtrm.nak_tx_mean);
			printf("\tMaximum transmissions per individual NAK                  : %lu\n", Statistics->transport.lbtrm.nak_tx_max);
			printf("\tDuplicate LBT-RM datagrams received                       : %lu\n", Statistics->transport.lbtrm.duplicate_data);
			printf("\tLBT-RM datagrams unrecoverable (window advance)           : %lu\n", Statistics->transport.lbtrm.unrecovered_txw);
			printf("\tLBT-RM datagrams unrecoverable (NAK generation expiration): %lu\n", Statistics->transport.lbtrm.unrecovered_tmo);
			printf("\tLBT-RM LBM messages received                              : %lu\n", Statistics->transport.lbtrm.lbm_msgs_rcved);
			printf("\tLBT-RM LBM messages received with uninteresting topic     : %lu\n", Statistics->transport.lbtrm.lbm_msgs_no_topic_rcved);
			printf("\tLBT-RM LBM requests received                              : %lu\n", Statistics->transport.lbtrm.lbm_reqs_rcved);
			printf("\tLBT-RM datagrams dropped (size)                           : %lu\n", Statistics->transport.lbtrm.dgrams_dropped_size);
			printf("\tLBT-RM datagrams dropped (type)                           : %lu\n", Statistics->transport.lbtrm.dgrams_dropped_type);
			printf("\tLBT-RM datagrams dropped (version)                        : %lu\n", Statistics->transport.lbtrm.dgrams_dropped_version);
			printf("\tLBT-RM datagrams dropped (hdr)                            : %lu\n", Statistics->transport.lbtrm.dgrams_dropped_hdr);
			printf("\tLBT-RM datagrams dropped (other)                          : %lu\n", Statistics->transport.lbtrm.dgrams_dropped_other);
			printf("\tLBT-RM datagrams received out of order                    : %lu\n", Statistics->transport.lbtrm.out_of_order);
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			printf("\tLBT-RU datagrams received                                 : %lu\n", Statistics->transport.lbtru.msgs_rcved);
			printf("\tLBT-RU datagram bytes received                            : %lu\n", Statistics->transport.lbtru.bytes_rcved);
			printf("\tLBT-RU NAK packets sent                                   : %lu\n", Statistics->transport.lbtru.nak_pckts_sent);
			printf("\tLBT-RU NAKs sent                                          : %lu\n", Statistics->transport.lbtru.naks_sent);
			printf("\tLost LBT-RU datagrams detected                            : %lu\n", Statistics->transport.lbtru.lost);
			printf("\tNCFs received (ignored)                                   : %lu\n", Statistics->transport.lbtru.ncfs_ignored);
			printf("\tNCFs received (shed)                                      : %lu\n", Statistics->transport.lbtru.ncfs_shed);
			printf("\tNCFs received (retransmit delay)                          : %lu\n", Statistics->transport.lbtru.ncfs_rx_delay);
			printf("\tNCFs received (unknown)                                   : %lu\n", Statistics->transport.lbtru.ncfs_unknown);
			printf("\tLoss recovery minimum time                                : %lums\n", Statistics->transport.lbtru.nak_stm_min);
			printf("\tLoss recovery mean time                                   : %lums\n", Statistics->transport.lbtru.nak_stm_mean);
			printf("\tLoss recovery maximum time                                : %lums\n", Statistics->transport.lbtru.nak_stm_max);
			printf("\tMinimum transmissions per individual NAK                  : %lu\n", Statistics->transport.lbtru.nak_tx_min);
			printf("\tMean transmissions per individual NAK                     : %lu\n", Statistics->transport.lbtru.nak_tx_mean);
			printf("\tMaximum transmissions per individual NAK                  : %lu\n", Statistics->transport.lbtru.nak_tx_max);
			printf("\tDuplicate LBT-RU datagrams received                       : %lu\n", Statistics->transport.lbtru.duplicate_data);
			printf("\tLBT-RU datagrams unrecoverable (window advance)           : %lu\n", Statistics->transport.lbtru.unrecovered_txw);
			printf("\tLBT-RU datagrams unrecoverable (NAK generation expiration): %lu\n", Statistics->transport.lbtru.unrecovered_tmo);
			printf("\tLBT-RU LBM messages received                              : %lu\n", Statistics->transport.lbtru.lbm_msgs_rcved);
			printf("\tLBT-RU LBM messages received with uninteresting topic     : %lu\n", Statistics->transport.lbtru.lbm_msgs_no_topic_rcved);
			printf("\tLBT-RU LBM requests received                              : %lu\n", Statistics->transport.lbtru.lbm_reqs_rcved);
			printf("\tLBT-RU datagrams dropped (size)                           : %lu\n", Statistics->transport.lbtru.dgrams_dropped_size);
			printf("\tLBT-RU datagrams dropped (type)                           : %lu\n", Statistics->transport.lbtru.dgrams_dropped_type);
			printf("\tLBT-RU datagrams dropped (version)                        : %lu\n", Statistics->transport.lbtru.dgrams_dropped_version);
			printf("\tLBT-RU datagrams dropped (hdr)                            : %lu\n", Statistics->transport.lbtru.dgrams_dropped_hdr);
			printf("\tLBT-RU datagrams dropped (SID)                            : %lu\n", Statistics->transport.lbtru.dgrams_dropped_sid);
			printf("\tLBT-RU datagrams dropped (other)                          : %lu\n", Statistics->transport.lbtru.dgrams_dropped_other);
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			printf("\tLBT-IPC datagrams received                                : %lu\n", Statistics->transport.lbtipc.msgs_rcved);
			printf("\tLBT-IPC datagram bytes received                           : %lu\n", Statistics->transport.lbtipc.bytes_rcved);
			printf("\tLBT-IPC LBM messages received                             : %lu\n", Statistics->transport.lbtipc.lbm_msgs_rcved);
			printf("\tLBT-IPC LBM messages received with uninteresting topic    : %lu\n", Statistics->transport.lbtipc.lbm_msgs_no_topic_rcved);
			printf("\tLBT-IPC LBM requests received                             : %lu\n", Statistics->transport.lbtipc.lbm_reqs_rcved);
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			printf("\tLBT-RDMA datagrams received              : %lu\n", Statistics->transport.lbtrdma.msgs_rcved);
			printf("\tLBT-RDMA datagram bytes received         : %lu\n", Statistics->transport.lbtrdma.bytes_rcved);
			printf("\tLBT-RDMA LBM messages received           : %lu\n", Statistics->transport.lbtrdma.lbm_msgs_rcved);
			printf("\tLBT-RDMA LBM messages received (no topic): %lu\n", Statistics->transport.lbtrdma.lbm_msgs_no_topic_rcved);
			printf("\tLBT-RDMA LBM requests received           : %lu\n", Statistics->transport.lbtrdma.lbm_reqs_rcved);
			break;
	}
	fflush(stdout);
}

void
dump_src_statistics(struct cache_stat_elem_t *cache_stat)
{
	lbm_src_transport_stats_t * Statistics = &cache_stat->stats.src;
#if 0
	struct in_addr addr;
	char appid[256];
	time_t timestamp;
	char * time_string;
	lbm_ulong_t source = LBMMON_ATTR_SOURCE_NORMAL;
	lbm_ulong_t objectid = 0;
	lbm_ulong_t processid = 0;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}
	switch (source)
	{
		case LBMMON_ATTR_SOURCE_IM:
			printf("\nContext IM source statistics received");
			break;
		default:
			printf("\nSource statistics received");
			break;
	}
	if (lbmmon_attr_get_appsourceid(AttributeBlock, appid, sizeof(appid)) == 0)
	{
		printf(" from %s", appid);
	}
	if (lbmmon_attr_get_ipv4sender(AttributeBlock, (lbm_uint_t *) &(addr.s_addr)) == 0)
	{
		printf(" at %s", inet_ntoa(addr));
	}
	if (lbmmon_attr_get_processid(AttributeBlock, &(processid)) == 0)
	{
		printf(", process ID=%0lx", processid);
	}
	if (lbmmon_attr_get_objectid(AttributeBlock, &(objectid)) == 0)
	{
		printf(", object ID=%0lx", objectid);
	}
	if (lbmmon_attr_get_timestamp(AttributeBlock, &timestamp) == 0)
	{
		time_string = ctime(&timestamp);
		printf(", sent %s", time_string);
		/* Reminder: ctime() returns a string terminated with a newline */
	}
	else
	{
		printf("\n");
	}
	printf("Source: %s\n", Statistics->source);
	printf("Transport: %s\n", translate_transport(Statistics->type));
#endif
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			printf("\tClients       : %lu\n", Statistics->transport.tcp.num_clients);
			printf("\tBytes buffered: %lu\n", Statistics->transport.tcp.bytes_buffered);
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			printf("\tLBT-RM datagrams sent                                 : %lu\n", Statistics->transport.lbtrm.msgs_sent);
			printf("\tLBT-RM datagram bytes sent                            : %lu\n", Statistics->transport.lbtrm.bytes_sent);
			printf("\tLBT-RM datagrams in transmission window               : %lu\n", Statistics->transport.lbtrm.txw_msgs);
			printf("\tLBT-RM datagram bytes in transmission window          : %lu\n", Statistics->transport.lbtrm.txw_bytes);
			printf("\tLBT-RM NAK packets received                           : %lu\n", Statistics->transport.lbtrm.nak_pckts_rcved);
			printf("\tLBT-RM NAKs received                                  : %lu\n", Statistics->transport.lbtrm.naks_rcved);
			printf("\tLBT-RM NAKs ignored                                   : %lu\n", Statistics->transport.lbtrm.naks_ignored);
			printf("\tLBT-RM NAKs shed                                      : %lu\n", Statistics->transport.lbtrm.naks_shed);
			printf("\tLBT-RM NAKs ignored (retransmit delay)                : %lu\n", Statistics->transport.lbtrm.naks_rx_delay_ignored);
			printf("\tLBT-RM retransmission datagrams sent                  : %lu\n", Statistics->transport.lbtrm.rxs_sent);
			printf("\tLBT-RM datagrams queued by rate control               : %lu\n", Statistics->transport.lbtrm.rctlr_data_msgs);
			printf("\tLBT-RM retransmission datagrams queued by rate control: %lu\n", Statistics->transport.lbtrm.rctlr_rx_msgs);
			printf("\tLBT-RM retransmission datagram bytes sent             : %lu\n", Statistics->transport.lbtrm.rx_bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			printf("\tLBT-RU datagrams sent                    : %lu\n", Statistics->transport.lbtru.msgs_sent);
			printf("\tLBT-RU datagram bytes sent               : %lu\n", Statistics->transport.lbtru.bytes_sent);
			printf("\tLBT-RU NAK packets received              : %lu\n", Statistics->transport.lbtru.nak_pckts_rcved);
			printf("\tLBT-RU NAKs received                     : %lu\n", Statistics->transport.lbtru.naks_rcved);
			printf("\tLBT-RU NAKs ignored                      : %lu\n", Statistics->transport.lbtru.naks_ignored);
			printf("\tLBT-RU NAKs shed                         : %lu\n", Statistics->transport.lbtru.naks_shed);
			printf("\tLBT-RU NAKs ignored (retransmit delay)   : %lu\n", Statistics->transport.lbtru.naks_rx_delay_ignored);
			printf("\tLBT-RU retransmission datagrams sent     : %lu\n", Statistics->transport.lbtru.rxs_sent);
			printf("\tClients                                  : %lu\n", Statistics->transport.lbtru.num_clients);
			printf("\tLBT-RU retransmission datagram bytes sent: %lu\n", Statistics->transport.lbtru.rx_bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			printf("\tClients                    : %lu\n", Statistics->transport.lbtipc.num_clients);
			printf("\tLBT-IPC datagrams sent     : %lu\n", Statistics->transport.lbtipc.msgs_sent);
			printf("\tLBT-IPC datagram bytes sent: %lu\n", Statistics->transport.lbtipc.bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			printf("\tClients                    : %lu\n", Statistics->transport.lbtrdma.num_clients);
			printf("\tLBT-RDMA datagrams sent     : %lu\n", Statistics->transport.lbtrdma.msgs_sent);
			printf("\tLBT-RDMA datagram bytes sent: %lu\n", Statistics->transport.lbtrdma.bytes_sent);
			break;
	}
	fflush(stdout);
}

void
rcv_statistics_cb(const void * AttributeBlock, const lbm_rcv_transport_stats_t * Statistics, void * ClientData)
{
	add_stat_to_cache(globalmoncache, AttributeBlock, (const union lbm_stats_structs *) Statistics, RCV_STATS);
}

void
src_statistics_cb(const void * AttributeBlock, const lbm_src_transport_stats_t * Statistics, void * ClientData)
{
	add_stat_to_cache(globalmoncache, AttributeBlock, (const union lbm_stats_structs *) Statistics, SRC_STATS);
}

void
evq_statistics_cb(const void * AttributeBlock, const lbm_event_queue_stats_t * Statistics, void * ClientData)
{
	return;
#if 0
Not using evq data yet
	add_stat_to_cache(globalmoncache, AttributeBlock, (const union lbm_stats_structs *) Statistics, EVQ_STATS);
#endif
}

void
ctx_statistics_cb(const void * AttributeBlock, const lbm_context_stats_t * Statistics, void * ClientData)
{
return;
#if 0
not using ctx data yet
	add_stat_to_cache(globalmoncache, AttributeBlock, (const union lbm_stats_structs *) Statistics, CTX_STATS);
#endif
}

int monsrc_notify_func(const char *topic_str, const char *source, void *clientd) {
	moncache_source_notif_t *s = (moncache_source_notif_t *) malloc(sizeof(*s));

	if(!s) return 0;

	if(dump_src_notifs)
		printf("Adding topic %s Source %s to cache\n",topic_str,source);

	strcpy(s->topic,topic_str);
	strcpy(s->source,source);

	add_stat_to_cache(globalmoncache, NULL, (const union lbm_stats_structs *) s, SRC_NOTIF);
	return 0;
}

lbm_src_notify_func_t monsrc_notify_t = { monsrc_notify_func, 0 };

/* Separate thread for dumping stats in the background */
#if defined(_WIN32)
DWORD WINAPI stat_thread_main(void *arg)
#else
void *stat_thread_main(void *arg)
#endif /* _WIN32 */
{
	while (1)
	{
		if(ctx_timeout == 0 && sctx) {
			lbm_context_delete(sctx);
			sctx = NULL;
			ctx_timeout = DEFAULT_CTX_TIMEOUT;
		}

		lock_cache(globalmoncache);
		ageout_cache(globalmoncache);
		unlock_cache(globalmoncache);

		if(!sctx)
			moncache_create_ctx();

		SLEEP_SEC(1);

		ctx_timeout--;

		if(dump_cache_stat && waitingforinput) {
			printf("Current cache size is %d, added %d\n",
				get_curr_cache_size(globalmoncache), get_curr_ivl_cache_added(globalmoncache));
		}
		reset_cache_ivl(globalmoncache);
	}

	return (0);
}


void moncache_create_ctx()
{
	/* Create context attributes */
	if (lbm_context_attr_create(&cattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}
	/* Set source notification function */
 	if (lbm_context_attr_setopt(cattr, "resolver_source_notification_function",
		&monsrc_notify_t, sizeof(monsrc_notify_t)) != 0) {

 		fprintf(stderr, "lbm_context_attr_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 		exit(1);
 	}
	/* Create LBM context (passing in context attributes) to know sources */
	if (lbm_context_create(&sctx, cattr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	lbmmon_rctl_t * monctl;
	lbmmon_rctl_attr_t * attr;
	lbmmon_rcv_statistics_func_t rcv = { rcv_statistics_cb };
	lbmmon_src_statistics_func_t src = { src_statistics_cb };
	lbmmon_evq_statistics_func_t evq = { evq_statistics_cb };
	lbmmon_ctx_statistics_func_t ctx = { ctx_statistics_cb };
	int rc;
	int c;
	int errflag = 0;
	char * transport_options = NULL;
	char transport_options_string[1024];
	char * format_options = NULL;
	char format_options_string[1024];
	const lbmmon_transport_func_t * transport = lbmmon_transport_lbm_module();
	const lbmmon_format_func_t * format = lbmmon_format_csv_module();

#ifdef _WIN32
	{
		WSADATA wsadata;
		int status;
		
		/* Windows socket setup code */
		if ((status = WSAStartup(MAKEWORD(2,2), &wsadata)) != 0)
		{
			fprintf(stderr, "%s: WSA startup error - %d\n", argv[0], status);
			exit(1);
		}
	}
#else
	/*
	 * Ignore SIGPIPE on UNIXes which can occur when writing to a socket
	 * with only one open end point.
	 */
	signal(SIGPIPE, SIG_IGN);
#endif /* _WIN32 */

	/* Create the global cache */
	lbm_log(log_callback, NULL);

	memset(transport_options_string, 0, sizeof(transport_options_string));
	memset(format_options_string, 0, sizeof(format_options_string));

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'C':
				moncache_default_size = atoi(optarg);
				break;
			case 't':
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "lbm") == 0)
					{
						transport = lbmmon_transport_lbm_module();
					}
					else if (strcasecmp(optarg, "udp") == 0)
					{
						transport = lbmmon_transport_udp_module();
					}
					else if (strcasecmp(optarg, "lbmsnmp") == 0)
					{
						transport = lbmmon_transport_lbmsnmp_module();
					}
					else
					{
						++errflag;
					}
				}
				else
				{
					++errflag;
				}
				break;

			case 0:
				if (optarg != NULL)
				{
					strncpy(transport_options_string, optarg, sizeof(transport_options_string));
				}
				else
				{
					++errflag;
				}
				break;

			case 'f':
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "csv") == 0)
					{
						format = lbmmon_format_csv_module();
					}
					else
					{
						++errflag;
					}
				}
				else
				{
					++errflag;
				}
				break;

			case 1:
				if (optarg != NULL)
				{
					strncpy(format_options_string, optarg, sizeof(format_options_string));
				}
				else
				{
					++errflag;
				}
				break;

			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);

			case '?':
			default:
				++errflag;
				break;
		}
	}

	if (errflag != 0)
	{
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	globalmoncache = moncache_new();

	if (strlen(transport_options_string) > 0)
	{
		transport_options = transport_options_string;
	}
	if (strlen(format_options_string) > 0)
	{
		format_options = format_options_string;
	}

	rc = lbmmon_rctl_attr_create(&attr);
	if (attr == NULL)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_create() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_RECEIVER_CALLBACK, (void *) &rcv, sizeof(rcv));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_SOURCE_CALLBACK, (void *) &src, sizeof(src));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_EVENT_QUEUE_CALLBACK, (void *) &evq, sizeof(evq));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_CONTEXT_CALLBACK, (void *) &ctx, sizeof(ctx));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_create(&monctl, format, (void *) format_options, transport, (void *) transport_options, attr, NULL);
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_create() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	lbmmon_rctl_attr_delete(attr);

	{
	int thrdid;
#if defined(_WIN32)
	HANDLE wthrdh;
	DWORD wthrdid;
#else
	pthread_t pthid;
#endif /* _WIN32 */
#if defined(_WIN32)
		if ((wthrdh = CreateThread(NULL, 0, stat_thread_main, &thrdid, 0,
									  &wthrdid)) == NULL) {
			fprintf(stderr, "could not create thread\n");
			exit(1);
		}
#else
		if (pthread_create(&pthid, NULL, stat_thread_main, &thrdid) != 0) {
			fprintf(stderr, "could not spawn thread\n");
			exit(1);
		}
#endif /* _WIN32 */
	}

	{
	char c = 'h';
	while(1) {

		waitingforinput = 0;

		lock_cache(globalmoncache);
		switch(c) {
		default:
		case 'h':
			printf("Enter option\n");
			printf("1) Dump topics by searching source string\n");
			printf("2) Dump topics by searching source IP\n");
			printf("3) Dump topics by searching destination IP\n");
			printf("4) Dump sources by searching for receivers IP\n");
			printf("A) Request Ads from contexts (can be intrusive on the whole system)\n");
			printf("r) Dump all receiving transports\n");
			printf("t) Dump all topics and sources\n");
			printf("T) Dump sources by topic\n");
			printf("S) Search for source/receiver by string and dump stats\n");
			printf("V) Save Search for source/receiver by string and dump stats constantly\n");
			printf("Toggle Stats\n");
			printf("c) Toggle stats output\n");
			printf("n) Toggle output of source notifications\n");
			printf("v) Toggle dumping new data\n");
			printf("\n");
			printf("q) Quit\n");
			break;

		case '1':
		{
			char searchterm[128],*start,*end;

			printf("Enter string to search\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_srcs_by_substr(start);
			break;
		}

		case '2':
		{
			char searchterm[32],*start,*end;

			printf("Enter Source IP to search for\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_srcs_by_srcip(start);
			break;
		}
			
		case '3':
		{
			char searchterm[32],*start,*end;

			printf("Enter Destination IP to search for\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_srcs_by_destip(start);
			break;
		}
			
		case '4':
		{
			char searchterm[32],*start,*end;

			printf("Enter Receiver IP to search for\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_srcs_by_rcvrip(start);
			break;
		}
			
		case 'A':
			/* Just send one request on the network for sources */
			lbm_context_topic_resolution_request(sctx,LBM_TOPIC_RES_REQUEST_ADVERTISEMENT,1000,0);
			break;

		case 'n':
			dump_src_notifs = !dump_src_notifs;
			break;

		case 'r':
			dump_rcvrsrcs();
			break;

		case 'c':
			dump_cache_stat = !dump_cache_stat;
			break;

		case 'S':
		{
			char searchterm[128],*start,*end;

			printf("Enter string to search\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_stats_by_substr(start);
			break;
		}

		case 'V':
		{
			char searchterm[128],*start,*end;

			printf("Enter string to search (and save or just return to clear)\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0') {
				strcpy(saved_searchterm,searchterm);
				dump_stats_by_substr(start);
			}
			else
				saved_searchterm[0] = '\0';
			break;
		}

		case 't':
			dump_srctopics(NULL);
			break;

		case 'v':
			dump_verbose = !dump_verbose;
			break;

		case 'T':
		{
			char searchterm[128],*start,*end;

			printf("Enter topic to search\n");

			fgets(searchterm,sizeof(searchterm),stdin);

			start = searchterm;

			while(isspace(*start) && *start != '\0') start++;
			end = start;
			while(!isspace(*end) && *end != '\0') end++;
			if(isspace(*end)) *end = '\0'; /* Terminate string without cr/lf */

			if(*start != '\0')
				dump_srctopics(start);
			break;
		}

		}
		unlock_cache(globalmoncache);

		waitingforinput = 1;

		c = getchar();

		if(c == 0xa) continue;

		getchar();

		if(c == 'q') break;

	}
	}
	lbmmon_rctl_destroy(monctl);
	return 0;
}


int
log_callback(int Level, const char * Message, void * ClientData)
{
	fprintf(stderr, "%s\n", Message);
	return (0);
}

moncache_t *moncache_new() {
	moncache_t *m = malloc(sizeof(*m));
	if(!m) return NULL;

	memset(m,0,sizeof(*m));

	m->max_cache_size = moncache_default_size;

	MUTEX_INIT(m->lock);

	return m;
}

int add_stat_to_cache( moncache_t *cache, const void * AttributeBlock, const union lbm_stats_structs *stats, int stat_type) {
	struct cache_stat_elem_t *new_elem;

	new_elem = malloc(sizeof(cache_stat));
	if(!new_elem) { fprintf(stderr,"Failed to allocate cache stat\n"); return -1; }

	/* Precautionary */
	memset(new_elem,0,sizeof(*new_elem));

	new_elem->stat_type = stat_type;
	switch(stat_type) {
	case SRC_STATS:
		memcpy(&new_elem->stats,stats,sizeof(new_elem->stats.src));
		break;
	case RCV_STATS:
		memcpy(&new_elem->stats,stats,sizeof(new_elem->stats.rcv));
		break;
	case SRC_NOTIF:
		memcpy(&new_elem->stats,stats,sizeof(new_elem->stats.srcnotif));
		break;
	default:
		/* Err, forgot something - just copy the whole block, though
		 * technically will invoke bad reads at the end
		 */
		memcpy(&new_elem->stats,stats,sizeof(new_elem->stats));
	}
	if (stat_type != SRC_NOTIF) {
		if (lbmmon_attr_get_appsourceid(AttributeBlock, new_elem->appid, sizeof(new_elem->appid)) != 0)
		{
			new_elem->appid[0] = '\0';
		}
		if (lbmmon_attr_get_ipv4sender(AttributeBlock, (lbm_uint_t *) &(new_elem->addr.s_addr)) != 0)
		{
			printf(" Could not get sender address from statistics\n");
		}
		strcpy(new_elem->addr_str,inet_ntoa(new_elem->addr));
		if (lbmmon_attr_get_timestamp(AttributeBlock, &new_elem->timestamp) != 0)
		{
			printf(" Could not get timestamp address from statistics\n");
		}
		/* Would use strdup here - but windows requires use of _strdup instead
		 * because of heap corruption on windows..... so for portability I do it
		 */
		switch(stat_type) {
		case SRC_STATS:
			new_elem->protocol = malloc(strlen(stats->src.source) + 1);
			strcpy(new_elem->protocol,stats->src.source);
			if(dump_verbose)
				printf("Source: App %s @ %s for source: %s\n", new_elem->appid, inet_ntoa(new_elem->addr), new_elem->stats.src.source);
			if(saved_searchterm[0] != '\0')
				dump_stats_by_substr(saved_searchterm);
			break;
		case RCV_STATS:
			new_elem->protocol = malloc(strlen(stats->rcv.source) + 1);
			strcpy(new_elem->protocol,stats->rcv.source);
			if(dump_verbose)
				printf("Receiver: App %s @ %s for source: %s\n", new_elem->appid, inet_ntoa(new_elem->addr), new_elem->stats.rcv.source);
			if(saved_searchterm[0] != '\0')
				dump_stats_by_substr(saved_searchterm);
			break;
		}
	}
	else
	{
		/* ditto */
		new_elem->protocol = malloc(strlen(stats->srcnotif.source) + 1);
		strcpy(new_elem->protocol,stats->srcnotif.source);
		new_elem->appid[0] = '\0';
		if(dump_verbose)
			printf("Source: App (notification) %s Topic %s\n", new_elem->stats.srcnotif.source, new_elem->stats.srcnotif.topic);
	}


	if(new_elem->protocol) {
		char *colon = strchr(new_elem->protocol,':');
		if(colon) {
			new_elem->srcip = colon + 1;
			*colon++ = '\0';
		}	
		if(colon) colon = strchr(colon,':');
		if(colon) {
			new_elem->srcport = colon + 1;
			*colon++ = '\0';
		}	
		if(colon) {
			colon = strchr(colon,':');
			if(colon) {
				new_elem->transport = colon + 1;
				*colon++ = '\0';
			}	
			if(colon) colon = strchr(colon,':');
			if(colon) {
				new_elem->destip = colon + 1;
				*colon++ = '\0';
			}	
			if(colon) colon = strchr(colon,':');
			if(colon) {
				new_elem->destport = colon + 1;
				*colon++ = '\0';
			}	
		}	
		/* colon should now point to the end of src str or the topic index */
	}

	lock_cache(cache);

	/* Calculate this entries hashes */
	cache_calc_hashes(cache,new_elem);

	/* Before adding to cache - search so we don't add a dup */
	{
	struct cache_stat_elem_t *dup = cache_search_dup(cache,new_elem);
	if(dup) {
		/* This is a dup - free new one and update old timestamp */
		current_tv(&dup->lasttv);

		/* Update stats to latest */
		switch(stat_type) {
		case SRC_STATS:
			memcpy(&dup->stats,&new_elem->stats,sizeof(new_elem->stats.src));
			break;
		case RCV_STATS:
			memcpy(&dup->stats,&new_elem->stats,sizeof(new_elem->stats.rcv));
			break;
		case SRC_NOTIF:
			memcpy(&dup->stats,&new_elem->stats,sizeof(new_elem->stats.srcnotif));
			break;
		default:
			/* Err, forgot something - just copy the whole block, though
		 	* technically will invoke bad reads at the end
		 	*/
			memcpy(&dup->stats,&new_elem->stats,sizeof(new_elem->stats));
			break;
		}

		free_cache_elem(new_elem);

		unlock_cache(globalmoncache);

		return 0;
	}
	}

	/* Mark creation time */
	current_tv(&new_elem->lasttv);

	/* Need to determine if any contents need reallocating */

	/* Add to the head so the newest data is at the top */
	if(cache->head) cache->head->prv = new_elem;
	else cache->tail = new_elem;
	new_elem->nxt = cache->head;
	cache->head = new_elem;

	cache->curr_cache_size++;
	cache->curr_cache_ivl_added++;

	trim_cache_to_size(cache);

	unlock_cache(cache);

	return 0;
}

void remove_cache_elem(moncache_t *cache,struct cache_stat_elem_t *oldelem) {
	if(oldelem->prv)
		oldelem->prv->nxt = oldelem->nxt;
	if(oldelem->nxt)
		oldelem->nxt->prv = oldelem->prv;
	if(cache->tail == oldelem)
		cache->tail = oldelem->prv;
	if(cache->head == oldelem)
		cache->head = oldelem->nxt;

	cache->curr_cache_size--;

	oldelem->nxt = NULL; /* precautionary */
	oldelem->prv = NULL; /* precautionary */
}

void free_cache_elem(struct cache_stat_elem_t *oldelem) {
	if(oldelem->protocol) free(oldelem->protocol);
	free(oldelem);
}

void free_cache_tail(moncache_t *cache) {
	struct cache_stat_elem_t *oldelem;

	oldelem = cache->tail;

	remove_cache_elem(cache,oldelem);

	free_cache_elem(oldelem);
}

void trim_cache_to_size(moncache_t *cache) {
	int trimmed = 0;

#ifdef DEBUG
	printf("Starting to trim %d %d\n", curr_cache_size, max_cache_size);
#endif
	while(cache->curr_cache_size > cache->max_cache_size) {
		free_cache_tail(cache);
		trimmed++;
	}
#ifdef DEBUG
	printf("Trimmed %d elements from cache\n", trimmed);
#endif
}

/* Course ageout */
void ageout_cache(moncache_t *cache) {
	struct cache_stat_elem_t *curr = cache->head,*nxt;
	int aged = 0;
	struct timeval currtv,endtv;

	current_tv(&currtv);

	while(curr) {
		memcpy(&endtv,&currtv,sizeof(endtv));

		endtv.tv_sec -= curr->lasttv.tv_sec;
		endtv.tv_usec -= curr->lasttv.tv_usec;
		normalize_tv(&endtv);
		
		nxt = curr->nxt;
		if(endtv.tv_sec >= DEFAULT_AGE_TIMEOUT) {
			remove_cache_elem(cache,curr);
			free_cache_elem(curr);
			aged++;
		}
		curr = nxt;
	}

	if(aged > 0 && dump_cache_stat)
		printf("%d elements aged out\n", aged);
}

int get_curr_cache_size(moncache_t *cache) { return cache->curr_cache_size; }
void lock_cache(moncache_t *cache) { MUTEX_LOCK(cache->lock); }
void unlock_cache(moncache_t *cache) { MUTEX_UNLOCK(cache->lock); }

/* Interval related controls on the cache */
int get_curr_ivl_cache_added(moncache_t *cache) { return cache->curr_cache_ivl_added; }
void reset_cache_ivl(moncache_t *cache) { cache->curr_cache_ivl_added = 0; }

/* Search routines */
void cache_calc_hashes(moncache_t *cache,struct cache_stat_elem_t *new_elem) {
	const char *c;
	/* Start hash with stat_type */
	unsigned long entry_hash = new_elem->stat_type;

	/* Add App id to hash */
	c = new_elem->appid;
	while(*c != '\0') { entry_hash += (unsigned long) *c; c++; };

	switch(new_elem->stat_type) {
	case SRC_STATS :
		c = new_elem->stats.src.source;
		while(*c != '\0') { entry_hash += (unsigned long) *c; c++; };
		break;
	case RCV_STATS :
		c = new_elem->stats.rcv.source;
		while(*c != '\0') { entry_hash += (unsigned long) *c; c++; };
		break;
	case SRC_NOTIF :
		c = new_elem->stats.srcnotif.source;
		while(*c != '\0') { entry_hash += (unsigned long) *c; c++; };
		c = new_elem->stats.srcnotif.topic;
		while(*c != '\0') { entry_hash += (unsigned long) *c; c++; };
		break;
	}

	/* Add the sender address */
	entry_hash += (unsigned long) new_elem->addr.s_addr;

#ifdef DEBUG
printf("HASH %lx app %s ip %x\n",entry_hash,new_elem->appid,new_elem->addr.s_addr);
#endif
	new_elem->entry_hash = entry_hash;
}

struct cache_stat_elem_t *cache_search_dup(moncache_t *cache,struct cache_stat_elem_t *new_elem) {
	struct cache_stat_elem_t *curr = cache->head;

	for( ; curr ; curr = curr->nxt) {
		if(new_elem->entry_hash != curr->entry_hash) continue;
		if(new_elem->stat_type != curr->stat_type) continue;
		if(curr->addr.s_addr != new_elem->addr.s_addr) continue;;

		switch(curr->stat_type) {
		case SRC_STATS :
			if(strcmp(curr->stats.src.source,new_elem->stats.src.source)) continue;
			break;
		case RCV_STATS :
			if(strcmp(curr->stats.rcv.source,new_elem->stats.rcv.source)) continue;
			break;
		case SRC_NOTIF :
			if(strcmp(curr->stats.srcnotif.source,new_elem->stats.srcnotif.source)) continue;
			if(strcmp(curr->stats.srcnotif.topic,new_elem->stats.srcnotif.topic)) continue;
			break;
		}

		if(strcmp(curr->appid,new_elem->appid)) continue;

		/* looks like a match */
		return curr;
	}
	return 0;
}

/* Cache dump routines */

void dump_srcs_by_substr(char *term)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping sources matching %s\n",term);
	while(curr) {
		switch(curr->stat_type) {
		case SRC_STATS :
		{	char *str = strstr(curr->stats.src.source,term);
			if(str)
				printf("Source: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.src.source);
			break;
		}
		case RCV_STATS :
		{	char *str = strstr(curr->stats.rcv.source,term);
			if(str)
				printf("Receiver: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.rcv.source);
			break;
		}
		case SRC_NOTIF :
		{	char *str = strstr(curr->stats.srcnotif.source,term);
			if(str)
				printf("Source: App (notification) %s Topic %s\n", curr->stats.srcnotif.source, curr->stats.srcnotif.topic);
			break;
		}
		}
		curr = curr->nxt;
	}
}

void dump_srcs_by_srcip(char *srcip)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping sources matching source IP %s\n",srcip);
	while(curr) {
		switch(curr->stat_type) {
		case SRC_STATS :
		{
			if(curr->srcip && (strcmp(curr->srcip,srcip) == 0))
				printf("Source: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.src.source);
			break;
		}
		case RCV_STATS :
		{
			char *str = strstr(curr->stats.rcv.source,srcip);
			if(str) {
				printf("Receiver: App %s @ %s for source: %s\n", 
					curr->appid,inet_ntoa(curr->addr),
					curr->stats.rcv.source);
			}
			break;
		}
		case SRC_NOTIF :
		{	char *str = strchr(curr->stats.srcnotif.source,':');
			if(str) {
				str++;
				if(!strncmp(str,srcip,strlen(srcip))) {
					printf("Source: (notification) %s topic %s\n",
						curr->stats.srcnotif.source, curr->stats.srcnotif.topic);
				}
			}
		}
		}
		curr = curr->nxt;
	}
	
}

void dump_srcs_by_destip(char *destip)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping sources matching destination IP %s\n",destip);
	while(curr) {
		switch(curr->stat_type) {
		case SRC_STATS :
			if(curr->destip && (strcmp(curr->destip,destip) == 0))
				printf("Source: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.src.source);
			break;
		case RCV_STATS :
		{	char *str = strchr(curr->stats.rcv.source,':');
			if(!str) break; /* Protocol skipped */
			str++;
			str = strchr(str,':');
			if(!str) break; /* source IP skipped */
			str++;
			str = strstr(str,destip);
			if(str) {
				printf("Receiver: App %s @ %s for source: %s\n", 
					curr->appid,inet_ntoa(curr->addr),
					curr->stats.rcv.source);
			}
			break;
		}
		case SRC_NOTIF :
		{	char *str = strchr(curr->stats.srcnotif.source,':');
			if(!str) break; /* Protocol skipped */
			str++;
			str = strchr(str,':');
			if(!str) break; /* source IP skipped */
			str++;
			str = strstr(str,destip);
			if(str) {
				printf("Source: (notification) %s topic %s\n",
					curr->stats.srcnotif.source, curr->stats.srcnotif.topic);
			}
		}

		}
		curr = curr->nxt;
	}
	
}

void dump_srctopics(char *topic)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	if(topic)
		printf("Dumping sources for topic %s\n",topic);
	else
		printf("Dumping known topics and sources\n");

	while(curr) {

		switch(curr->stat_type) {
		case SRC_NOTIF :
			if(topic == NULL || strcmp(topic,curr->stats.srcnotif.topic) == 0) {
				printf("Source: (notification) %s topic %s\n", 
					curr->stats.srcnotif.source, curr->stats.srcnotif.topic);
			}
		}
		curr = curr->nxt;
	}
	
}

void dump_srcs_by_rcvrip(char *rcvrip)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping sources by receiver IP %s\n",rcvrip);
	
	while(curr) {

		if(!strcmp(rcvrip,curr->addr_str)) {
			switch(curr->stat_type) {
			case RCV_STATS :
				printf("Receiver: App %s @ %s for source: %s\n", 
					curr->appid,rcvrip, curr->stats.rcv.source);
			break;
		}

		}
		curr = curr->nxt;
	}
	
}

void dump_rcvrsrcs()
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping all receiver sources\n");
	
	while(curr) {

		switch(curr->stat_type) {
		case RCV_STATS :
			printf("Receiver: App %s @ %s for source: %s\n", 
				curr->appid,inet_ntoa(curr->addr),
				curr->stats.rcv.source);
		break;
		}

		curr = curr->nxt;
	}
	
}
 
void dump_stats_by_substr(char *term)
{
	struct cache_stat_elem_t *curr = globalmoncache->head;

	printf("Dumping stats matching %s\n",term);
	while(curr) {
		switch(curr->stat_type) {
		case SRC_STATS :
		{	char *str = strstr(curr->stats.src.source,term);
			if(str) {
				printf("Source: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.src.source);
				dump_src_statistics(curr);
			}
			break;
		}
		case RCV_STATS :
		{	char *str = strstr(curr->stats.rcv.source,term);
			if(str) {
				printf("Receiver: App %s @ %s for source: %s\n", curr->appid, inet_ntoa(curr->addr), curr->stats.rcv.source);
				dump_rcv_statistics(curr);
			}
			break;
		}
		}
		curr = curr->nxt;
	}
}

