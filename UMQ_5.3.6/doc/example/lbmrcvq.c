/*
"lbmrcvq.c: application that receives messages from a given topic
"  (single receiver) using an event queue.

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

static const char *rcsid_example_lbmrcvq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmrcvq.c#2 $";

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

const char Purpose[] = "Purpose: Receive messages from a single topic using an event queue.";
char Usage[] =
"Usage: %s [options] topic\n"
"Available options:\n"
"  -c, --config=FILE     Use LBM configuration file FILE.\n"
"  -C, --context-stats   fetch context rather than receiver stats\n"
"                        Multiple config files are allowed.\n"
"                        Example:  '-c file1.cfg -c file2.cfg'\n"
"  -E, --exit            exit after source ends\n"
"  -h, --help            display this help and exit\n"
"  -r NUM                delete receiver after NUM messages\n"
"  -s, --stats=NUM       print LBM statistics every NUM seconds\n"
"  -S, --stop            exit after source ends, print throughput summary\n"
"  -v, --verbose         be verbose about incoming messages (-v -v = be even more verbose)\n"
"  -V, --verify          verify message contents\n"
MONOPTS_RECEIVER
MONOPTS_EVENT_QUEUE
MONMODULEOPTS_SENDER;

const char * OptionString = "c:CEhSs:r:vV";
#define OPTION_MONITOR_RCV 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
#define OPTION_MONITOR_EVQ 7
#define OPTION_MAX_SOURCES 8
const struct option OptionTable[] =
{
	{ "config", required_argument, NULL, 'c' },
	{ "context-stats", no_argument, NULL, 'C' },
	{ "exit", no_argument, NULL, 'E' },
	{ "help", no_argument, NULL, 'h' },
	{ "stop", no_argument, NULL, 'S' },
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
	{ "monitor-evq", required_argument, NULL, OPTION_MONITOR_EVQ },
	{ NULL, 0, NULL, 0 }
};

struct Options {
	int ascii;          /* Flag to display messages as ASCII text */
	int context_stats;  /* Flag to fetch context rather than receiver stats */
	int end_on_end;     /* Flag to end program when source stops sending */
	int msg_limit;      /* Limit on number of messages to receive */
	int stats_ivl;      /* Interval for dumping statistics, in seconds */
	int summary;        /* Flag to display a summary when source stops */
	int verbose;        /* Flag to control program verbosity */
	int verify_msgs;    /* Flag to control message verification (verifymsg.h) */

	int max_sources;						/* Maximum number of source statistics to display */
	char transport_options_string[1024];    /* Transport options given to lbmmon_sctl_create() */
	char format_options_string[1024];       /* Format options given to lbmmon_sctl_create() */
	char application_id_string[1024];       /* Application ID given to lbmmon_context_monitor() */
	int monitor_context;                    /* Flag to control context level monitoring */
	int monitor_context_ivl;                /* Interval for context level monitoring */
	int monitor_receiver;                   /* Flag to control receiver level monitoring */
	int monitor_receiver_ivl;               /* Interval for receiver level monitoring */
	int monitor_event_queue;                /* Flag to control event queue monitoring */
	int monitor_event_queue_ivl;            /* Interval for event queue monitoring */
	lbmmon_transport_func_t * transport;    /* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;          /* Function pointer to chosen format module */
	char *topic;                            /* The topic on which messages will be received */
} options;

#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10

int msg_count = 0, total_msg_count = 0, byte_count = 0;
int rx_msg_count = 0;
int otr_msg_count = 0;
int stotal_msg_count = 0;
#if defined(_WIN32)
signed __int64 total_byte_count = 0;
#else
unsigned long long total_byte_count = 0;
#endif /* _WIN32 */
int close_recv = 0;
struct timeval starttv, endtv;
int timer_id = -1;
lbm_context_t *ctx = NULL;
lbm_event_queue_t *evq = NULL;
int unrec_count = 0;
int burst_loss = 0;

struct timeval data_start_tv;
struct timeval data_end_tv;
struct timeval stattv; 				/* to track time between printing LBM transport stats */

lbm_ulong_t lost = 0, last_lost = 0;
lbm_rcv_transport_stats_t *stats = NULL;
int nstats;


/* For the elapsed time, calculate and print the msgs/sec and bits/sec */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned int bytes, lbm_ulong_t lost)
{
	char scale[] = {'\0', 'K', 'M', 'G'};
	int msg_scale_index = 0, bit_scale_index = 0;
	double sec = 0.0, mps = 0.0, bps = 0.0;
	double kscale = 1000.0;
	
	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */	
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	bps = (double)bytes*8/sec;
	
	while (mps >= kscale) {
		mps /= kscale;
		msg_scale_index++;
	}
	
	while (bps >= kscale) {
		bps /= kscale;
		bit_scale_index++;
	}
	
	if ((rx_msg_count > 0) || (otr_msg_count > 0))
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rx_msg_count, otr_msg_count);
	else
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
	
	if (lost > 0 || burst_loss > 0 || unrec_count > 0 ){
		fprintf(fp, " [%lu pkts lost, %u msgs unrecovered, %d loss bursts]",
			lost, unrec_count, burst_loss);
	}
	fprintf(fp, "\n");
	fflush(fp);
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_rcv_transport_stats_t stats)
{
	switch (stats.type) {
	case LBM_TRANSPORT_STAT_TCP:
		fprintf(fp, " [%s] received %lu msgs/%lu bytes, %lu no topics, %lu requests\n",
				stats.source,
				stats.transport.tcp.lbm_msgs_rcved,
				stats.transport.tcp.bytes_rcved,
				stats.transport.tcp.lbm_msgs_no_topic_rcved,
				stats.transport.tcp.lbm_reqs_rcved);
		break;
	case LBM_TRANSPORT_STAT_LBTRM:
		{
			char stmstr[256] = "", txstr[256] = "";

			if (stats.transport.lbtrm.nak_tx_max > 0) {
				/* we usually don't use sprintf, but should be OK here for the moment. */
				sprintf(stmstr, "Recovery time: %lu min/%lu mean/%lu max. ",
						stats.transport.lbtrm.nak_stm_min,
						stats.transport.lbtrm.nak_stm_mean,
						stats.transport.lbtrm.nak_stm_max);
				sprintf(txstr, "Tx per NAK: %lu min/%lu mean/%lu max. ",
						stats.transport.lbtrm.nak_tx_min,
						stats.transport.lbtrm.nak_tx_mean,
						stats.transport.lbtrm.nak_tx_max);
			}
			fprintf(fp, " [%s] Received %lu msgs/%lu bytes/%lu dups/%lu lost. "
						"Unrecovered: %lu (window advance) + %lu (timeout). "
						"%s"
						"NAKs: %lu (%lu packets). "
						"%s"
						"NCFs: %lu ignored/%lu shed/%lu rx delay/%lu unknown. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats.source,
						stats.transport.lbtrm.msgs_rcved,
						stats.transport.lbtrm.bytes_rcved,
						stats.transport.lbtrm.duplicate_data,
						stats.transport.lbtrm.lost,
						stats.transport.lbtrm.unrecovered_txw,
						stats.transport.lbtrm.unrecovered_tmo,
						stmstr,
						stats.transport.lbtrm.naks_sent,
						stats.transport.lbtrm.nak_pckts_sent,
						txstr,
						stats.transport.lbtrm.ncfs_ignored,
						stats.transport.lbtrm.ncfs_shed,
						stats.transport.lbtrm.ncfs_rx_delay,
						stats.transport.lbtrm.ncfs_unknown,
						stats.transport.lbtrm.lbm_msgs_rcved,
						stats.transport.lbtrm.lbm_msgs_no_topic_rcved,
						stats.transport.lbtrm.lbm_reqs_rcved);
		}
		break;
	case LBM_TRANSPORT_STAT_LBTRU:
		{
			char stmstr[256] = "", txstr[256] = "";

			if (stats.transport.lbtru.nak_tx_max > 0) {
				/* we usually don't use sprintf, but should be OK here for the moment. */
				sprintf(stmstr, "Recovery time: %lu min/%lu mean/%lu max. ",
						stats.transport.lbtru.nak_stm_min,
						stats.transport.lbtru.nak_stm_mean,
						stats.transport.lbtru.nak_stm_max);
				sprintf(txstr, "Tx per NAK: %lu min/%lu mean/%lu max. ",
						stats.transport.lbtru.nak_tx_min,
						stats.transport.lbtru.nak_tx_mean,
						stats.transport.lbtru.nak_tx_max);
			}
			fprintf(fp, " [%s] Received %lu msgs/%lu bytes/%lu dups/%lu lost. "
						"Unrecovered: %lu (window advance) + %lu (timeout). "
						"%s"
						"NAKs: %lu (%lu packets). "
						"%s"
						"NCFs: %lu ignored/%lu shed/%lu rx delay/%lu unknown. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats.source,
						stats.transport.lbtru.msgs_rcved,
						stats.transport.lbtru.bytes_rcved,
						stats.transport.lbtru.duplicate_data,
						stats.transport.lbtru.lost,
						stats.transport.lbtru.unrecovered_txw,
						stats.transport.lbtru.unrecovered_tmo,
						stmstr,
						stats.transport.lbtru.naks_sent,
						stats.transport.lbtru.nak_pckts_sent,
						txstr,
						stats.transport.lbtru.ncfs_ignored,
						stats.transport.lbtru.ncfs_shed,
						stats.transport.lbtru.ncfs_rx_delay,
						stats.transport.lbtru.ncfs_unknown,
						stats.transport.lbtru.lbm_msgs_rcved,
						stats.transport.lbtru.lbm_msgs_no_topic_rcved,
						stats.transport.lbtru.lbm_reqs_rcved);
		}
		break;
	case LBM_TRANSPORT_STAT_LBTIPC:
		{
			fprintf(fp, " [%s] Received %lu msgs/%lu bytes. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats.source,
						stats.transport.lbtipc.msgs_rcved,
						stats.transport.lbtipc.bytes_rcved,
						stats.transport.lbtipc.lbm_msgs_rcved,
						stats.transport.lbtipc.lbm_msgs_no_topic_rcved,
						stats.transport.lbtipc.lbm_reqs_rcved);
		}
		break;
	case LBM_TRANSPORT_STAT_LBTRDMA:
		{
			fprintf(fp, " [%s] Received %lu msgs/%lu bytes. "
						"%lu LBM msgs, %lu no topics, %lu requests.\n",
						stats.source,
						stats.transport.lbtrdma.msgs_rcved,
						stats.transport.lbtrdma.bytes_rcved,
						stats.transport.lbtrdma.lbm_msgs_rcved,
						stats.transport.lbtrdma.lbm_msgs_no_topic_rcved,
						stats.transport.lbtrdma.lbm_reqs_rcved);
		}
		break;
	default:
		break;
	}
	fflush(fp);
}

/* Utility to print contents of buffer in hex/ASCII format */
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

/* Logging callback function (passed into lbm_log()) */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

/* Event queue monitor callback (passed into lbm_event_queue_create()) */
int evq_monitor(lbm_event_queue_t *evq, int event, size_t evq_size,
				lbm_ulong_t event_delay_usec, void *clientd)
{
	printf("event queue threshold exceeded - event %x, sz %u, delay %lu\n",
			event, evq_size, event_delay_usec);
	return 0;
}

/*
 * Immediate received message handler
 * (passed into lbm_context_rcv_immediate_msgs())
 */
int rcv_handle_immediate_msg(lbm_context_t *ctx, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;

	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Immediate data message received */
		msg_count++;
		total_msg_count++;
		byte_count += msg->len;
		if (opts->verbose) {
			printf("IM [%s][%u], %u bytes\n",
					msg->source, msg->sequence_number, msg->len);
			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_REQUEST:
		msg_count++;
		total_msg_count++;
		byte_count += msg->len;
		if (opts->verbose) {
			printf("IM Request [%s][%u], %u bytes\n",
					msg->source, msg->sequence_number, msg->len);
			if (opts->verbose > 1) 
				dump(msg->data, msg->len);
		}
		break;
	default:
		printf("Unknown immediate message lbm_msg_t type %x [%s]\n", msg->type, msg->source);
		break;
	}
	if (opts->msg_limit > 0 && total_msg_count > opts->msg_limit) {
		if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* Received message handler (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;

	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Data message received (associated with desired topic) */
		(stotal_msg_count == 0) ? current_tv (&data_start_tv) : current_tv(&data_end_tv);
		msg_count++;
		total_msg_count++;
		stotal_msg_count++;
		byte_count += msg->len;
		total_byte_count += msg->len;

		if (msg->flags & LBM_MSG_FLAG_RETRANSMIT)
			rx_msg_count++;
		if (msg->flags & LBM_MSG_FLAG_OTR)
			otr_msg_count++;

		if (opts->verify_msgs) {
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
		if (opts->verbose) {
			printf("[%s][%s][%u]%s%s, %u bytes\n",
					msg->topic_name, msg->source, msg->sequence_number,
					((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX-" : ""),
					((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""),
					msg->len);
			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_REQUEST:
		/* Request message received (no response processed here) */
		(stotal_msg_count == 0) ? current_tv (&data_start_tv) : current_tv(&data_end_tv);
		msg_count++;
		total_msg_count++;
		byte_count += msg->len;
		total_byte_count += msg->len;
		if (opts->verbose) {
			printf("[%s][%s][%u], Request\n",
					msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_BOS:
		printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
		printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		if (opts->end_on_end)
			close_recv = 1;
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		unrec_count++;
		if (opts->verbose) {
			printf("[%s][%s][%u], LOST\n",
					msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		if (opts->verbose) {
			printf("[%s][%s][%u], LOST BURST\n",
					msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	if ((opts->msg_limit > 0 && total_msg_count >= opts->msg_limit) || close_recv) {
		/*
		 * If we've received all that we wanted or the source has
		 * gone away, unblock the event queue dispatcher (forcing it
		 * to return).
		 */
		if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/*
 * Timer handler (passed into lbm_schedule_timer()) used to print bandwidth
 * usage stats once per second.
 */
int rcv_handle_tmo(lbm_context_t *ctx, const void *clientd)
{
	lbm_rcv_t *rcv = (lbm_rcv_t *) clientd; /* passed from main as client (i.e. user) data */
	struct Options *opts = &options;
	lbm_ulong_t lost_tmp;
	int flPrintStats = 0;
	int count = 0;
	int have_stats = 0, set_nstats;
	if (!opts->stats_ivl && opts->ascii)
		return 0;

	timer_id = -1;
	
	current_tv(&endtv);

	if (opts->stats_ivl) {
		flPrintStats = ((endtv.tv_sec > stattv.tv_sec) ||
			 (endtv.tv_sec == stattv.tv_sec && endtv.tv_usec >= stattv.tv_usec)) ? 1 : 0;
	}

	/* Retrieve either context or receiver stats */
	if (opts->context_stats)
	{
		while (!have_stats){
			set_nstats = nstats;
			if (lbm_context_retrieve_rcv_transport_stats(ctx, &set_nstats, stats) == LBM_FAILURE){
				/* Double the number of stats passed to the API to be retrieved */
				/* Do so until we retrieve stats successfully or hit the max limit */
				nstats *= 2;
				if (nstats > DEFAULT_MAX_NUM_SRCS){
					fprintf(stderr, "Cannot retrieve all context stats (%s).  Maximum number of sources = %d.\n",
							lbm_errmsg(), DEFAULT_MAX_NUM_SRCS);
					exit(1);
				}
				stats = (lbm_rcv_transport_stats_t *)realloc(stats,  nstats * sizeof(lbm_rcv_transport_stats_t));
			}
			else{
				have_stats = 1;
			}
		}
	}
	else
	{
		while (!have_stats){
			set_nstats = nstats;
			if (lbm_rcv_retrieve_all_transport_stats(rcv, &set_nstats, stats) == LBM_FAILURE){
				/* Double the number of stats passed to the API to be retrieved */
				/* Do so until we retrieve stats successfully or hit the max limit */
				nstats *= 2;
				if (nstats > DEFAULT_MAX_NUM_SRCS){
					fprintf(stderr, "Cannot retrieve all receiver stats (%s).  Maximum number of sources = %d.\n",
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
	}

	for (lost = 0, count = 0; count < set_nstats; count++)
	{
		if ( flPrintStats ) {
			if (nstats > 1)
				fprintf(stdout, "source %u/%u:", count+1, set_nstats);
			print_stats(stdout, stats[count]);
		}

		switch (stats[count].type) {
		case LBM_TRANSPORT_STAT_LBTRM:
			lost += stats[count].transport.lbtrm.lost;
			break;
		case LBM_TRANSPORT_STAT_LBTRU:
			lost += stats[count].transport.lbtru.lost;
			break;
		}
	}

	lost_tmp = lost;
	if (last_lost <= lost)
		lost -= last_lost;
	else
		lost = 0;
	last_lost = lost_tmp;

	if (!opts->ascii) {
		endtv.tv_sec -= starttv.tv_sec;
		endtv.tv_usec -= starttv.tv_usec;
		normalize_tv(&endtv);

		print_bw(stdout, &endtv, msg_count, byte_count, lost);
	}

	msg_count = 0;
	rx_msg_count = 0;
	otr_msg_count = 0;
	byte_count = 0;
	unrec_count = 0;

	if (flPrintStats) {
		current_tv(&stattv);
		stattv.tv_sec += opts->stats_ivl;
	}

	current_tv(&starttv);
	/* Restart timer */
	if (timer_id = lbm_schedule_timer(ctx, rcv_handle_tmo, rcv, evq, 1000) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
		exit(1);
	}
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
#endif

void process_cmdline(int argc, char **argv, struct Options *opts)
{
	int c, errflag = 0; 

	/* Set default option values */
	memset(opts, 0, sizeof(*opts));
	opts->max_sources = DEFAULT_NUM_SRCS;
	opts->transport_options_string[0] = '\0';
	opts->format_options_string[0] = '\0';
	opts->application_id_string[0] = '\0';
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case 'A':
				opts->ascii++;
				break;
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'C':
				opts->context_stats = 1;
				break;
			case 'E':
				opts->end_on_end = 1;
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
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
				opts->verify_msgs = 1;
				break;
			case OPTION_MAX_SOURCES:
				opts->max_sources = atoi(optarg);
				break;
			case OPTION_MONITOR_CTX:
				opts->monitor_context = 1;
				opts->monitor_context_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_RCV:
				opts->monitor_receiver = 1;
				opts->monitor_receiver_ivl = atoi(optarg);
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
			case OPTION_MONITOR_EVQ:
				opts->monitor_event_queue = 1;
				opts->monitor_event_queue_ivl = atoi(optarg);
				break;
			default:
				errflag++;
				break;
		}
	}
	if ((errflag != 0) || (optind == argc))
	{
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
	lbm_topic_t *topic;
	lbm_rcv_t *rcv;
	lbm_context_attr_t * ctx_attr;
	unsigned short int request_port;
	int request_port_bound;
	size_t optlen;
	lbm_ipv4_address_mask_t unicast_target_iface;
	struct in_addr inaddr;
	lbmmon_sctl_t * monctl;
	
	double total_time = 0.0;
	double total_mps = 0.0;
	double total_bps = 0.0;

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

	nstats = opts->max_sources;
	/* Allocate array for statistics */
	stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
	if (stats == NULL)
	{
		fprintf(stderr, "can't allocate statistics array\n");
		exit(1);
	}

	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Retrieve current context settings */
	if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Create LBM context */
	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	/* Create an event queue and associate it with a callback */
	if (lbm_event_queue_create(&evq, evq_monitor, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
		exit(1);
	}
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
	/*
	 * Create an immediate message receiver and associate it with
	 * a callback (for *topicless* immediate sends).
	 */
	if (lbm_context_rcv_immediate_msgs(ctx, rcv_handle_immediate_msg, NULL, evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_rcv_immediate_msgs: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Lookup desired topic */
	if (lbm_rcv_topic_lookup(&topic, ctx, argv[optind], NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/*
	 * Create a receiver associated the looked up topic and given callback.
	 */ 
	if (lbm_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if (opts->monitor_context || opts->monitor_receiver || opts->monitor_event_queue)
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
		if (opts->monitor_receiver)
		{
			if (lbmmon_rcv_monitor(monctl, rcv, application_id, opts->monitor_receiver_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_rcv_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		if (opts->monitor_event_queue)
		{
			if (lbmmon_evq_monitor(monctl, evq, application_id, opts->monitor_event_queue_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_evq_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
	}

	current_tv(&starttv);
	/* Start up timer to print bandwidth utilization stats every second. */
	if (timer_id = lbm_schedule_timer(ctx, rcv_handle_tmo, rcv, evq, 1000) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
		exit(1);
	}
	while (1)
	{
		/*
		 * Dispatch event queue (only returns upon error or when
		 * unblocked in one of our callbacks).
		 */
		if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch returned error.\n");
			break;
		}
		if ((opts->msg_limit > 0 && total_msg_count >= opts->msg_limit) || close_recv) {
			/*
			 * Delete receiver if we've gotten all we wanted or if
			 * the source has gone away.
			 */
			lbm_rcv_delete(rcv);
			rcv = NULL;
			break;
		}
	}

	if (opts->summary) {
		total_time = ((double)data_end_tv.tv_sec + (double)data_end_tv.tv_usec / 1000000.0) - ((double)data_start_tv.tv_sec + (double)data_start_tv.tv_usec / 1000000.0);
		printf ("\nTotal time        : %-5.4g sec\n", total_time);
		printf ("Messages received : %u\n", stotal_msg_count);
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
	}
	else
		printf("Quitting....\n");

	SLEEP_SEC(5);

	if (opts->monitor_context || opts->monitor_receiver || opts->monitor_event_queue)
	{
		if (opts->monitor_context)
		{
			if (lbmmon_context_unmonitor(monctl, ctx) == -1)
			{
				fprintf(stderr, "lbmmon_context_unmonitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		if (opts->monitor_receiver)
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
		if (opts->monitor_event_queue)
		{
			if (lbmmon_evq_unmonitor(monctl, evq) == -1)
			{
				fprintf(stderr, "lbmmon_evq_unmonitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		if (lbmmon_sctl_destroy(monctl) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_destoy() failed(), %s\n", lbmmon_errmsg());
			exit(1);
		}
	}

	/*
	 * Delete LBM context and event queue.
	 * The event queue is independent of the LBM context an so may be
	 * deleted after the context is deallocated.
	 */
	lbm_context_delete(ctx);
	lbm_event_queue_delete(evq);
	return 0;
}

