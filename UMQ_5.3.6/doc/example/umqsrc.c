/*
"umqsrc.c: application that sends to a given topic (single
"  source) at a rate-limited pace. Understands UMQ.

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
*/

#ifdef __VOS__
#define _POSIX_C_SOURCE 200112L 
#include <sys/time.h>
#include <pthread.h>
#endif
#if defined(__TANDEM) && defined(HAVE_TANDEM_SPT)
	#include <ktdmtyp.h>
	#include <spthread.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <sys/timeb.h>
	#define strcasecmp stricmp
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <signal.h>
	#include <sys/time.h>
	#include <netdb.h>
	#include <errno.h>
	#if defined(__VMS)
		#include ppl$routines
	#else
		#include <semaphore.h>
		#ifdef __APPLE__
			#include <libkern/OSAtomic.h>
		#endif
	#endif
	#if defined(__TANDEM)
		#include <strings.h>
	#endif
#endif
#include "replgetopt.h"
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "monmodopts.h"
#include "verifymsg.h"
#include "lbm-example-util.h"

static const char *rcsid_example_umqsrc = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/umqsrc.c#2 $";

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_MAX_MESSAGES 10000000
#define DEFAULT_MSGS_PER_SEC 0
#define DEFAULT_FLIGHT_SZ	0
#define DEFAULT_DELAY_B4CLOSE 5

/* Application Level Counters */
unsigned long appsent, stablerecv;

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

/* Generic semaphore macros. */
#if defined(_WIN32)
	/* Windows doesn't allow us to read a semaphore's value, so we have to
	 * keep track of it separately.  If more than one application thread
	 * was sending with the same semaphore, doing it this simply wouldn't
	 * be correct; but umesrc only has one thread that ever increments the
	 * semaphore, and only one thread that ever decrements it. */
	int semaphore_value = 0;
	#define SEMAPHORE_INCREMENT(sem) \
		do { \
			if (!ReleaseSemaphore(sem, 1, NULL)) { \
				fprintf(stderr, "ReleaseSemaphore error: %d\n", GetLastError()); \
			} else { \
				InterlockedIncrement(&semaphore_value); \
			} \
		} while(0)
	#define SEMAPHORE_DECREMENT(sem) \
		do { \
			while (WaitForSingleObject(sem, INFINITE) != WAIT_OBJECT_0) {} \
			InterlockedDecrement(&semaphore_value); \
		} while(0)
	#define SEMAPHORE_GETVALUE(sem,value) \
		do { \
			value = semaphore_value; \
		} while(0)
#elif defined(__VMS)
	#define SEMAPHORE_INCREMENT(sem) PPL$INCREMENT_SEMAPHORE(&sem)
	#define SEMAPHORE_DECREMENT(sem) PPL$DECREMENT_SEMAPHORE(&sem)
	#define SEMAPHORE_GETVALUE(sem,value) PPL$READ_SEMAPHORE(&sem,&value)
#elif defined(__APPLE__)
	/* sem_getvalue doesn't seem to be implemented on all versions of OSX,
	 * so keep track of value separately. */
	lbm_uint32_t semaphore_value = 0;
	#define SEMAPHORE_INCREMENT(sem) \
		do { \
			if (sem_post(sem) < 0) { \
				fprintf(stderr, "sem_post failed. Errno=%d\n", errno); \
				exit(1); \
			} else { \
				semaphore_value = OSAtomicIncrement32(&semaphore_value);\
			} \
		} while(0)
	#define SEMAPHORE_DECREMENT(sem) \
		do { \
			while (sem_wait(sem) < 0) {} \
			semaphore_value = OSAtomicDecrement32(&semaphore_value);\
		} while(0)
	#define SEMAPHORE_GETVALUE(sem,value) \
		do { \
			value = semaphore_value; \
		} while(0)
#else
	#define SEMAPHORE_INCREMENT(sem) sem_post(&sem)
	#define SEMAPHORE_DECREMENT(sem) \
		do { \
			while (sem_wait(&sem) < 0) {} \
		} while(0)
	#define SEMAPHORE_GETVALUE(sem,value) sem_getvalue(&sem,&value)
#endif

const char Purpose[] = "Purpose: Send messages on a single topic.";
const char Usage[] =
	"Usage: %s [options] topic\n"
	"Available options:\n"
"  -A, --appsets=CFG         use ULB Application Sets given by CFG\n"
"  -c, --config=FILE         use LBM configuration file FILE\n"
"  -d, --delay=NUM           delay sending for NUM seconds after source creation\n"
"  -h, --help                display this help and exit\n"
"  -f, --flight-size=NUM     allow NUM unstabilized messages in flight (determines message rate)\n"
"  -i, --ids                 display Message IDs for sent message\n"
"  -I, --im                  submit Immediate Messages to the Queue\n"
"  -l, --length=NUM          send messages of NUM bytes\n"
"  -L, --linger=NUM          linger for NUM seconds before closing context\n"
"  -M, --messages=NUM        send NUM messages\n"
"  -m, --message-rate=NUM    send at NUM messages per second (trumped by -f)\n"
"  -n, --non-block           use non-blocking I/O\n"
"  -P, --pause=NUM           pause NUM milliseconds after each send\n"
"  -Q, --queue=name          use Queue given by name\n"
"  -R, --rate=[UM]DATA/RETR  Set transport type to LBT-R[UM], set data rate limit to\n"
"                            DATA bits per second, and set retransmit rate limit to\n"
"                            RETR bits per second.  For both limits, the optional\n"
"                            k, m, and g suffixes may be used.  For example,\n"
"                            '-R 1m/500k' is the same as '-R 1000000/500000'\n"
"  -s, --statistics=NUM      print statistics every NUM seconds\n"
"  -T, --stability           set Message Stability Notification\n"
"  -v, --verbose             print additional info in verbose form\n"
"  -V, --verifiable          construct verifiable messages\n"
"  -X, --index               Send messages on specified index\n"
MONOPTS_SENDER
MONMODULEOPTS_SENDER;

const char * OptionString = "A:c:d:f:hiIL:l:M:m:nP:Q:R:s:vVX:";
#define OPTION_MONITOR_SRC 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
const struct option OptionTable[] =
{
	{ "appsets", required_argument, NULL, 'A' },
	{ "config", required_argument, NULL, 'c' },
	{ "delay", required_argument, NULL, 'd' },
	{ "flight-size", required_argument, NULL, 'f' },
	{ "help", no_argument, NULL, 'h' },
	{ "ids", no_argument, NULL, 'i' },
	{ "im", no_argument, NULL, 'I' },
	{ "length", required_argument, NULL, 'l' },
	{ "linger", required_argument, NULL, 'L' },
	{ "message-rate", required_argument, NULL, 'm' },
	{ "messages", required_argument, NULL, 'M' },
	{ "non-block", no_argument, NULL, 'n' },
	{ "pause", required_argument, NULL, 'P' },
	{ "queue", required_argument, NULL, 'Q' },
	{ "rate", required_argument, NULL, 'R' },
	{ "statistics", required_argument, NULL, 's' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "verifiable", no_argument, NULL, 'V' },
	{ "index", required_argument, NULL, 'X' },
	{ "monitor-src", required_argument, NULL, OPTION_MONITOR_SRC },
	{ "monitor-ctx", required_argument, NULL, OPTION_MONITOR_CTX },
	{ "monitor-transport", required_argument, NULL, OPTION_MONITOR_TRANSPORT },
	{ "monitor-transport-opts", required_argument, NULL, OPTION_MONITOR_TRANSPORT_OPTS },
	{ "monitor-format", required_argument, NULL, OPTION_MONITOR_FORMAT },
	{ "monitor-format-opts", required_argument, NULL, OPTION_MONITOR_FORMAT_OPTS },
	{ "monitor-appid", required_argument, NULL, OPTION_MONITOR_APPID },
	{ NULL, 0, NULL, 0 }
};

struct Options {
	int ids;											/* option to display message IDs or not */
	int flightsz;									/* number of messages per "flight" */
	int im;         								/* submit Immediate Messages or source-based */

	char transport_options_string[1024];	/* Transport options given to lbmmon_sctl_create() */
	char format_options_string[1024];		/* Format options given to lbmmon_sctl_create() */
	char application_id_string[1024];		/* Application ID given to lbmmon_context_monitor() */
	char conffname[256];							/* Configuration filename */
	int delay,linger;								/* Interval to linger before and after sending messages */
	size_t msglen;									/* Length of messages to be sent */

	size_t min_msglen;							/* Minimum length of messages */
	size_t max_msglen;							/* Maximum length of messages */
	int decrement;									/* flag to control whether we increment or decrement the msglen */
	size_t msglen_incr;							/* length to increment/decrement by */
	unsigned int msglen_seed;					/* seed for using generating random message lengths */

	unsigned int msgs;							/* Number of messages to be sent */
	int msgs_per_sec;								/* Message rate: number of messages per second */
	int nonblock;									/* Flag to control whether blocking sends are used */
	int pause_ivl;									/* Pause interval between messages */
	lbm_uint64_t rm_rate;					/* Rate control values */
	lbm_uint64_t rm_retrans;				/* Rate control values */
	char rm_protocol;								/* Rate control protocol */
	lbm_ulong_t stats_sec;						/* Interval for dumping statistics, in milliseconds */
	int stability;									/* Flag to enable Message Stability Notification */
	char qname[256];								/* Queue name to send to */
	char appsets[256];      					/* ULB Application Sets */
	lbm_umq_index_info_t index; /* UMQ index to send on. */
	int use_index;             /* Send on an index or not. */

	int verbose;									/* Flag to control program verbosity */
	int verifiable_msgs;							/* Flag to control message verification (verifymsg.h) */
	int monitor_context;							/* Flag to control context level monitoring */
	int monitor_context_ivl;					/* Interval for context level monitoring */
	int monitor_source;			 				/* Flag to control source level monitoring */
	unsigned int monitor_source_ivl;		 	/* Interval for source level monitoring */

	lbmmon_transport_func_t * transport;	/* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;			/* Function pointer to chosen format module */

	char *topic;									/* The topic on which messages will be sent */
} options;

int blocked = 0;

/* For the elapsed time, calculate and print the msgs/sec and bits/sec */
void print_bw(FILE *fp, struct timeval *tv, size_t msgs, unsigned long long bytes)
{
	double sec = 0.0, mps = 0.0, bps = 0.0;
	double kscale = 1000.0, mscale = 1000000.0;
	char mgscale = 'K', bscale = 'K';
	
	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */	
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	bps = ((double)(bytes<<3))/sec; /* Multiply by 8 and divide */
	if (mps <= mscale) {
		mgscale = 'K';
		mps /= kscale;
	} else {
		mgscale = 'M';
		mps /= mscale;
	}
	if (bps <= mscale) {
		bscale = 'K';
		bps /= kscale;
	} else {
		bscale = 'M';
		bps /= mscale;
	}
	fprintf(fp, "%.04g secs. %.04g %cmsgs/sec. %.04g %cbps\n", sec,
			mps, mgscale, bps, bscale);
	fflush(fp);
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_src_t *src)
{
	lbm_src_transport_stats_t stats;

	if (src != NULL) {
		/* Retrieve source transport statistics */
		if (lbm_src_retrieve_transport_stats(src, &stats) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_retrieve_stats: %s\n", lbm_errmsg());
			exit(1);
		}
		switch (stats.type) {
		case LBM_TRANSPORT_STAT_TCP:
			fprintf(fp, "TCP, buffered %lu, clients %lu, app sent %lu stable %lu inflight %lu\n",stats.transport.tcp.bytes_buffered,
				stats.transport.tcp.num_clients,
				appsent,stablerecv,stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
			break;
		case LBM_TRANSPORT_STAT_LBTRM:
			fprintf(fp, "LBT-RM, sent %lu/%lu, txw %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu, rctlr %lu/%lu, app sent %lu stable %lu inflight %lu\n",
				stats.transport.lbtrm.msgs_sent, stats.transport.lbtrm.bytes_sent,
				stats.transport.lbtrm.txw_msgs, stats.transport.lbtrm.txw_bytes,
				stats.transport.lbtrm.naks_rcved, stats.transport.lbtrm.nak_pckts_rcved,
				stats.transport.lbtrm.naks_ignored, stats.transport.lbtrm.naks_rx_delay_ignored,
				stats.transport.lbtrm.naks_shed,
				stats.transport.lbtrm.rxs_sent,
				stats.transport.lbtrm.rctlr_data_msgs, stats.transport.lbtrm.rctlr_rx_msgs,
				appsent,stablerecv,stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
			break;
		case LBM_TRANSPORT_STAT_LBTRU:
			fprintf(fp, "LBT-RU, clients %lu, sent %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu app sent %lu stable %lu inflight %lu\n",
				stats.transport.lbtru.num_clients,
				stats.transport.lbtru.msgs_sent, stats.transport.lbtru.bytes_sent,
				stats.transport.lbtru.naks_rcved, stats.transport.lbtru.nak_pckts_rcved,
				stats.transport.lbtru.naks_ignored, stats.transport.lbtru.naks_rx_delay_ignored,
				stats.transport.lbtru.naks_shed,
				stats.transport.lbtru.rxs_sent,
				appsent,stablerecv,stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
			break;
		default:
			break;
		}
	} else {
		fprintf(fp, "app sent %lu stable %lu inflight %lu\n", 
			appsent, stablerecv, stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
	}
	fflush(fp);
}

/* Logging callback */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	int newline = 1;

	if (message[strlen(message)-1] == '\n')
		newline = 0;

	if (newline)
		printf("LOG Level %d: %s\n", level, message);
	else
		printf("LOG Level %d: %s", level, message);
	return 0;
}

struct TimerControl {
	int stats_timer_id;
	lbm_ulong_t stats_msec;
	int stop_rescheduling_timer;
} timer_control = { -1, 0, 0 };

int force_reclaim_total = 0;
struct timeval reclaim_tsp = { 0, 0 };
#if defined(_WIN32)
HANDLE nflight;
#elif defined(__VMS)
unsigned long nflight;
#elif defined(__APPLE__)
sem_t * nflight;
char nflight_name[256];
FILE *dev_random_fp;
lbm_ulong_t rand_num;
#else
sem_t nflight;
#endif
lbm_uint_t last_clientd_stable = 0;
lbm_uint_t last_clientd_sent = 0;
int sleep_before_sending = 0;
int initial_src_reg = 1;
int initial_ulb_reg = 1;

/* Source event handler callback (passed into lbm_src_create()) */
int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
	struct Options *opts = &options;

	switch (event) {
	case LBM_SRC_EVENT_CONNECT:
		{
			const char *clientname = (const char *)ed;
			
			printf("Receiver connect [%s]\n",clientname);
		}
		break;
	case LBM_SRC_EVENT_DISCONNECT:
		{
			const char *clientname = (const char *)ed;
			
			printf("Receiver disconnect [%s]\n",clientname);
		}
		break;
	case LBM_SRC_EVENT_WAKEUP:
		blocked = 0;
		break;
	case LBM_SRC_EVENT_UMQ_MESSAGE_ID_INFO:
		{
			lbm_src_event_umq_message_id_info_t *info = (lbm_src_event_umq_message_id_info_t *)ed;
			
			printf("ID [%" PRIx64 ":%" PRIx64 "] (cd %p)\n", info->msg_id.regid, info->msg_id.stamp, info->msg_clientd);
		}
		break;
	case LBM_SRC_EVENT_UMQ_REGISTRATION_ERROR:
		{
			const char *errstr = (const char *)ed;
			
			printf("Error registering source with UMQ queue: %s\n", errstr);
		}
		break;
	case LBM_SRC_EVENT_UMQ_REGISTRATION_COMPLETE_EX:
		{
			int i, semval;
			lbm_src_event_umq_registration_complete_ex_t *reg = (lbm_src_event_umq_registration_complete_ex_t *)ed;

			sleep_before_sending = 1000;

			/* do this for the initial src registration only */
			if (initial_src_reg) {
				/* When registration completes, whatever the value of the
				 * flight size semaphore was, bump it up to the starting
				 * flight size again. */
				if (opts->flightsz > 0) {
					SEMAPHORE_GETVALUE(nflight,semval);
					for (i = (opts->flightsz - semval); i > 0; i--) {
						SEMAPHORE_INCREMENT(nflight);
					}
				}
				initial_src_reg = 0;
			}
			
			printf("UMQ \"%s\"[%x] src registration complete. Flags %x ", reg->queue, reg->queue_id, reg->flags);
			if (reg->flags & LBM_SRC_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)
				printf("QUORUM ");
			printf("\n");
		}
		break;
	case LBM_SRC_EVENT_UMQ_MESSAGE_STABLE_EX:
		{
			int i, semval;
			lbm_src_event_umq_stability_ack_info_ex_t *info = (lbm_src_event_umq_stability_ack_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ \"%s\"[%x][%s][%u]: message [%" PRIx64 ":%" PRIx64 "] [%x,%x] stable. (cd %p). Flags %x ", 
					   info->queue, info->queue_id, info->queue_instance, info->queue_instance_index, info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number, info->msg_clientd, info->flags);
				if (info->flags & LBM_SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE)
					printf("IA ");
				if (info->flags & LBM_SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_INTERGROUP_STABLE)
					printf("IR ");
				if (info->flags & LBM_SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_STABLE)
					printf("STABLE ");
				printf("\n");				
			}
			if (info->flags & LBM_SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_STABLE) {
				/* Peg the counter for the received stable message */
				stablerecv++;
				if (opts->flightsz > 0) {
					SEMAPHORE_INCREMENT(nflight);
				}
			}
		}
		break;
	case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED:
		{
			lbm_src_event_ume_ack_info_t *ackinfo = (lbm_src_event_ume_ack_info_t *)ed;

			if (opts->verbose)
				printf("UME message reclaimed - sequence number %x (cd %p)\n",	ackinfo->sequence_number, ackinfo->msg_clientd);
		}
		break;
	case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
		{
			lbm_src_event_ume_ack_ex_info_t *ackinfo = (lbm_src_event_ume_ack_ex_info_t *)ed;

			if (opts->verbose) {
				printf("UME message reclaimed (ex) - sequence number %x (cd %p). Flags 0x%x ",
					ackinfo->sequence_number, ackinfo->msg_clientd, ackinfo->flags);
				if (ackinfo->flags & LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED_EX_FLAG_FORCED) {
					printf("FORCED");
				}
				printf("\n");
			}
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_RECEIVER_REGISTRATION_EX:
		{
			lbm_src_event_umq_ulb_receiver_info_ex_t *info = (lbm_src_event_umq_ulb_receiver_info_ex_t *)ed;
			int i, semval;

			/* do this for the receiver rregistration only */
			if (initial_ulb_reg) {
				/* When registration completes, whatever the value of the
				 * flight size semaphore was, bump it up to the starting
				 * flight size again. */
				if (opts->flightsz > 0) {
					SEMAPHORE_GETVALUE(nflight,semval);
					for (i = (opts->flightsz - semval); i > 0; i--) {
						SEMAPHORE_INCREMENT(nflight);
					}
				}
				initial_ulb_reg = 0;
			}
			printf("UMQ ULB [%" PRIx64 "][%x][%u] receiver [%s] registration.\n", info->registration_id, info->assignment_id, info->application_set_index,
				   info->receiver);
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_RECEIVER_DEREGISTRATION_EX:
		{
			lbm_src_event_umq_ulb_receiver_info_ex_t *info = (lbm_src_event_umq_ulb_receiver_info_ex_t *)ed;

			printf("UMQ ULB [%" PRIx64 "][%x][%u] receiver [%s] deregistration.\n", info->registration_id, info->assignment_id, info->application_set_index,
				   info->receiver);
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_RECEIVER_READY_EX:
		{
			lbm_src_event_umq_ulb_receiver_info_ex_t *info = (lbm_src_event_umq_ulb_receiver_info_ex_t *)ed;

			printf("UMQ ULB [%" PRIx64 "][%x][%u] receiver [%s] ready for messages.\n", info->registration_id, info->assignment_id, info->application_set_index,
				   info->receiver);
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_RECEIVER_TIMEOUT_EX:
		{
			lbm_src_event_umq_ulb_receiver_info_ex_t *info = (lbm_src_event_umq_ulb_receiver_info_ex_t *)ed;

			printf("UMQ ULB [%" PRIx64 "][%x][%u] receiver [%s] EOL.\n", info->registration_id, info->assignment_id, info->application_set_index,
				   info->receiver);
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_MESSAGE_CONSUMED_EX:
		{
			lbm_src_event_umq_ulb_message_info_ex_t *info = (lbm_src_event_umq_ulb_message_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ ULB message [%" PRIx64 ":%" PRIx64 "] [%x,%x] consumed by [%" PRIx64 "][%x][%u][%s]\n", info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number, info->registration_id, info->assignment_id, info->application_set_index,
					   info->receiver);
			}
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_MESSAGE_ASSIGNED_EX:
		{
			lbm_src_event_umq_ulb_message_info_ex_t *info = (lbm_src_event_umq_ulb_message_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ ULB message [%" PRIx64 ":%" PRIx64 "] [%x,%x] assigned to [%" PRIx64 "][%x][%u][%s] (cd %p)\n", info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number, info->registration_id, info->assignment_id, info->application_set_index,
					   info->receiver, info->msg_clientd);
			}
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_MESSAGE_REASSIGNED_EX:
		{
			lbm_src_event_umq_ulb_message_info_ex_t *info = (lbm_src_event_umq_ulb_message_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ ULB message [%" PRIx64 ":%" PRIx64 "] [%x,%x] reassigned from [%" PRIx64 "][%x][%u][%s]\n", info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number, info->registration_id, info->assignment_id, info->application_set_index,
					   info->receiver);
			}
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_MESSAGE_TIMEOUT_EX:
		{
			lbm_src_event_umq_ulb_message_info_ex_t *info = (lbm_src_event_umq_ulb_message_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ ULB message [%" PRIx64 ":%" PRIx64 "] [%x,%x] EOL [%u]\n", info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number, info->application_set_index);
			}
		}
		break;
	case LBM_SRC_EVENT_UMQ_ULB_MESSAGE_COMPLETE_EX:
		{
			lbm_src_event_umq_ulb_message_info_ex_t *info = (lbm_src_event_umq_ulb_message_info_ex_t *)ed;

			if (opts->verbose) {
				printf("UMQ ULB message [%" PRIx64 ":%" PRIx64 "] [%x,%x] complete\n", info->msg_id.regid, info->msg_id.stamp,
					   info->first_sequence_number, info->last_sequence_number);
			}
			/* Peg the counter for the received stable message */
			stablerecv++;
			if (opts->flightsz > 0) {
				SEMAPHORE_INCREMENT(nflight);
			}
		}
		break;
	case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION:
		{
			lbm_src_event_flight_size_notification_t *fsnote = (lbm_src_event_flight_size_notification_t *)ed;

			if (opts->verbose) {
				printf("Flight Size Notification. Type ");
				switch (fsnote->type) {
				case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UME:
					printf("UME");
					break;
				case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_ULB:
					printf("ULB");
					break;
				case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UMQ:
					printf("UMQ");
					break;
				default:
					printf("unknown");
					break;
				}
				printf(". Inflight is %s specified flight size\n",
					fsnote->state == LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_STATE_OVER ? "OVER" : "UNDER");
			}
		}
		break;
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

int handle_ctx_src_event(lbm_context_t *ctx, int event, void *ed, void *cd)
{
	/* just pass to src event handling */
	return handle_src_event(NULL, event, ed, cd);
}

int handle_ctx_event(lbm_context_t *ctx, int event, void *ed, void *cd)
{
	struct Options *opts = &options;

	switch (event) {
	case LBM_CONTEXT_EVENT_UMQ_REGISTRATION_ERROR:
		{
			const char *errstr = (const char *)ed;
			
			printf("Error registering ctx with UMQ queue: %s\n", errstr);
		}
		break;
	case LBM_CONTEXT_EVENT_UMQ_REGISTRATION_SUCCESS_EX:
		{
			lbm_context_event_umq_registration_ex_t *reg = (lbm_context_event_umq_registration_ex_t *)ed;

			printf("UMQ queue \"%s\"[%x][%s][%u] ctx registration. ID %" PRIx64 " Flags %x ", reg->queue, reg->queue_id, reg->queue_instance, reg->queue_instance_index,
				reg->registration_id, reg->flags);
			if (reg->flags & LBM_CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)
				printf("QUORUM ");
			printf("\n");
		}
		break;
	case LBM_CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX:
		{
			lbm_context_event_umq_registration_complete_ex_t *reg = (lbm_context_event_umq_registration_complete_ex_t *)ed;

			printf("UMQ queue \"%s\"[%x] ctx registration complete. ID %" PRIx64 " Flags %x ", reg->queue, reg->queue_id, reg->registration_id, reg->flags);
			if (reg->flags & LBM_CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)
				printf("QUORUM ");
			printf("\n");
			
			/* if submitting IMs and flightsz in use, then try to increment semaphore */
			if (opts->im && opts->flightsz > 0) {
				int i = 0, semval = 0;

				SEMAPHORE_GETVALUE(nflight,semval);
				for (i = opts->flightsz - semval - 1; i > 0; i--) {
					SEMAPHORE_INCREMENT(nflight);
				}
			}
		}
		break;
	case LBM_CONTEXT_EVENT_UMQ_INSTANCE_LIST_NOTIFICATION:
		{
			const char *evstr = (const char *)ed;
			
			printf("UMQ IL Notification: %s\n", evstr);
		}
		break;
	default:
		printf("Unknown context event %d\n", event);
		break;
	}
	return 0;	
}

/* Timer callback to handle periodic display of source statistics */
int handle_stats_timer(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *src = (lbm_src_t *) clientd;

	print_stats(stdout, src);

	if (!timer_control.stop_rescheduling_timer) {
		if ((timer_control.stats_timer_id =
			lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, timer_control.stats_msec)) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	return 0;
}

int handle_force_reclaim(const char *topic, lbm_uint_t sqn, void *clientd)
{
	struct timeval *tsp = (struct timeval *)clientd;
	struct timeval endtv, nowtv;
	double secs = 0;
	
	if (tsp == NULL) {
		fprintf(stderr,"WARNING: source for topic \"%s\" forced reclaim %x\n", topic, sqn);
	} else {
		current_tv(&endtv);
		endtv.tv_sec -= tsp->tv_sec;
		endtv.tv_usec -= tsp->tv_usec;
		normalize_tv(&endtv);
		secs = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
		force_reclaim_total++;
		if (secs > 5.0) {
			fprintf(stderr,"WARNING: source for topic \"%s\" forced reclaim. Total %d.\n", topic, force_reclaim_total);
			current_tv(&nowtv);
			memcpy(tsp,&nowtv,sizeof(nowtv));			
		}
	}
	return 0;
}

/*
 * Function that determines how to pace sending of messages to obtain a given
 * rate.  Given messages per second, calculates number of messages to send in 
 * a particular interval and the number of milliseconds to pause between 
 * intervals. For this example application, the interval between messages
 * is set to be 20ms.
 */
void calc_rate_vals(int msgs_per_sec, int* msgs, int* interval)
{
	int intervals_per_sec = 1000;

	*interval = 20; /* in milliseconds */

	intervals_per_sec = 1000/(*interval);

	while(*interval <= 1000 && msgs_per_sec%intervals_per_sec != 0)
	{
		(*interval)++;
		while(1000%*interval != 0 && *interval <= 1000)
			(*interval)++;
		intervals_per_sec = 1000/(*interval);
	}
	*msgs = msgs_per_sec/intervals_per_sec;
}

#ifdef __VOS__
/* set round-robin scheduling policy for calling thread */
void set_rr_scheduling()
{
	pthread_t thread;
	int e,policy;
	struct sched_param param;

	thread = pthread_self(); /* get calling thread, i.e. main thread */
	pthread_getschedparam(thread, &policy, &param); /* get parameters */

	policy = SCHED_RR;
	e = pthread_setschedparam(thread, policy, &param);

	if(e != 0)
	{
		fprintf(stderr,
		  "failed to set round-robin thread scheduling policy.\n");
		exit(1);
	}
}
#endif

int read_msglen(char *arg, struct Options *opts) {
	int rc;
	int arg1, arg2, arg4;
	char arg3[30];

	rc = sscanf(arg, "%d-%d%[,?]%d", &arg1, &arg2, arg3, &arg4);
	if (rc == 0) {
		return -1;
	}
	/* presumably, rc >= 1 */
	opts->msglen = arg1;
	if (rc >= 2) {
		/* range specified */
		/* check the min and max msglen */
		if (arg1 <= arg2) {
			opts->min_msglen = arg1;
			opts->max_msglen = arg2;
		} else {
			opts->min_msglen = arg2;
			opts->max_msglen = arg1;
			opts->decrement = 1;
		}
	}
	if (rc == 1) {
		opts->max_msglen = opts->msglen;
		opts->min_msglen = opts->msglen;
	} else if (rc == 2 && arg1 != arg2) {
		opts->msglen_incr = 1;
	} else if (rc == 3) {
		if (arg3[0] != '?') {
			return -1;
		}
		opts->msglen_seed = 1234;
		srand(opts->msglen_seed);
	} else if (rc == 4) {
		if (arg3[0] == '?') {
			opts->msglen_seed = arg4;
			srand(opts->msglen_seed);
		} else if (arg3[0] == ',') {
			opts->msglen_incr = arg4;
		} else {
			return -1;
		}
	}
	return 0;
}

void process_cmdline(int argc, char **argv,struct Options *opts)
{
	int c,errflag = 0;

	/* Set default option values */
	memset(opts, 0, sizeof(*opts));
	opts->delay = 1;
	opts->im = 0;
	opts->flightsz = DEFAULT_FLIGHT_SZ;
	opts->linger = DEFAULT_DELAY_B4CLOSE;
	opts->msglen = MIN_ALLOC_MSGLEN;
	opts->min_msglen = 0;
	opts->max_msglen = MIN_ALLOC_MSGLEN;
	opts->decrement = 0;
	opts->msglen_incr = 0;
	opts->msglen_seed = 0;
	opts->msgs = DEFAULT_MAX_MESSAGES;
	opts->msgs_per_sec = DEFAULT_MSGS_PER_SEC;
	opts->conffname[0] = '\0';
	opts->qname[0] = '\0';
	opts->appsets[0] = '\0';
	opts->transport_options_string[0] = '\0';
	opts->format_options_string[0] = '\0';
	opts->application_id_string[0] = '\0';
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();

	/* Process the command line options, setting local variables with values */
	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
		    case 'A':
				strncpy(opts->appsets, optarg, sizeof(opts->appsets));
				break;
			case 'c':
				strncpy(opts->conffname, optarg, sizeof(opts->conffname));
				break;
			case 'd':
				opts->delay = atoi(optarg);
				break;
			case 'f':
				opts->flightsz = atoi(optarg);
				break;
			case 'i':
				opts->ids = 1;
				break;
			case 'I':
				opts->im = 1;
				break;
			case 'l':
				if (read_msglen(optarg, opts) == -1) {
					++errflag;
				}
				break;
			case 'L':
				opts->linger = atoi(optarg);
				break;
			case 'm':
				opts->msgs_per_sec = atoi(optarg);
				break;
			case 'M':
				opts->msgs = atoi(optarg);
				break;
			case 'n':
				opts->nonblock = 1;
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			case 'P':
				opts->pause_ivl = atoi(optarg);
				break;
			case 'Q':
				strncpy(opts->qname, optarg, sizeof(opts->qname));				
				break;
			case 'R':
				errflag += parse_rate(optarg, &opts->rm_protocol, &opts->rm_rate, &opts->rm_retrans);
				break;
			case 's':
				opts->stats_sec = atoi(optarg);
				break;
			case 'v':
				opts->verbose = 1;
				break;
			case 'V':
				opts->verifiable_msgs = 1;
				break;
			case 'X':
			{
				int sscanf_res = 0;
				sscanf_res = sscanf(optarg, "%" SCNu64, &(opts->index.index));
				if (sscanf_res == 1) {
					/* Assume numeric index. */
					opts->index.index_len = sizeof(lbm_uint64_t);
					opts->index.flags |= LBM_UMQ_INDEX_FLAG_NUMERIC;
				}
				else {
					/* Assume named index. */
					strncpy(opts->index.index, optarg, sizeof(opts->index.index));
					opts->index.index_len = strlen(opts->index.index);
					printf("Going to use index \"%s\"\n", opts->index.index);
				}
				opts->use_index = 1;
			}
				break;
			case OPTION_MONITOR_SRC:
				opts->monitor_source = 1;
				opts->monitor_source_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_CTX:
				opts->monitor_context = 1;
				opts->monitor_context_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_TRANSPORT:
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "lbm") == 0)
					{
						opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
					}
					else if (strcasecmp(optarg, "udp") == 0)
					{
						opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_udp_module();
					}
					else if (strcasecmp(optarg, "lbmsnmp") == 0)
					{
						opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbmsnmp_module();
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
			case OPTION_MONITOR_TRANSPORT_OPTS:
				if (optarg != NULL)
				{
					strncpy(opts->transport_options_string, optarg, sizeof(opts->transport_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_FORMAT:
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "csv") == 0)
					{
						opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();
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
			case OPTION_MONITOR_FORMAT_OPTS:
				if (optarg != NULL)
				{
					strncpy(opts->format_options_string, optarg, sizeof(opts->format_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_APPID:
				if (optarg != NULL)
				{
					strncpy(opts->application_id_string, optarg, sizeof(opts->application_id_string));
				}
				else
				{
					++errflag;
				}
				break;
			default:
				errflag++;
				break;
		}
	}
	if ((errflag != 0) || (optind == argc))
	{
		/* An error occurred processing the command line - dump the LBM version, usage and exit */
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	/* command line option processing complete at this point */
	opts->topic = argv[optind];
}


int main(int argc, char **argv)
{
	struct Options *opts = &options; /* filled by process_cmdline */
	double secs = 0.0; /* used for printing message rate statistics */
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src = NULL;
	lbm_src_topic_attr_t * tattr;
	lbm_context_attr_t * cattr;
	struct timeval starttv, endtv;
	int i;
	unsigned long long bytes_sent = 0;
	unsigned long count = 0;
	int flag_value = 0;
	char *message = NULL;
	int msgs_per_ivl = 1;	/* stores result from calc_rate_vals */
	size_t optlen = 0;
	lbmmon_sctl_t * monctl;
	lbm_ume_src_force_reclaim_func_t reclaim_func;
	lbm_context_event_func_t ctx_event_func;
	lbm_context_src_event_func_t src_event_func;
	int xflag = 0;

#ifdef __VOS__
	set_rr_scheduling(); /* set round-robin scheduling policy for thread */
#endif
	
#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;
		
		/* Windows socket startup code */
		if ((status = WSAStartup(MAKEWORD(2,2),&wsadata)) != 0) {
			fprintf(stderr,"%s: WSA startup error - %d\n",argv[0],status);
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

	/* Process the different options set by the command line */
	process_cmdline(argc,argv,opts);

	/* Load LBM/UME configuration from file (if provided) */
	if (opts->conffname[0] != '\0') {
		if (lbm_config(opts->conffname) == LBM_FAILURE) {
			fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	if (opts->msgs_per_sec != 0 && opts->pause_ivl != 0) {
		fprintf(stderr, "-m and -P are conflicting options\n");
		exit(1);
	}

	if (opts->im && opts->monitor_source) {
		fprintf(stderr, "submitting Immediate Messages. Can not monitor source\n");
		exit(1);
	}

	if (opts->im && opts->stats_sec > 0) {
		fprintf(stderr, "submitting Immediate Messages. Can not print source stats.\n");
		exit(1);
	}

	/* If set, check the requested message length is not too small */
	if (opts->verifiable_msgs != 0) {
		size_t min_msglen = minimum_verifiable_msglen();
		if (opts->min_msglen < min_msglen) {
			printf("Specified message length %u is too small for verifiable messages.\n", opts->min_msglen);
			printf("Setting message length to minimum (%u).\n", min_msglen);
			opts->msglen = min_msglen;
			opts->min_msglen = min_msglen;
		}
	}

	if (opts->msgs_per_sec > 0) {
		opts->flightsz = 0;
	}
	if (opts->flightsz > 0 && opts->stability == 0) {
		opts->stability = 1;
		fprintf(stderr, "Enabling message stability notification to control unstablized message backlog\n");
	}
	
	/* Initialize the flightsz semaphore.  It starts at 0, and must
	 * be posted to by a registration complete event to allow us
	 * to start sending (if using flight size). */
	if (opts->flightsz > 0)
	{
#if defined(_WIN32)
		nflight = CreateSemaphore(NULL, 0, opts->flightsz, NULL);
		if (nflight == NULL)
		{
			fprintf(stderr, "CreateSemaphore error: %d\n", GetLastError());
			exit(1);
		}
#elif defined(__VMS)
		int zero = 0;
		int status = PPL$CREATE_SEMAPHORE(&nflight, 0, &opts->flightsz, &zero);
		if ((status & 1) == 0)
		{
			fprintf(stderr, "Failed to create semaphore. Status=0x%X\n", status);
			exit(status);
		}
#elif defined(__APPLE__)
		if ((dev_random_fp = fopen("/dev/random", "r")) == NULL) {
			fprintf(stderr, "Failed to open /dev/random. Errno=%d\n", errno);
			exit(1);
		}
		fread(&rand_num, sizeof(rand_num), 1, dev_random_fp);
		fclose(dev_random_fp);
		sprintf(nflight_name, "/umesrc%d_%u", getpid(), rand_num);
		nflight = NULL;
		if ((nflight = sem_open(nflight_name, O_CREAT|O_EXCL, S_IRWXU, 0)) == SEM_FAILED) {
			fprintf(stderr, "Failed to create semaphore. Errno=%d\n", errno);
			exit(1);
		}
#else
		if (sem_init(&nflight, 0, 0) < 0) {
			fprintf(stderr, "Failed to create semaphore. Errno=%d\n", errno);
			exit(1);
		}
#endif
		/* initially submit 1 IM before we are blocked */
		if (opts->im) {
			SEMAPHORE_INCREMENT(nflight);
		}
	}
	
	/* Setup logging callback */
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}

	/* if message buffer is too small, then the sprintf will cause issues. So, allocate with a min size */
	if (opts->max_msglen < MIN_ALLOC_MSGLEN) {
		message = malloc(MIN_ALLOC_MSGLEN);
	} else {
		message = malloc(opts->max_msglen);
	}
	if (message == NULL) {
		fprintf(stderr, "could not allocate message buffer of size %u bytes\n",opts->msglen);
		exit(1);
	}
	memset(message, 0, opts->max_msglen);
	if (opts->msgs_per_sec > 0)
		calc_rate_vals(opts->msgs_per_sec, &msgs_per_ivl, &opts->pause_ivl);

	printf("%d msgs/sec -> %d msgs/ivl, %d msec ivl\n", opts->msgs_per_sec,
			msgs_per_ivl, opts->pause_ivl);
	/* Retrieve current context settings */
	if (lbm_context_attr_create(&cattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}
	{
		/*
		 * Since we are manually validating attributes, retrieve any XML configuration
		 * attributes set for this context.
		 */
		char ctx_name[256];
		size_t ctx_name_len = sizeof(ctx_name);
		if (lbm_context_attr_str_getopt(cattr, "context_name", ctx_name, &ctx_name_len) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_str_getopt - context_name: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_context_attr_set_from_xml(cattr, ctx_name) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_set_from_xml - context_name: %s\n", lbm_errmsg());
			exit(1);
		}
		/* Retrieve current source topic settings */
		if (lbm_src_topic_attr_create_from_xml(&tattr, ctx_name, opts->topic) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_attr_create_from_xml: %s\n", lbm_errmsg());
			exit(1);
		}
	}

 	if (opts->rm_rate != 0) {
 		printf("Sending with LBT-R%c data rate limit %" PRIu64 ", retransmission rate limit %" PRIu64 "\n", 
			opts->rm_protocol,opts->rm_rate, opts->rm_retrans);
		/* Set transport attribute to LBT-RM */
		switch(opts->rm_protocol) {
		case 'M':
 			if (lbm_src_topic_attr_str_setopt(tattr, "transport", "LBTRM") != 0) {
 				fprintf(stderr, "lbm_src_topic_str_setopt:transport: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RM data rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtrm_data_rate_limit", &opts->rm_rate, sizeof(opts->rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RM retransmission rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtrm_retransmit_rate_limit", &opts->rm_retrans, sizeof(opts->rm_retrans)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtrm_retransmit_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			break;
		case 'U':
 			if (lbm_src_topic_attr_str_setopt(tattr, "transport", "LBTRU") != 0) {
 				fprintf(stderr, "lbm_src_topic_str_setopt:transport: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RU data rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtru_data_rate_limit", &opts->rm_rate, sizeof(opts->rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtru_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RU retransmission rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtru_retransmit_rate_limit", &opts->rm_retrans, sizeof(opts->rm_retrans)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtru_retransmit_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			break;
		}
 	}

	reclaim_func.func = handle_force_reclaim;
	reclaim_func.clientd = &reclaim_tsp;
	if (lbm_src_topic_attr_setopt(tattr, "ume_force_reclaim_function", &reclaim_func, sizeof(reclaim_func)) != 0) {
		fprintf(stderr, "lbm_src_topic_attr_str_setopt:ume_force_reclaim_function: %s\n", lbm_errmsg());
		exit(1);		
	}
	/* if configured, set the queue name option */
	if (opts->qname[0] != '\0') {
		if (lbm_src_topic_attr_str_setopt(tattr, "umq_queue_name", opts->qname) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_str_setopt:umq_queue_name: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/* if configured, set the ULB appset option */
	if (opts->appsets[0] != '\0') {
		if (lbm_src_topic_attr_str_setopt(tattr, "umq_ulb_application_set", opts->appsets) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_str_setopt:umq_ulb_application_set: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	src_event_func.func = handle_ctx_src_event;
	src_event_func.evq = NULL;
	src_event_func.clientd = opts;
	if (lbm_context_attr_setopt(cattr, "source_event_function", &src_event_func, sizeof(src_event_func)) != 0) {
		fprintf(stderr, "lbm_context_str_setopt:source_event_function: %s\n", lbm_errmsg());
		exit(1);		
	}
	ctx_event_func.func = handle_ctx_event;
	ctx_event_func.evq = NULL;
	ctx_event_func.clientd = opts;
	if (lbm_context_attr_setopt(cattr, "context_event_function", &ctx_event_func, sizeof(ctx_event_func)) != 0) {
		fprintf(stderr, "lbm_context_str_setopt:context_event_function: %s\n", lbm_errmsg());
		exit(1);
	}
	
	/* check the queue name */
	optlen = sizeof(opts->qname);
	if (lbm_src_topic_attr_getopt(tattr, "umq_queue_name", opts->qname, &optlen) != 0) {
		fprintf(stderr, "lbm_src_topic_attr_getopt:umq_queue_name: %s\n", lbm_errmsg());
		exit(1);
	}
	if (opts->qname[0] != '\0') {
		printf("Using UMQ queue \"%s\"\n", opts->qname);
	}
	/* check the ULB appsets */
	optlen = sizeof(opts->appsets);
	if (lbm_src_topic_attr_str_getopt(tattr, "umq_ulb_application_set", opts->appsets, &optlen) != 0) {
		fprintf(stderr, "lbm_src_topic_attr_str_getopt:umq_ulb_application_set: %s\n", lbm_errmsg());
		exit(1);
	}
	if (opts->appsets[0] != '\0') {
		char tbuffer[512];
		lbm_ulong_t mask;

		printf("Using ULB application set(s) \"%s\"\n", opts->appsets);
		/* setup the events we want/need */
		optlen = sizeof(mask);
		if (lbm_src_topic_attr_getopt(tattr, "umq_ulb_events", &mask, &optlen) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_getopt:umq_ulb_events: %s\n", lbm_errmsg());
			exit(1);
		}
		if (opts->flightsz > 0) {
			mask |= LBM_SRC_TOPIC_ATTR_UMQ_ULB_EVENT_MSG_COMPLETE | LBM_SRC_TOPIC_ATTR_UMQ_ULB_EVENT_RCV_REGISTRATION |
				LBM_SRC_TOPIC_ATTR_UMQ_ULB_EVENT_RCV_DEREGISTRATION | LBM_SRC_TOPIC_ATTR_UMQ_ULB_EVENT_RCV_TIMEOUT;
		}
		if (opts->verbose) {
			mask |= LBM_SRC_TOPIC_ATTR_UMQ_ULB_EVENT_ALL;
		}
		if (lbm_src_topic_attr_setopt(tattr, "umq_ulb_events", &mask, sizeof(mask)) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_setopt:umq_ulb_events: %s\n", lbm_errmsg());
			exit(1);
		}
		optlen = sizeof(tbuffer);
		if (lbm_src_topic_attr_str_getopt(tattr, "umq_ulb_application_set_assignment_function", tbuffer, &optlen) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_str_getopt:umq_ulb_application_set_assignment_function: %s\n", lbm_errmsg());
			exit(1);
		}
		printf(" Assignment Function(s) \"%s\"\n", tbuffer);
		optlen = sizeof(tbuffer);
		if (lbm_src_topic_attr_str_getopt(tattr, "umq_ulb_application_set_load_factor_behavior", tbuffer, &optlen) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_str_getopt:umq_ulb_application_set_load_factor_behavior: %s\n", lbm_errmsg());
			exit(1);
		}
		printf(" Load Factor Behavior(s) \"%s\"\n", tbuffer);
	}
	if (opts->qname[0] == '\0' && opts->appsets[0] == '\0') {
		fprintf(stderr, "Queue name not set and ULB application sets not set. Exiting.\n");
		exit(1);
	}
	optlen = sizeof(flag_value);
	if (lbm_src_topic_attr_getopt(tattr, "ume_late_join", &flag_value, &optlen) != 0) {
		fprintf(stderr, "lbm_src_topic_attr_getopt:ume_late_join: %s\n", lbm_errmsg());
		exit(1);
	}
	if (flag_value) {
		printf("Using UME Late Join.\n");
	} else {
		printf("Not using UME Late Join.\n");
	}
	if (opts->im) {
		optlen = sizeof(flag_value);
		if (lbm_context_attr_getopt(cattr, "umq_message_stability_notification", &flag_value, &optlen) != 0) {
			fprintf(stderr, "lbm_context_attr_getopt:umq_message_stability_notification: %s\n", lbm_errmsg());
			exit(1);
		}		
	} else {
		optlen = sizeof(flag_value);
		if (lbm_src_topic_attr_getopt(tattr, "umq_message_stability_notification", &flag_value, &optlen) != 0) {
			fprintf(stderr, "lbm_src_topic_attr_getopt:umq_message_stability_notification: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	if (flag_value) {
		printf("Using UMQ Message Stability Notification. ");
		if (opts->verbose == 1)
			printf("Will display message stability events. \n");
		else
			printf(" Will not display events. \n");
	} else {
		printf("Not using UMQ Message Stability Notification.\n");
	}
	
	/* Create LBM context (passing in context attributes) */
	if (lbm_context_create(&ctx, cattr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(cattr);

	if (!opts->im) {
		/* Allocate the desired topic */
		if (lbm_src_topic_alloc(&topic, ctx, opts->topic, tattr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	lbm_src_topic_attr_delete(tattr);

	if (!opts->im) {
		/*
	 	 * Create LBM source passing in the allocated topic and event
	 	 * handler. The source object is returned here in src.
	 	 */
		if (lbm_src_create(&src, ctx, topic, handle_src_event, opts, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
			exit(1);
		}
	} else {
		printf("Submitting Immediate Messages. Not creating a source.\n");
	}
	/* If statistics were requested, set up an LBM timer to dump the statistics */
	if (opts->stats_sec > 0) {
		timer_control.stats_msec = opts->stats_sec * 1000;

		/* Schedule timer to call the function handle_stats_timer() to dump current stats */
		if ((timer_control.stats_timer_id =
			lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, timer_control.stats_msec)) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	if (opts->monitor_context || opts->monitor_source)
	{
		char * transport_options = NULL;
		char * format_options = NULL;
		char * application_id = NULL;

		if (strlen(opts->transport_options_string) > 0)
		{
			transport_options = opts->transport_options_string;
		}
		if (strlen(opts->format_options_string) > 0)
		{
			format_options = opts->format_options_string;
		}
		if (strlen(opts->application_id_string) > 0)
		{
			application_id = opts->application_id_string;
		}
		if (lbmmon_sctl_create(&monctl, opts->format, format_options, opts->transport, transport_options) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_create() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}
		if (opts->monitor_context)
		{
			if (lbmmon_context_monitor(monctl, ctx, application_id, opts->monitor_context_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_context_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		else
		{
			if (lbmmon_src_monitor(monctl, src, application_id, opts->monitor_source_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_src_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
	}
	if (opts->delay > 0) {
		printf("Delaying for %d second%s\n", opts->delay, ((opts->delay > 1) ? "s" : ""));
		SLEEP_SEC(opts->delay);
	}
	printf("Sending %u messages of size %u bytes to topic [%s]\n", opts->msgs, opts->msglen, opts->topic);

	current_tv(&starttv);
	for (count = 0; count < opts->msgs; ) {
		lbm_src_send_ex_info_t exinfo;
		
		for (i = 0; i < msgs_per_ivl; i++)
		{
			exinfo.flags = LBM_SRC_SEND_EX_FLAG_UME_CLIENTD;
			if (opts->verifiable_msgs) {
				construct_verifiable_msg(message, opts->msglen);
			} else {
				sprintf(message, "message %lu", count);
			}
			exinfo.ume_msg_clientd = (void *)((lbm_uint_t)count + 1);
			last_clientd_sent = (lbm_uint_t)count + 1;
			if (opts->ids) {
				exinfo.flags |= LBM_SRC_SEND_EX_FLAG_UMQ_MESSAGE_ID_INFO;
			}
			if (opts->use_index) {
				exinfo.flags |= LBM_SRC_SEND_EX_FLAG_UMQ_INDEX;
				exinfo.umq_index = &(opts->index);
			}

			blocked = 1;
			/* Send message using allocated source */
			if (opts->flightsz > 0)
			{
#if defined(__VMS)
				unsigned short semval;
#else
				int semval;
#endif
				SEMAPHORE_DECREMENT(nflight);
				/* Determine if we are about to block when sending this message. If so,
				 * force a flush to ensure the message leaves */
				SEMAPHORE_GETVALUE(nflight,semval);
				if(semval == 0 || semval == 1)
					xflag = LBM_MSG_FLUSH;
				else
					xflag = 0;
				/* Allow some time for topic resolution to take place
				 * after a store re-registration. */
				if (sleep_before_sending > 0) {
					SLEEP_MSEC(sleep_before_sending);
					sleep_before_sending = 0;
				}
			}
			if (opts->im) {
				if (lbm_queue_immediate_message(ctx, opts->qname, opts->topic, message, opts->msglen, 0, &exinfo) == LBM_FAILURE) {
					fprintf(stderr, "lbm_queue_immediate_message: %s\n", lbm_errmsg());
					exit(1);
				}
			} else {
				if (lbm_src_send_ex(src, message, opts->msglen, (opts->nonblock ? LBM_SRC_NONBLOCK : 0) | xflag, &exinfo) == LBM_FAILURE) {
					if (lbm_errnum() == LBM_EWOULDBLOCK ) {
						while (blocked)	{
							SLEEP_MSEC(100);
						}
						continue;
					}
					if (lbm_errnum() == LBM_EUMENOREG) {
						int sent_ok = 0;

						while (lbm_errnum() == LBM_EUMENOREG && !sent_ok) {
							printf("Send unsuccessful. Waiting...\n");
							SLEEP_MSEC(1000);
							if (lbm_src_send_ex(src, message, opts->msglen, (opts->nonblock ? LBM_SRC_NONBLOCK : 0) | xflag, &exinfo) != LBM_FAILURE) {
								sent_ok = 1;
								break;
							}
						}
						if (!sent_ok) {
							fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
							exit(1);
						} else {
							printf("Send OK. Continuing.\n");
						}
					} else {
						fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
						exit(1);
					}
				}
			}
			blocked = 0;
			bytes_sent += (unsigned long long) opts->msglen;
			count++;
			appsent++;
			if (opts->msglen_incr != 0) {
				if (opts->decrement) {
					opts->msglen -= opts->msglen_incr;
					if (opts->msglen < opts->min_msglen) {
						opts->msglen = opts->max_msglen;
					}
				} else {
					opts->msglen += opts->msglen_incr;
					if (opts->msglen > opts->max_msglen) {
						opts->msglen = opts->min_msglen;
					}
				}
			} else if (opts->msglen_seed != 0) {
				opts->msglen = opts->min_msglen + (rand() % (opts->max_msglen - opts->min_msglen + 1));
			}
		}
		if (opts->pause_ivl > 0)
			SLEEP_MSEC(opts->pause_ivl);
	}
	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);
	secs = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
	printf("Sent %lu messages of size %u bytes in %.04g seconds.\n", count, opts->msglen, secs);
	print_bw(stdout, &endtv, (size_t) count, bytes_sent);
	if (force_reclaim_total > 0)
		printf("%d force reclamations\n", force_reclaim_total);

	/* Stop rescheduling the stats timer */
	timer_control.stop_rescheduling_timer = 1;

	/*
	 * Sleep for a bit so that batching gets out all the queued messages,
	 * if any.  If we just exit, then some messages may not have been sent by
	 * TCP yet.
	 */
	if (opts->stats_sec > 0 && opts->stats_sec > opts->linger) {
		printf("Delaying to catch last stats timer... \n");
		SLEEP_SEC((opts->stats_sec - opts->linger) + 1);
	} else {
		print_stats(stdout, src);
	}
	if (opts->linger > 0) {
		printf("Lingering for %d seconds...\n", opts->linger);
		SLEEP_SEC(opts->linger);
	}
	if (opts->monitor_context || opts->monitor_source)
	{
		if (opts->monitor_context)
		{
			if (lbmmon_context_unmonitor(monctl, ctx) == -1)
			{
				fprintf(stderr, "lbmmon_context_unmonitor() failed\n");
				exit(1);
			}
		}
		else
		{
			if (lbmmon_src_unmonitor(monctl, src) == -1)
			{
				fprintf(stderr, "lbmmon_src_unmonitor() failed\n");
				exit(1);
			}
		}
		if (lbmmon_sctl_destroy(monctl) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_destoy() failed()\n");
			exit(1);
		}
	}

	if (!opts->im) {
		printf("Deleting source\n");
		/* Deallocate source and LBM context */
		lbm_src_delete(src);
		src = NULL;
	}

	printf("Deleting context\n");
	lbm_context_delete(ctx);
	ctx = NULL;
	free(message);

	#ifdef __APPLE__
	sem_close(nflight);
	sem_unlink(nflight_name);
	#endif

	return 0;
}

