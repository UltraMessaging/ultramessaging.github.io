/*
"umqrcv.c: application that receives messages from a given topic
"  (single receiver). Understands UMQ.

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
	#include <sys/timeb.h>
	#define strcasecmp stricmp
	#define snprintf _snprintf
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <signal.h>
	#include <sys/time.h>
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

static const char *rcsid_example_umqrcv = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/umqrcv.c#2 $";

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

const char Purpose[] = "Purpose: Receive messages on a single topic.";
const char Usage[] =
"Usage: %s [options] topic\n"
"Available options:\n"
"  -A, --ascii           display messages as ASCII text (-A -A for newlines after each msg)\n"
"  -c, --config=FILE     use FILE as LBM configuration file\n"
"  -D, --dereg           deregister upon exit\n"
"  -d, --delay=NUM		 delay receiver creation NUM seconds from context creation\n"
"  -E, --exit            exit after source ends\n"
"  -h, --help            display this help and exit\n"
"  -I, --type-id=ID      set Receiver Type ID to ID\n"
"  --max-sources=num     allow num sources (for statistics gathering purposes)\n"
"  -r, --msgs=NUM        delete receiver after NUM messages\n"
"  -s, --statistics=NUM  print statistics every NUM seconds, along with bandwidth\n"
"  -S, --stop            exit after source ends, print throughput summary\n"
"  -X, --index           reserve given index if possible, or leave blank to reserve random index\n"
"  -v, --verbose         be verbose about incoming messages\n"
"                        (-v -v = be even more verbose)\n"
"  -V, --verify          verify message contents\n"
MONOPTS_RECEIVER
MONMODULEOPTS_SENDER;

const char * OptionString = "Ac:Ed:DhI:r:s:SvVX::";
#define OPTION_MONITOR_RCV 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
#define OPTION_MAX_SOURCES 7
const struct option OptionTable[] =
{
	{ "ascii", no_argument, NULL, 'A' },
	{ "config", required_argument, NULL, 'c' },
	{ "exit", no_argument, NULL, 'E' },
	{ "dereg", no_argument, NULL, 'D' },
	{ "delay", required_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ "type-id", required_argument, NULL, 'I' },
	{ "msgs", required_argument, NULL, 'r' },
	{ "statistics", required_argument, NULL, 's' },
	{ "stop", no_argument, NULL, 'S' },
	{ "index", optional_argument, NULL, 'X' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "verify", no_argument, NULL, 'V' },
	{ "max-sources", required_argument, NULL, OPTION_MAX_SOURCES },
	{ "monitor-rcv", required_argument, NULL, OPTION_MONITOR_RCV },
	{ "monitor-ctx", required_argument, NULL, OPTION_MONITOR_CTX },
	{ "monitor-transport", required_argument, NULL, OPTION_MONITOR_TRANSPORT },
	{ "monitor-transport-opts", required_argument, NULL, OPTION_MONITOR_TRANSPORT_OPTS },
	{ "monitor-format", required_argument, NULL, OPTION_MONITOR_FORMAT },
	{ "monitor-format-opts", required_argument, NULL, OPTION_MONITOR_FORMAT_OPTS },
	{ "monitor-appid", required_argument, NULL, OPTION_MONITOR_APPID },
	{ NULL, 0, NULL, 0 }
};

struct Options {
	int ascii;          /* Flag to display messages as ASCII text */
	int end_on_end;     /* Flag to end program when source stops sending */
	int max_sources;    /* Maximum number of sources (for statistics) */
	int msg_limit;      /* Limit on number of messages to receive */
	int stats_ivl;      /* Interval for dumping statistics, in seconds */
	int summary;   /* Flag to display a summary when source stops */
	int verbose;        /* Flag to control program verbosity */
	int verify;         /* Flag to control message verification (verifymsg.h) */
	int dereg;			/* Flag to control deregistration on exit */
	int delay;			/* Delay between context creation and receiver creation */
	lbm_uint_t rcv_type_id; /* receiver type ID */
	char conffname[256]; /* Configuration filename */

	char transport_options_string[1024];    /* Transport options given to lbmmon_sctl_create() */
	char format_options_string[1024];       /* Format options given to lbmmon_sctl_create()  */
	char application_id_string[1024];       /* Application ID given to lbmmon_context_monitor() */
	int monitor_context;                    /* Flag to control context level monitoring */
	int monitor_context_ivl;                /* Interval for context level monitoring */
	int monitor_receiver;                   /* Flag to control receiver level monitoring */
	int monitor_receiver_ivl;               /* Interval for receiver level monitoring */
	lbmmon_transport_func_t * transport;    /* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;          /* Function pointer to chosen format module  */
	char *topic;                            /* The topic on which messages will be received */
	lbm_umq_index_info_t index;             /* UMQ index to reserve. */
	int reserve_index;                      /* Reserve an index or not. */
	int reserve_specific_index;             /* Reserve a _particular_ index. */
} options;


#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10

int msg_count = 0;
int rx_msg_count = 0;
int otr_msg_count = 0;
int total_msg_count = 0;
int total_reassign_count = 0;
int total_resub_count = 0;
int stotal_msg_count = 0;
int subtotal_msg_count = 0;
int byte_count = 0;
#if defined(_WIN32)
signed __int64 total_byte_count = 0;
#else
unsigned long long total_byte_count = 0;
#endif /* _WIN32 */
int unrec_count = 0;
int total_unrec_count = 0;
int burst_loss = 0;
int close_recv = 0;
int close_int = 0;
int force_close_recv = 0;
int deregistering = 0;
struct timeval data_start_tv;
struct timeval data_end_tv;

lbm_uint_t expected_sqn = 0;
char saved_source[LBM_MSG_MAX_SOURCE_LEN] = "";
lbm_ulong_t lost = 0, last_lost = 0;
lbm_rcv_transport_stats_t *stats = NULL;
int nstats;

/*
 * For the elapsed time, calculate and print the msgs/sec, bits/sec, and
 * loss stats
 */
void print_bw(FILE *fp, struct timeval *tv, int msgs, int bytes, int unrec, lbm_ulong_t lost, int rx_msgs, int otr_msgs)
{
	double sec = 0.0, mps = 0.0, bps = 0.0;
	double kscale = 1000.0, mscale = 1000000.0;
	char mgscale = 'K', bscale = 'K';

	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	bps = (double)bytes*8/sec;
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

	if ((rx_msgs != 0) || (otr_msgs != 0))
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]",
			sec, mps, mgscale, bps, bscale, rx_msgs, otr_msgs);
	else
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps",
			sec, mps, mgscale, bps, bscale);

	if (lost != 0 || unrec != 0 || burst_loss != 0) {
		fprintf(fp, " [%lu pkts lost, %u msgs unrecovered, %d bursts]",
				lost, unrec, burst_loss);
	}
	fprintf(fp, "\n");
	fflush(fp);
	burst_loss = 0;
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_rcv_transport_stats_t *stats, int nstats)
{
	int i;

	for (i = 0; i < nstats; i++)
	{
		switch (stats[i].type) {
		case LBM_TRANSPORT_STAT_TCP:
			fprintf(fp, " [%s], received %lu, LBM %lu/%lu/%lu\n",
					stats[i].source,
					stats[i].transport.tcp.bytes_rcved,
					stats[i].transport.tcp.lbm_msgs_rcved,
					stats[i].transport.tcp.lbm_msgs_no_topic_rcved,
					stats[i].transport.tcp.lbm_reqs_rcved);
			break;
		case LBM_TRANSPORT_STAT_LBTRM:
			{
				char stmstr[256] = "", txstr[256] = "";

				if (stats[i].transport.lbtrm.nak_tx_max > 0) {
					/* we usually don't use sprintf, but should be OK here for the moment. */
					sprintf(stmstr, ", nak stm %lu/%lu/%lu",
							stats[i].transport.lbtrm.nak_stm_min, stats[i].transport.lbtrm.nak_stm_mean,
							stats[i].transport.lbtrm.nak_stm_max);
					sprintf(txstr, ", nak tx %lu/%lu/%lu",
							stats[i].transport.lbtrm.nak_tx_min, stats[i].transport.lbtrm.nak_tx_mean,
							stats[i].transport.lbtrm.nak_tx_max);
				}
				fprintf(fp, " [%s], received %lu/%lu, dups %lu, loss %lu, naks %lu/%lu, ncfs %lu-%lu-%lu-%lu, unrec %lu/%lu%s%s\n",
						stats[i].source,
						stats[i].transport.lbtrm.msgs_rcved, stats[i].transport.lbtrm.bytes_rcved,
						stats[i].transport.lbtrm.duplicate_data,
						stats[i].transport.lbtrm.lost,
						stats[i].transport.lbtrm.naks_sent, stats[i].transport.lbtrm.nak_pckts_sent,
						stats[i].transport.lbtrm.ncfs_ignored, stats[i].transport.lbtrm.ncfs_shed,
						stats[i].transport.lbtrm.ncfs_rx_delay, stats[i].transport.lbtrm.ncfs_unknown,
						stats[i].transport.lbtrm.unrecovered_txw,
						stats[i].transport.lbtrm.unrecovered_tmo,
						stmstr, txstr);
			}
			break;
		case LBM_TRANSPORT_STAT_LBTRU:
			{
				char stmstr[256] = "", txstr[256] = "";

				if (stats[i].transport.lbtru.nak_tx_max > 0) {
					/* we usually don't use sprintf, but should be OK here for the moment. */
					sprintf(stmstr, ", nak stm %lu/%lu/%lu",
							stats[i].transport.lbtru.nak_stm_min, stats[i].transport.lbtru.nak_stm_mean,
							stats[i].transport.lbtru.nak_stm_max);
					sprintf(txstr, ", nak tx %lu/%lu/%lu",
							stats[i].transport.lbtru.nak_tx_min, stats[i].transport.lbtru.nak_tx_mean,
							stats[i].transport.lbtru.nak_tx_max);
				}
				fprintf(fp, " [%s], LBM %lu/%lu/%lu, received %lu/%lu, dups %lu, loss %lu, naks %lu/%lu, ncfs %lu-%lu-%lu-%lu, unrec %lu/%lu%s%s\n",
						stats[i].source,
						stats[i].transport.lbtru.lbm_msgs_rcved,
						stats[i].transport.lbtru.lbm_msgs_no_topic_rcved,
						stats[i].transport.lbtru.lbm_reqs_rcved,
						stats[i].transport.lbtru.msgs_rcved, stats[i].transport.lbtru.bytes_rcved,
						stats[i].transport.lbtru.duplicate_data,
						stats[i].transport.lbtru.lost,
						stats[i].transport.lbtru.naks_sent, stats[i].transport.lbtru.nak_pckts_sent,
						stats[i].transport.lbtru.ncfs_ignored, stats[i].transport.lbtru.ncfs_shed,
						stats[i].transport.lbtru.ncfs_rx_delay, stats[i].transport.lbtru.ncfs_unknown,
						stats[i].transport.lbtru.unrecovered_txw,
						stats[i].transport.lbtru.unrecovered_tmo,
						stmstr, txstr);
			}
			break;
		case LBM_TRANSPORT_STAT_LBTIPC:
			{
				fprintf(fp, " [%s] Received %lu msgs/%lu bytes. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats[i].source,
						stats[i].transport.lbtipc.msgs_rcved,
						stats[i].transport.lbtipc.bytes_rcved,
						stats[i].transport.lbtipc.lbm_msgs_rcved,
						stats[i].transport.lbtipc.lbm_msgs_no_topic_rcved,
						stats[i].transport.lbtipc.lbm_reqs_rcved);
			}
			break;
		case LBM_TRANSPORT_STAT_LBTRDMA:
			{
				fprintf(fp, " [%s] Received %lu msgs/%lu bytes. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats[i].source,
						stats[i].transport.lbtrdma.msgs_rcved,
						stats[i].transport.lbtrdma.bytes_rcved,
						stats[i].transport.lbtrdma.lbm_msgs_rcved,
						stats[i].transport.lbtrdma.lbm_msgs_no_topic_rcved,
						stats[i].transport.lbtrdma.lbm_reqs_rcved);
			}
			break;
		default:
			break;
		}
	}
	fflush(fp);
}

/* Utility to print the contents of a buffer in hex/ASCII format */
void dump(const char *buffer, int size)
{
	int i,j;
	unsigned char c;
	char textver[20];

	for (i=0;i<(size >> 4);i++) {
		for (j=0;j<16;j++) {
			c = buffer[(i << 4)+j];
			printf("%02x ",c);
			textver[j] = ((c<0x20)||(c>0x7e))?'.':c;
		}
		textver[j] = 0;
		printf("\t%s\n",textver);
	}
	for (i=0;i<size%16;i++) {
		c = buffer[size-size%16+i];
		printf("%02x ",c);
		textver[i] = ((c<0x20)||(c>0x7e))?'.':c;
	}
	for (i=size%16;i<16;i++) {
		printf("   ");
		textver[i] = ' ';
	}
	textver[i] = 0;
	printf("\t%s\n",textver);
}

/* Logging handler passed into lbm_log() */
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

/*
 * Handler for immediate messages directed to NULL topic
 * (passed into lbm_context_rcv_immediate_msgs()
 */
int rcv_handle_immediate_msg(lbm_context_t *ctx, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;

	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Data message received */
		msg_count++;
		total_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		
		if (opts->ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (opts->ascii > 1) putchar('\n');
		}
		if (opts->verbose) {
			printf("IM [%s][%u], %u bytes\n", msg->source,
					msg->sequence_number, (unsigned) msg->len);
			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_REQUEST:
		/* Request message received (no response processed here) */
		msg_count++;
		total_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		if (opts->ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (opts->ascii > 1) putchar('\n');
		}
		if (opts->verbose) {
			printf("IM Request [%s][%u], %u bytes\n", msg->source,
					msg->sequence_number, (unsigned) msg->len);
			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	default:
		printf("Unknown immediate message lbm_msg_t type %x [%s]\n", msg->type, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* context event handler for UMQ events */
int handle_ctx_event(lbm_context_t *ctx, int event, void *ed, void *cd)
{
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

/* Received message handler (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;
	char msgidstr[48] = "";
	char indexstr[LBM_UMQ_MAX_INDEX_LEN + 1] = "";
	lbm_umq_index_info_t index_info;

	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Data message received */
		if (subtotal_msg_count == 0) {
			/*
			 * Squirrel away source information so we know what
			 * source we are receiving from, although this example
			 * make no real effort in ensure that all messages
			 * received are from this source.
			 */
			strncpy(saved_source, msg->source, sizeof(saved_source));
		}

		(stotal_msg_count == 0) ? current_tv (&data_start_tv) : current_tv(&data_end_tv);

		msg_count++;
		total_msg_count++;
		stotal_msg_count++;
		
		if (msg->flags & LBM_MSG_FLAG_RETRANSMIT)
			rx_msg_count++;
		if (msg->flags & LBM_MSG_FLAG_OTR)
			otr_msg_count++;
		
		if (msg->flags & LBM_MSG_FLAG_UMQ_REASSIGNED)
			total_reassign_count++;
		if (msg->flags & LBM_MSG_FLAG_UMQ_RESUBMITTED)
			total_resub_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		total_byte_count += msg->len;
		if (opts->ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (opts->ascii > 1) putchar('\n');
		}
		if (opts->verbose) {
			lbm_umq_msgid_t mid;
			
			if (lbm_msg_retrieve_msgid(msg, &mid) == 0) {
				sprintf(msgidstr, "[%" PRIx64 ":%" PRIx64 "]", mid.regid, mid.stamp);
			}
			if (lbm_msg_retrieve_umq_index(msg, &index_info) == 0) {
				if (index_info.flags & LBM_UMQ_INDEX_FLAG_NUMERIC)
					snprintf(indexstr, sizeof(indexstr), "[%"PRIu64"]", *(lbm_uint64_t *)(index_info.index));
				else
					snprintf(indexstr, sizeof(indexstr), "[\"%s\"]", index_info.index);
			}
			printf("[%s][%s][%x]%s%s%s%s%s%s, %u bytes\n", msg->topic_name, msg->source, msg->sequence_number,
				   msgidstr, indexstr,
				   ((msg->flags & LBM_MSG_FLAG_UME_RETRANSMIT) ? "-RX-" : ""), 
				   ((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""), 
				   ((msg->flags & LBM_MSG_FLAG_UMQ_REASSIGNED) ? "-RA-" : ""),
				   ((msg->flags & LBM_MSG_FLAG_UMQ_RESUBMITTED)   ? "-RS-" : ""),
				   (unsigned) msg->len);
			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		if (opts->verify) {
			int rc = verify_msg(msg->data, msg->len, opts->verbose);
			if (rc == 0)
			{
				printf("Message sqn %x does not verify!\n", msg->sequence_number);
			}
			else if (rc == -1)
			{
				fprintf(stderr, "Message sqn %x is not a verifiable message.\n", msg->sequence_number);
				fprintf(stderr, "Use -V option on source and restart receiver.\n");
				exit(1);
			}
			else
			{
				if (opts->verbose)
				{
					printf("Message sqn %x verifies\n", msg->sequence_number);
				}
			}
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		unrec_count++;
		total_unrec_count++;
		if (opts->verbose) {
			printf("[%s][%s][%x], LOST\n", msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		if (opts->verbose) {
			printf("[%s][%s][%x], LOST BURST\n", msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_REQUEST:
		/* Request message received (no response processed here) */
		(stotal_msg_count == 0) ? current_tv (&data_start_tv) : current_tv(&data_end_tv);
		msg_count++;
		total_msg_count++;
		stotal_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		total_byte_count += msg->len;
		break;
	case LBM_MSG_BOS:
		printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
		printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		subtotal_msg_count = 0;

		/* When verifying sequence numbers, multiple sources or EOS and new sources will cause
		 * the verification to fail as we don't track the numbers on a per source basis.
		 */
		if (opts->end_on_end) {
			if (opts->dereg) {
				printf("De-Registering from all Queues\n");
				if (lbm_rcv_umq_deregister(rcv, NULL) == LBM_FAILURE) {
					fprintf(stderr, "lbm_rcv_umq_deregister: %s\n", lbm_errmsg());
					exit(1);
				}
				deregistering = 1;
			} else {
				close_recv = 1;
			}
		}
		break;
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		printf("[%s], no sources found for topic\n", msg->topic_name);
		break;
	case LBM_MSG_UMQ_REGISTRATION_ERROR:
		printf("[%s][%s] UMQ registration error: %s\n", msg->topic_name, msg->source, msg->data);
		exit(0);
		break;
	case LBM_MSG_UMQ_REGISTRATION_COMPLETE_EX:
		{
			lbm_msg_umq_registration_complete_ex_t *reg = (lbm_msg_umq_registration_complete_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (reg->flags & LBM_MSG_UMQ_REGISTRATION_COMPLETE_EX_FLAG_ULB)
				type = "ULB";

			printf("[%s][%s] %s \"%s\"[%x] registration complete. AssignID %x. Flags %x ", msg->topic_name, msg->source, type, reg->queue, reg->queue_id, 
				reg->assignment_id, reg->flags);
			if (reg->flags & LBM_MSG_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)
				printf("QUORUM ");
			printf("\n");
			if (opts->reserve_index) {
				if (lbm_rcv_umq_index_reserve(rcv, NULL, opts->reserve_specific_index ? &(opts->index) : NULL) == LBM_FAILURE) {
					fprintf(stderr, "lbm_rcv_umq_index_reserve: %s\n", lbm_errmsg());
					exit(1);
				}
			}
		}
		break;
	case LBM_MSG_UMQ_DEREGISTRATION_COMPLETE_EX:
		{
			lbm_msg_umq_deregistration_complete_ex_t *reg = (lbm_msg_umq_deregistration_complete_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (reg->flags & LBM_MSG_UMQ_DEREGISTRATION_COMPLETE_EX_FLAG_ULB)
				type = "ULB";

			printf("[%s][%s] %s \"%s\"[%x] deregistration complete. Flags %x ", msg->topic_name, msg->source, type, reg->queue, reg->queue_id, reg->flags);
			printf("\n");
			close_recv = 1;
		}
		break;
	case LBM_MSG_UMQ_INDEX_ASSIGNMENT_ELIGIBILITY_ERROR:
		printf("[%s][%s] UMQ index assignment eligibility error: %s\n", msg->topic_name, msg->source, msg->data);
		break;
	case LBM_MSG_UMQ_INDEX_ASSIGNMENT_ERROR:
		printf("[%s][%s] UMQ index assignment error: %s\n", msg->topic_name, msg->source, msg->data);
		break;
	case LBM_MSG_UMQ_INDEX_ASSIGNMENT_ELIGIBILITY_START_COMPLETE_EX:
		{
			lbm_msg_umq_index_assignment_eligibility_start_complete_ex_t *ias = (lbm_msg_umq_index_assignment_eligibility_start_complete_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (ias->flags & LBM_MSG_UMQ_INDEX_ASSIGNMENT_ELIGIBILITY_START_COMPLETE_EX_FLAG_ULB)
				type = "ULB";

			printf("[%s][%s] %s \"%s\"[%x] index assignment eligibility start complete. Flags %x\n", msg->topic_name, msg->source, type, ias->queue, ias->queue_id, ias->flags);
		}
		break;
	case LBM_MSG_UMQ_INDEX_ASSIGNMENT_ELIGIBILITY_STOP_COMPLETE_EX:
		{
			lbm_msg_umq_index_assignment_eligibility_stop_complete_ex_t *ias = (lbm_msg_umq_index_assignment_eligibility_stop_complete_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (ias->flags & LBM_MSG_UMQ_INDEX_ASSIGNMENT_ELIGIBILITY_STOP_COMPLETE_EX_FLAG_ULB)
				type = "ULB";

			printf("[%s][%s] %s \"%s\"[%x] index assignment eligibility stop complete. Flags %x\n", msg->topic_name, msg->source, type, ias->queue, ias->queue_id, ias->flags);
		}
		break;
	case LBM_MSG_UMQ_INDEX_ASSIGNED_EX:
		{
			lbm_msg_umq_index_assigned_ex_t *ia = (lbm_msg_umq_index_assigned_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (ia->flags & LBM_MSG_UMQ_INDEX_ASSIGNED_EX_FLAG_ULB)
				type = "ULB";

			if (ia->index_info.flags & LBM_UMQ_INDEX_FLAG_NUMERIC)
				snprintf(indexstr, sizeof(indexstr), "%" PRIu64, *(lbm_uint64_t *)(&(ia->index_info.index)));
			else
				snprintf(indexstr, sizeof(indexstr), "\"%s\"", ia->index_info.index);

			printf("[%s][%s] %s \"%s\"[%x] beginning of index assignment for index %s. Flags %x\n", msg->topic_name, msg->source, type, ia->queue, ia->queue_id, indexstr, ia->flags);
		}
		break;
	case LBM_MSG_UMQ_INDEX_RELEASED_EX:
		{
			lbm_msg_umq_index_released_ex_t *ir = (lbm_msg_umq_index_released_ex_t *)(msg->data);
			const char *type = "UMQ";

			if (ir->flags & LBM_MSG_UMQ_INDEX_RELEASED_EX_FLAG_ULB)
				type = "ULB";

			if (ir->index_info.flags & LBM_UMQ_INDEX_FLAG_NUMERIC)
				snprintf(indexstr, sizeof(indexstr), "%" PRIu64, *(lbm_uint64_t *)(&(ir->index_info.index)));
			else
				snprintf(indexstr, sizeof(indexstr), "\"%s\"", ir->index_info.index);

			printf("[%s][%s] %s \"%s\"[%x] end of index assignment for index %s. Flags %x\n", msg->topic_name, msg->source, type, ir->queue, ir->queue_id, indexstr, ir->flags);

		}
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

#if !defined(_WIN32)
static int LossRate = 0;

static
void
SigHupHandler(int signo)
{
	if (LossRate >= 100)
	{
		return;
	}
	LossRate += 5;
	if (LossRate > 100)
	{
		LossRate = 100;
	}
	lbm_set_lbtrm_loss_rate(LossRate);
	lbm_set_lbtru_loss_rate(LossRate);
}

static
void
SigUsr1Handler(int signo)
{
	if (LossRate >= 100)
	{
		return;
	}
	LossRate += 10;
	if (LossRate > 100)
	{
		LossRate = 100;
	}
	lbm_set_lbtrm_loss_rate(LossRate);
	lbm_set_lbtru_loss_rate(LossRate);
}

static
void
SigUsr2Handler(int signo)
{
	LossRate = 0;
	lbm_set_lbtrm_loss_rate(LossRate);
	lbm_set_lbtru_loss_rate(LossRate);
}

static
void 
SigIntHandler(int signo)
{
	if (close_int) {
		force_close_recv = 1;
	} else {
		close_int = 1;
	}
}
#else
BOOL WINAPI
SigIntHandler(DWORD ControlType)
{
	switch (ControlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		if (close_int) {
			force_close_recv = 1;
		} else {
			close_int = 1;
		}
		return TRUE;
	default:
		break;
	}
	return FALSE;
}
#endif

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

void process_cmdline(int argc, char **argv, struct Options *opts)
{
	int c, errflag = 0;

	/* Set default option values */
	memset(opts, 0, sizeof(*opts));
	opts->max_sources = DEFAULT_NUM_SRCS;
	opts->conffname[0] = '\0';
	opts->transport_options_string[0] = '\0';
	opts->format_options_string[0] = '\0';
	opts->application_id_string[0] = '\0';
	opts->rcv_type_id = 0;
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();
	opts->delay = 0;

	/* Process the command line options, setting local variables with values */
	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case 'A':
				opts->ascii++;
				break;
			case 'c':
				strncpy(opts->conffname, optarg, sizeof(opts->conffname));
				break;
			case 'E':
				opts->end_on_end = 1;
				break;
			case 'D':
				opts->dereg = 1;
				break;
			case 'd':
				opts->delay = atoi(optarg);
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			case 'I':
				opts->rcv_type_id = atoi(optarg);
				break;
			case 'r':
				opts->msg_limit = atoi(optarg);
				break;
			case 's':
				opts->stats_ivl = atoi(optarg);
				break;
			case 'S':
				opts->end_on_end = 1;
				opts->summary = 1;
				break;
			case 'v':
				opts->verbose++;
				break;
			case 'V':
				opts->verify = 1;
				break;
			case 'X':
			{
				if (optarg != NULL) {
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
						printf("Will attempt to reserve index \"%s\".\n", opts->index.index);
					}
					opts->reserve_specific_index = 1;
				}
				else {
					printf("Will reserve a random index.\n");
				}
				opts->reserve_index = 1;
			}
				break;
			case OPTION_MONITOR_CTX:
				opts->monitor_context = 1;
				opts->monitor_context_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_RCV:
				opts->monitor_receiver = 1;
				opts->monitor_receiver_ivl = atoi(optarg);
				break;
			case OPTION_MAX_SOURCES:
				opts->max_sources = atoi(optarg);
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
	int opmode, opmode_seq = 0; /* flag for sequential mode - from config file */
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_rcv_t *rcv;
	lbm_context_attr_t * ctx_attr;
	lbm_rcv_topic_attr_t * rcv_attr;
	unsigned short int request_port;
	int request_port_bound;
	size_t optlen;
	lbm_ipv4_address_mask_t unicast_target_iface;
	struct in_addr inaddr;
	lbmmon_sctl_t * monctl;
	struct timeval stattv;
	lbm_context_event_func_t ctx_event_func;

	double total_time = 0.0;
	double total_mps = 0.0;
	double total_bps = 0.0;
	lbm_ulong_t lost_tmp;
	int have_stats = 0, set_nstats;
	int i;

#ifdef __VOS__
	set_rr_scheduling();	/* set round-robin thread scheduling policy */
#endif

#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;

		/* Windows socket setup code */
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

	nstats = opts->max_sources;
	/* Allocate array for statistics */
	stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
	if (stats == NULL)
	{
		fprintf(stderr, "can't allocate statistics array\n");
		exit(1);
	}
	
	/* Initialize logging callback */
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Retrieve current context settings */
	if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
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
		if (lbm_context_attr_str_getopt(ctx_attr, "context_name", ctx_name, &ctx_name_len) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_str_getopt - context_name: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_context_attr_set_from_xml(ctx_attr, ctx_name) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_set_from_xml - context_name: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/* Retrieve operational mode setting from context attribute structure */
	optlen = sizeof(opmode);
	if (lbm_context_attr_getopt(ctx_attr, "operational_mode", &opmode, &optlen) == LBM_FAILURE) {
		fprintf(stderr,"lbm_context_attr_str_getopt(operational_mode): %s\n", lbm_errmsg());
		exit(1);
	}
	if (opmode == LBM_CTX_ATTR_OP_SEQUENTIAL) {
		/*
		 * Operational mode is set to "sequential" meaning that LBM
		 * processing will not be handled in a separate thread. (See below.)
		 */
		opmode_seq = 1;
		printf("Sequential mode enabled.\n");
	}
	/* set the UMQ event callback so we get some additional event data */
	ctx_event_func.func = handle_ctx_event;
	ctx_event_func.evq = NULL;
	ctx_event_func.clientd = NULL;
	if (lbm_context_attr_setopt(ctx_attr, "context_event_function", &ctx_event_func, sizeof(ctx_event_func)) != 0) {
		fprintf(stderr, "lbm_context_str_setopt:context_event_function: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Create LBM context according to given attribute structure */
	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	/*
	 * Check settings to determine the TCP target for immediate messages.
	 * It might be appropriate to communicate this back to the source
	 * as a message.
	 */
	optlen = sizeof(request_port_bound);
	if (lbm_context_getopt(ctx,
				"request_tcp_bind_request_port",
				&request_port_bound,
				&optlen) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_context_getopt(request_tcp_bind_request_port): %s\n",
				lbm_errmsg());
		exit(1);
	}
	if (request_port_bound == 1) {
		optlen = sizeof(request_port);
		if (lbm_context_getopt(ctx,
				       "request_tcp_port",
				       &request_port,
				       &optlen) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_getopt(request_tcp_port): %s\n",
					lbm_errmsg());
			exit(1);
		}
		optlen = sizeof(unicast_target_iface);
		if (lbm_context_getopt(ctx,
				       "request_tcp_interface",
				       &unicast_target_iface,
				       &optlen) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_getopt(request_tcp_interface): %s\n",
					lbm_errmsg());
			exit(1);
		}
		/* if the request_tcp_interface is INADDR_ANY, get one we know is good. */
		if(unicast_target_iface.addr == INADDR_ANY) {
			if (lbm_context_getopt(ctx,
					       "resolver_multicast_interface",
					       &unicast_target_iface,
					       &optlen) == LBM_FAILURE) {
				fprintf(stderr, "lbm_context_getopt(resolver_multicast_interface): %s\n",
						lbm_errmsg());
				exit(1);
			}
		}
		inaddr.s_addr = unicast_target_iface.addr;
		printf("Immediate messaging target: TCP:%s:%d\n", inet_ntoa(inaddr),
			   ntohs(request_port));
	} else {
		printf("Request port binding disabled, no immediate messaging target.\n");
	}
#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
	signal(SIGINT, SigIntHandler);
#else
	SetConsoleCtrlHandler(SigIntHandler, TRUE);
#endif

	/* Initialize immediate message handler (for topicless immediate sends) */
	if (lbm_context_rcv_immediate_msgs(ctx, rcv_handle_immediate_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_rcv_immediate_msgs: %s\n", lbm_errmsg());
		exit(1);
	}
	
	/* delay creating the receiver if asked to */
	if (opts->delay > 0) {
		printf("Delaying receiver creation for %d seconds\n", opts->delay);
		SLEEP_SEC(opts->delay);
	}

	/* init rcv topic attributes */
	if (lbm_rcv_topic_attr_create(&rcv_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}

	/* if setting umq_receiver_type_id, then set it */
	if (opts->rcv_type_id != 0) {
		if (lbm_rcv_topic_attr_setopt(rcv_attr, "umq_receiver_type_id", &opts->rcv_type_id, sizeof(opts->rcv_type_id)) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_attr_setopt:umq_receiver_type_id: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* Lookup desired topic */
	if (lbm_rcv_topic_lookup(&topic, ctx, opts->topic, rcv_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_rcv_topic_attr_delete(rcv_attr);

#if !defined(_WIN32)
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif
	/*
	 * Create receiver passing in the looked up topic info and the message
	 * handler callback.
	 */
	if (lbm_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if (opts->monitor_context || opts->monitor_receiver)
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
			if (lbmmon_rcv_monitor(monctl, rcv, application_id, opts->monitor_receiver_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_rcv_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
	}

	if ( opts->stats_ivl ) {
		current_tv ( &stattv );
		stattv.tv_sec += opts->stats_ivl;
	}
	while (1) {
		struct timeval starttv, endtv;
		int flPrintStats = 0;

		current_tv(&starttv);

		if (opmode_seq) {
			/*
			 * Run LBM context processing for 1 second if sequential
			 * mode is enabled.
			 */
			if (lbm_context_process_events(ctx, 1000) == LBM_FAILURE) {
				fprintf(stderr, "lbm_context_process_events: %s\n", lbm_errmsg());
				exit(1);
			}
		} else {
			/*
			 * Otherwise, just sleep for 1 second. LBM processing is
			 * done in its own thread.
			 */
			SLEEP_SEC(1);
		}

		/* Retrieve receiver stats */
		have_stats = 0;
		while (!have_stats){
			set_nstats = nstats;
			if (lbm_rcv_retrieve_all_transport_stats(rcv, &set_nstats, stats) == LBM_FAILURE){
				/* Double the number of stats passed to the API to be retrieved */
				/* Do so until we retrieve stats successfully or hit the max limit */
				nstats *= 2;
				if (nstats > DEFAULT_MAX_NUM_SRCS){
					fprintf(stderr, "Cannot retrieve all receiver stats (%s). Maximum number of sources = %d.\n",
							lbm_errmsg(), DEFAULT_MAX_NUM_SRCS);
					exit(1);
				}
				stats = (lbm_rcv_transport_stats_t *)realloc(stats,  nstats * sizeof(lbm_rcv_transport_stats_t));
				if (stats == NULL){
					fprintf(stderr, "Cannot reallocate statistics array\n");
					exit(1);
				}
			}
			else{
				have_stats = 1;
			}
		}
		
		lost = 0;
		for (i = 0; i < set_nstats; i++)
		{
			switch (stats[i].type) {
			case LBM_TRANSPORT_STAT_LBTRM:
				lost += stats[i].transport.lbtrm.lost;
				break;
			case LBM_TRANSPORT_STAT_LBTRU:
				lost += stats[i].transport.lbtru.lost;
				break;
			}
		}
		lost_tmp = lost;
		if (last_lost <= lost)
			lost -= last_lost;
		else
			lost = 0;
		last_lost = lost_tmp;

		current_tv(&endtv);

		if ( opts->stats_ivl )
			flPrintStats = ( ( endtv.tv_sec > stattv.tv_sec ) ||
							 ( endtv.tv_sec == stattv.tv_sec && endtv.tv_usec >= stattv.tv_usec ) ) ? 1 : 0;

		endtv.tv_sec -= starttv.tv_sec;
		endtv.tv_usec -= starttv.tv_usec;
		normalize_tv(&endtv);

		if (!opts->ascii)
			print_bw(stdout, &endtv, msg_count,
					byte_count, unrec_count, lost, rx_msg_count, otr_msg_count);
		if ( flPrintStats ) {
 			print_stats(stdout, stats, set_nstats);
			current_tv ( &stattv );
			stattv.tv_sec += opts->stats_ivl;
		}

		/*
		 * Get rid of receiver if we've received all we we
		 * wanted or if the sender has already gone away.
		 */
		if (((opts->msg_limit > 0 && total_msg_count >= opts->msg_limit) || (close_int)) && !deregistering) {
			if (opts->dereg) {
				printf("De-Registering from all Queues\n");
				if (lbm_rcv_umq_deregister(rcv, NULL) == LBM_FAILURE) {
					fprintf(stderr, "lbm_rcv_umq_deregister: %s\n", lbm_errmsg());
					exit(1);
				}
				deregistering = 1;
			} else {
				close_recv = 1;
			}
		}
		if (force_close_recv) {
			printf("Forcing shutdown\n");
			close_recv = 1;
		}
		if (close_recv) {
			lbm_rcv_delete(rcv);
			rcv = NULL;
		}
		msg_count = 0;
		byte_count = 0;
		unrec_count = 0;
		if (rcv == NULL)
			break;
	}

	if (opts->summary) {
		total_time = ((double)data_end_tv.tv_sec + (double)data_end_tv.tv_usec / 1000000.0)
						- ((double)data_start_tv.tv_sec + (double)data_start_tv.tv_usec / 1000000.0);
		printf ("\nTotal time        : %-5.4g sec\n", total_time);
		printf ("Messages received : %u\n", stotal_msg_count);
		printf ("Reassigned        : %u\n", total_reassign_count);
		printf ("Resubmitted       : %u\n", total_resub_count);
#if defined(_WIN32)
		printf ("Bytes received    : %I64d\n", total_byte_count);
#else
		printf ("Bytes received    : %lld\n", total_byte_count);
#endif
		if (total_time > 0) {
			total_mps = (double)total_msg_count/total_time;
			total_bps = (double)total_byte_count*8/total_time;
			printf ("Avg. throughput   : %-5.4g Kmsgs/sec, %-5.4g Mbps\n\n", total_mps/1000.0, total_bps/1000000.0);
		}

	} else {
		printf("Quitting.... received %u messages (%u reassigned, %u resubmitted)\n", total_msg_count, total_reassign_count, total_resub_count);
	}

	if (opts->monitor_context || opts->monitor_receiver)
	{
		if (opts->monitor_context)
		{
			if (lbmmon_context_unmonitor(monctl, ctx) == -1)
			{
				fprintf(stderr, "lbmmon_context_unmonitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		else
		{
			if (rcv != NULL)
			{
				if (lbmmon_rcv_unmonitor(monctl, rcv) == -1)
				{
					fprintf(stderr, "lbmmon_rcv_unmonitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			}
		}
		if (lbmmon_sctl_destroy(monctl) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_destoy() failed(), %s\n", lbmmon_errmsg());
			exit(1);
		}
	}

	SLEEP_SEC(5);

	/* Delete LBM context (not strictly necessary in this example) */
	lbm_context_delete(ctx);
	if (stats != NULL)
		free(stats);
	return 0;
}

