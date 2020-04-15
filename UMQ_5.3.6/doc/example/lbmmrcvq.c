/*
"lbmmrcvq.c: application that receives messages from a set of topics
"  (multiple receivers) using event queues.

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
	#define snprintf _snprintf
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <signal.h>
	#include <sys/time.h>
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

static const char *rcsid_example_lbmmrcvq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmrcvq.c#2 $";

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

const char Purpose[] = "Purpose: Receive messages from multiple topics using event queue.";
const char Usage[] =
"Usage: %s [options]\n"
"  -B, --bufsize=#          Set receive socket buffer size to # (in MB)\n"
"  -c, --config=FILE        Use LBM configuration file FILE.\n"
"                           Multiple config files are allowed.\n"
"                           Example:  '-c file1.cfg -c file2.cfg'\n"
"  -C, --contexts=NUM       use NUM lbm_context_t objects\n"
"  -h, --help               display this help and exit\n"
"  -i, --initial-topic=NUM  use NUM as initial topic number\n"
"  -L, --linger=NUM         linger for NUM seconds after done\n"
"  -r, --root=STRING        use topic names with root of STRING\n"
"  -R, --receivers=NUM      create NUM receivers\n"
"  -s, --statistics         print statistics along with bandwidth\n"
"  -v, --verbose            be verbose\n"
MONOPTS_RECEIVER
MONOPTS_EVENT_QUEUE
MONMODULEOPTS_SENDER;

const char * OptionString = "B:c:C:hi:L:r:R:sv";
#define OPTION_MONITOR_RCV 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
#define OPTION_MONITOR_EVQ 7
const struct option OptionTable[] =
{
	{ "bufsize", required_argument, NULL, 'B' },
	{ "config", required_argument, NULL, 'c' },
	{ "contexts", required_argument, NULL, 'C' },
	{ "help", no_argument, NULL, 'h' },
	{ "initial-topic", required_argument, NULL, 'i' },
	{ "linger", required_argument, NULL, 'L' },
	{ "root", required_argument, NULL, 'r' },
	{ "receivers", required_argument, NULL, 'R' },
	{ "statistics", no_argument, NULL, 's' },
	{ "verbose", no_argument, NULL, 'v' },
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

#define DEFAULT_MAX_MESSAGES 10000000
#define MAX_NUM_RCVS 100000
#define MAX_TOPIC_NAME_LEN 80
#define DEFAULT_NUM_RCVS 100
#define MAX_NUM_CTXS 4
#define DEFAULT_NUM_CTXS 1
#define DEFAULT_TOPIC_ROOT "29west.example.multi"
#define DEFAULT_INITIAL_TOPIC_NUMBER 0
#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10

lbm_event_queue_t *evq = NULL;
lbm_context_t *ctxs[MAX_NUM_CTXS];
lbm_rcv_t **rcvs = NULL;
int count = 0;
int linger = 10;
int msg_count = 0;
int byte_count = 0;
int unrec_count = 0;
int verbose = 0;
int pstats = 0;
int burst_loss = 0;
struct timeval starttv, endtv;
int rxs = 0;
int otrs = 0;
lbm_ulong_t lost = 0, last_lost = 0, lost_tmp = 0;
lbm_rcv_transport_stats_t * stats = NULL;
int nstats = DEFAULT_NUM_SRCS;
int num_rcvs = DEFAULT_NUM_RCVS;
int num_ctxs = DEFAULT_NUM_CTXS;

/*
 * For the elapsed time, calculate and print the msgs/sec and bits/sec as well
 * as any unrecoverable data.
 */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned int bytes, int lost)
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

	if ((rxs != 0) || (otrs != 0)){
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]", sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rxs, otrs);
	}
	else{ 
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps", sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
	}
		
	if (lost != 0 || unrec_count != 0 || burst_loss != 0) {
		fprintf(fp, " [%u pkts lost, %u msgs unrecovered, %d loss bursts]", lost, unrec_count, burst_loss);
	}

	fputs("\n",fp);
	fflush(fp);
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_rcv_transport_stats_t stats)
{
	switch (stats.type) {
	case LBM_TRANSPORT_STAT_TCP:
		fprintf(fp, " [%s], received %lu, LBM %lu/%lu/%lu\n",
				stats.source,stats.transport.tcp.bytes_rcved,
				stats.transport.tcp.lbm_msgs_rcved,
				stats.transport.tcp.lbm_msgs_no_topic_rcved,
				stats.transport.tcp.lbm_reqs_rcved);
		break;
	case LBM_TRANSPORT_STAT_LBTRM:
		{
			char stmstr[256] = "", txstr[256] = "";

			if (stats.transport.lbtrm.nak_tx_max > 0) {
				/* we usually don't use sprintf, but should be OK here for the moment. */
				sprintf(stmstr, ", nak stm %lu/%lu/%lu",
						stats.transport.lbtrm.nak_stm_min, stats.transport.lbtrm.nak_stm_mean,
						stats.transport.lbtrm.nak_stm_max);
				sprintf(txstr, ", nak tx %lu/%lu/%lu",
						stats.transport.lbtrm.nak_tx_min, stats.transport.lbtrm.nak_tx_mean,
						stats.transport.lbtrm.nak_tx_max);
			}
			fprintf(fp, " [%s], received %lu/%lu, dups %lu, loss %lu, naks %lu/%lu, ncfs %lu-%lu-%lu-%lu, unrec %lu/%lu%s%s\n",
					stats.source,
					stats.transport.lbtrm.msgs_rcved, stats.transport.lbtrm.bytes_rcved,
					stats.transport.lbtrm.duplicate_data,
					stats.transport.lbtrm.lost,
					stats.transport.lbtrm.naks_sent, stats.transport.lbtrm.nak_pckts_sent,
					stats.transport.lbtrm.ncfs_ignored, stats.transport.lbtrm.ncfs_shed,
					stats.transport.lbtrm.ncfs_rx_delay, stats.transport.lbtrm.ncfs_unknown,
					stats.transport.lbtrm.unrecovered_txw,
					stats.transport.lbtrm.unrecovered_tmo,
					stmstr, txstr);
		}
		break;
	case LBM_TRANSPORT_STAT_LBTRU:
		{
			char stmstr[256] = "", txstr[256] = "";

			if (stats.transport.lbtru.nak_tx_max > 0) {
				/* we usually don't use sprintf, but should be OK here for the moment. */
				sprintf(stmstr, ", nak stm %lu/%lu/%lu",
						stats.transport.lbtru.nak_stm_min, stats.transport.lbtru.nak_stm_mean,
						stats.transport.lbtru.nak_stm_max);
				sprintf(txstr, ", nak tx %lu/%lu/%lu",
						stats.transport.lbtru.nak_tx_min, stats.transport.lbtru.nak_tx_mean,
						stats.transport.lbtru.nak_tx_max);
			}
			fprintf(fp, " [%s], LBM %lu/%lu/%lu, received %lu/%lu, dups %lu, loss %lu, naks %lu/%lu, ncfs %lu-%lu-%lu-%lu, unrec %lu/%lu%s%s\n",
					stats.source,
					stats.transport.lbtru.lbm_msgs_rcved,
					stats.transport.lbtru.lbm_msgs_no_topic_rcved,
					stats.transport.lbtru.lbm_reqs_rcved,
					stats.transport.lbtru.msgs_rcved, stats.transport.lbtru.bytes_rcved,
					stats.transport.lbtru.duplicate_data,
					stats.transport.lbtru.lost,
					stats.transport.lbtru.naks_sent, stats.transport.lbtru.nak_pckts_sent,
					stats.transport.lbtru.ncfs_ignored, stats.transport.lbtru.ncfs_shed,
					stats.transport.lbtru.ncfs_rx_delay, stats.transport.lbtru.ncfs_unknown,
					stats.transport.lbtru.unrecovered_txw,
					stats.transport.lbtru.unrecovered_tmo,
					stmstr, txstr);
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

/*
* function to retrieve transport level loss or display transport level stats
* @num_ctx -- number of contexts
* @ctx -- contexts to retrieve transport stats for
* @print_flag -- if 1, display stats, retrieve loss otherwise
*/
lbm_ulong_t get_loss_or_print_stats(int num_ctxs, lbm_context_t * ctxs[], int print_flag){
	int ctx, nstat;
	lbm_ulong_t lost = 0;
	int have_stats, set_nstats;
	
	for (ctx = 0; ctx < num_ctxs; ctx++){
		have_stats = 0;
		while (!have_stats){
			set_nstats = nstats;
			if (lbm_context_retrieve_rcv_transport_stats(ctxs[ctx], &set_nstats, stats) == LBM_FAILURE){
				/* Double the number of stats passed to the API to be retrieved */
				/* Do so until we retrieve stats successfully or hit the max limit */
				nstats *= 2;
				if (nstats > DEFAULT_MAX_NUM_SRCS){
					fprintf(stderr, "Cannot retrieve all stats (%s).  Maximum number of sources = %d.\n",
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
		/* If we get here, we have the stats */
		for (nstat = 0; nstat < set_nstats; nstat++){
			if (print_flag){
				/* Display transport level stats */
				fprintf(stdout, "stats %u/%u (ctx %u):", nstat+1, set_nstats, ctx);
				print_stats(stdout, stats[nstat]);
			}
			else{
				/* Accumlate transport level loss */
				switch (stats[nstat].type){
					case LBM_TRANSPORT_STAT_LBTRM:
						lost += stats[nstat].transport.lbtrm.lost;
						break;
					case LBM_TRANSPORT_STAT_LBTRU:
						lost += stats[nstat].transport.lbtru.lost;
						break;
				}
			}
		}
	}
	return lost;
}

/*
 * Time callback to print bandwidth stats every second
 * (passed to lbm_schedule_timer())
 */
int rcv_handle_tmo(lbm_context_t *ctx, const void *clientd)
{
	/* Calculate aggregate transport level loss */
	/* Pass 0 for the print flag indicating interested in retrieving loss stats */
	lost = get_loss_or_print_stats(num_ctxs, ctxs, 0);

	lost_tmp = lost;
	if (last_lost <= lost){
		lost -= last_lost;
	}
	else{
		lost = 0;
	}
	last_lost = lost_tmp;

	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);

	print_bw(stdout, &endtv, msg_count, byte_count, lost);
	
	msg_count = 0;
	byte_count = 0;
	unrec_count = 0;
	rxs = 0;
	otrs = 0;

	if (pstats){
		/* Display transport level statistics */
		/* Pass opts->pstats for the print flag indicating interested in displaying stats */
		get_loss_or_print_stats(num_ctxs, ctxs, pstats);
	}
	
	current_tv(&starttv);
	if (lbm_schedule_timer(ctx, rcv_handle_tmo, NULL, evq, 1000) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
		exit(1);
	}
	return 0;
}

/* Callback received message handler (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_DATA:
		/*
		 * Data message received.
		 * All we do is increment the counters. We want to display
		 * aggregate reception rates for all receivers.
		 */
		msg_count++;
		byte_count += msg->len;

		if(msg->flags & LBM_MSG_FLAG_RETRANSMIT){
			rxs++;
		}
		if(msg->flags & LBM_MSG_FLAG_OTR){
			otrs++;
		}

		if (verbose) {
			printf("[%s][%s][%u]%s%s, %u bytes\n",
				   msg->topic_name, msg->source, msg->sequence_number,
				   ((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX-" : ""), 
				   ((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""), 
				   (unsigned int)msg->len);
		}
		break;
	case LBM_MSG_BOS:
		if (verbose)
			printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
		if (verbose)
			printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		unrec_count++;
		if (verbose) {
			printf("[%s][%s][%u], LOST\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		if (verbose) {
			printf("[%s][%s][%u], LOST BURST\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_REQUEST:
		/*
		 * Request message received.
		 * Just increment counters. We don't bother with responses here.
		 */
		msg_count++;
		byte_count += msg->len;
		if (verbose) {
			printf("[%s][%s][%u], Request\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* Event queue monitor callback (passed to lbm_event_queue_create()) */
int evq_monitor(lbm_event_queue_t *evq, int event, size_t evq_size,
				lbm_ulong_t event_delay_usec, void *clientd)
{
	printf("event queue threshold exceeded - event %x, sz %u, delay %lu\n",
		   event, evq_size, event_delay_usec);
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

int main(int argc, char **argv)
{
	lbm_topic_t *topic = NULL;
	lbm_context_attr_t * cattr;
	char topicname[LBM_MSG_MAX_TOPIC_LEN];
	char topicroot[80] = DEFAULT_TOPIC_ROOT;
	int c;
	int i = 0;
	int errflag = 0;
	int ctxidx = 0;
	int initial_topic_number = DEFAULT_INITIAL_TOPIC_NUMBER;
	lbmmon_sctl_t * monctl;
	int monitor_context = 0;
	int monitor_context_ivl = 0;
	int monitor_receiver = 0;
	int monitor_receiver_ivl = 0;
	int monitor_event_queue = 0;
	int monitor_event_queue_ivl = 0;
	long bufsize = 8;
	char * transport_options = NULL;
	char transport_options_string[1024];
	char * format_options = NULL;
	char format_options_string[1024];
	char * application_id = NULL;
	char application_id_string[1024];
	const lbmmon_transport_func_t * transport = lbmmon_transport_lbm_module();
	const lbmmon_format_func_t * format = lbmmon_format_csv_module();

#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;
		
		/* Windows socket setup */
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

	memset(transport_options_string, 0, sizeof(transport_options_string));
	memset(format_options_string, 0, sizeof(format_options_string));
	memset(application_id_string, 0, sizeof(application_id_string));

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case 'B':
				bufsize = atoi(optarg);
				break;
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'C':
				num_ctxs = atoi(optarg);
				if (num_ctxs > MAX_NUM_CTXS)
				{
					fprintf(stderr, "Too many contexts specified. Max number of contexts is %d\n", MAX_NUM_CTXS);
					errflag++;
				}
				break;
			case 'i':
				initial_topic_number = atoi(optarg);
				break;
			case 'L':
				linger = atoi(optarg);
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			case 'r':
				strncpy(topicroot, optarg, sizeof(topicroot));
				break;
			case 'R':
				num_rcvs = atoi(optarg);
				if (num_rcvs > MAX_NUM_RCVS)
				{
					fprintf(stderr, "Too many receivers specified. Max number of receivers is %d\n", MAX_NUM_RCVS);
					errflag++;
				}
				break;
			case 's':
				pstats++;
				break;
			case 'v':
				verbose++;
				break;
			case OPTION_MONITOR_CTX:
				monitor_context = 1;
				monitor_context_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_RCV:
				monitor_receiver = 1;
				monitor_receiver_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_TRANSPORT:
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
			case OPTION_MONITOR_TRANSPORT_OPTS:
				if (optarg != NULL)
				{
					strncpy(transport_options_string, optarg, sizeof(transport_options_string));
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
			case OPTION_MONITOR_FORMAT_OPTS:
				if (optarg != NULL)
				{
					strncpy(format_options_string, optarg, sizeof(format_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_APPID:
				if (optarg != NULL)
				{
					strncpy(application_id_string, optarg, sizeof(application_id_string));
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_EVQ:
				monitor_event_queue = 1;
				monitor_event_queue_ivl = atoi(optarg);
				break;
			default:
				errflag++;
				break;
		}
	}
	if (errflag != 0)
	{
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
	if (stats == NULL)
	{
		fprintf(stderr, "can't allocate statistics array\n");
		exit(1);
	}

	printf("Using %d context(s)\n", num_ctxs);
	if (monitor_context || monitor_receiver || monitor_event_queue)
	{
		if (strlen(transport_options_string) > 0)
		{
			transport_options = transport_options_string;
		}
		if (strlen(format_options_string) > 0)
		{
			format_options = format_options_string;
		}
		if (strlen(application_id_string) > 0)
		{
			application_id = application_id_string;
		}
		if (lbmmon_sctl_create(&monctl, format, format_options, transport, transport_options) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_create() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}
	}

	/* Retrieve current context settings */
	if (lbm_context_attr_create(&cattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}

	/* Set receive socket buffers to 8MB since we expect many receivers sharing transports */
	if(bufsize != 0) {
		bufsize *= 1024 * 1024;
		if (lbm_context_attr_setopt(cattr, "transport_tcp_receiver_socket_buffer",&bufsize,sizeof(bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: TCP %s\n", lbm_errmsg());
 			exit(1);
		} 
		if (lbm_context_attr_setopt(cattr, "transport_lbtrm_receiver_socket_buffer",&bufsize,sizeof(bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: LBTRM %s\n", lbm_errmsg());
 			exit(1);
		}
		if (lbm_context_attr_setopt(cattr, "transport_lbtru_receiver_socket_buffer",&bufsize,sizeof(bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: LBTRU %s\n", lbm_errmsg());
 			exit(1);
		}
	}

	/* Create one or more LBM contexts */
	for (i = 0; i < num_ctxs; i++)
	{
		if (lbm_context_create(&(ctxs[i]), cattr, NULL, NULL) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (monitor_context)
		{
			char appid[1024];
			char * appid_ptr = NULL;
			if (application_id != NULL)
			{
				snprintf(appid, sizeof(appid), "%s(%d)", application_id, i);
				appid_ptr = appid;
			}
			if (lbmmon_context_monitor(monctl, ctxs[i], appid_ptr, monitor_context_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_context_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
	}
	/* Create an event queue (with monitor callback) */
	if (lbm_event_queue_create(&evq, evq_monitor, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
		exit(1);
	}
	if (monitor_event_queue)
	{
		char * appid_ptr = NULL;
		if (application_id != NULL)
		{
			appid_ptr = application_id;
		}
		if (lbmmon_evq_monitor(monctl, evq, appid_ptr, monitor_event_queue_ivl) == -1)
		{
			fprintf(stderr, "lbmmon_evq_monitor() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}
	}
	
	if ((rcvs = malloc(sizeof(lbm_rcv_t *) * MAX_NUM_RCVS)) == NULL) {
		fprintf(stderr, "could not allocate receivers array\n");
		exit(1);
	}

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/* Create all the receivers */
	printf("Creating %d receivers\n", num_rcvs);
	ctxidx = 0;
	for (i = 0; i < num_rcvs; i++) {
		sprintf(topicname, "%s.%d", topicroot, (i + initial_topic_number));
		topic = NULL;
		/* First lookup desired topic */
		if (lbm_rcv_topic_lookup(&topic, ctxs[ctxidx], topicname, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		/*
		 * Create the receiver passing in the looked up topic info.
		 * We use the same callback function for data received.
		 */
		if (lbm_rcv_create(&(rcvs[i]), ctxs[ctxidx], topic, rcv_handle_msg, NULL, evq) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (monitor_receiver)
		{
			char appid[1024];
			char * appid_ptr = NULL;
			if (application_id != NULL)
			{
				snprintf(appid, sizeof(appid), "%s(%d)", application_id, i);
				appid_ptr = appid;
			}
			if (lbmmon_rcv_monitor(monctl, rcvs[i], appid_ptr, monitor_receiver_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_receiver_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		/* printf("Created receiver %d - '%s'\n",i,topicname); */
		if (i > 1 && (i % 1000) == 0)
			printf("Created %d receivers\n", i);
		ctxidx++;
		if (ctxidx >= num_ctxs)
			ctxidx = 0;
	}
	printf("Created %d receivers. Will start calculating aggregate throughput.\n", num_rcvs);

	current_tv(&starttv);
	/* Initialize timer with callback to print bandwidth stats. */
	if (lbm_schedule_timer(ctxs[0], rcv_handle_tmo, NULL, evq, 1000) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
		exit(1);
	}
	/*
	 * Enter event queue dispatch routine (result is that receiver callback
	 * is called on this thread, instead of LBM thread).
	 */
	if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_dispatch returned error.\n");
	}
	printf("Lingering for %d seconds...",linger);
	SLEEP_SEC(linger);
	printf("\n");

	if (monitor_context || monitor_receiver || monitor_event_queue)
	{
		if (monitor_context)
		{
			for (i = 0; i < num_ctxs; ++i)
			{
				if (lbmmon_context_unmonitor(monctl, ctxs[i]) == -1)
				{
					fprintf(stderr, "lbmmon_context_unmonitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			}
		}
		if (monitor_receiver)
		{
			for (i = 0; i < num_rcvs; ++i)
			{
				if (lbmmon_rcv_unmonitor(monctl, rcvs[i]) == -1)
				{
					fprintf(stderr, "lbmmon_rcv_unmonitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			}
		}
		if (monitor_event_queue)
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
	 * Although this is not strictly necessary in this example, this
	 * demonstrates how to delete the previously allocated receivers and
	 * contexts.
	 */
	printf("Deleting receivers....\n");
	for (i = 0; i < count; i++) {
		lbm_rcv_delete(rcvs[i]);
		rcvs[i] = NULL;
		if (i > 1 && (i % 1000) == 0)
			printf("Deleted %d receivers\n",i);
	}
	for (i = 0; i < num_ctxs; i++) {
		lbm_context_delete(ctxs[i]);
		ctxs[i] = NULL;
	}
	printf("Quitting....\n");
	return 0;
}

