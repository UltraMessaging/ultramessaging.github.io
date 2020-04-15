/*
"lbmmrcv.c: application that receives messages from a set of topics
"  (multiple receivers).

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
			#include <ktdmtyp.h>
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

static const char *rcsid_example_lbmmrcv = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmrcv.c#2 $";

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

const char Purpose[] = "Purpose: Receive messages on  multiple topics.";
const char Usage[] =
"Usage: %s [options]\n"
"  -B, --bufsize=#          Set receive socket buffer size to # (in MB)\n"
"  -c, --config=FILE        Use LBM configuration file FILE.\n"
"                           Multiple config files are allowed.\n"
"                           Example:  '-c file1.cfg -c file2.cfg'\n"
"  -C, --contexts=NUM       use NUM lbm_context_t objects\n"
"  -E, --exit               exit and end upon receiving End-of-Stream notification\n"
"  -e, --end-flag=FILE      clean up and exit when file FILE is created\n"
"  -h, --help               display this help and exit\n"
"  -i, --initial-topic=NUM  use NUM as initial topic number\n"
"  -o, --regid-offset=offset  use offset to calculate Registration ID\n"
"                             (as source registration ID + offset)\n"
"                             offset of 0 forces creation of regid by store\n"
"  -L, --linger=NUM         linger for NUM seconds after done\n"
"  -r, --root=STRING        use topic names with root of STRING\n"
"  -R, --receivers=NUM      create NUM receivers\n"
"  -s, --statistics         print statistics along with bandwidth\n"
"  -v, --verbose            be verbose\n"
MONOPTS_RECEIVER
MONMODULEOPTS_SENDER;

const char * OptionString = "B:c:C:Ee:hi:o:L:r:R:sv";
#define OPTION_MONITOR_RCV 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
const struct option OptionTable[] =
{
	{ "bufsize", required_argument, NULL, 'B' },
	{ "config", required_argument, NULL, 'c' },
	{ "contexts", required_argument, NULL, 'C' },
	{ "help", no_argument, NULL, 'h' },
	{ "end-flag", required_argument, NULL, 'e' },
	{ "initial-topic", required_argument, NULL, 'i' },
	{ "regid-offset", required_argument, NULL, 'o' },
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
	{ NULL, 0, NULL, 0 }
};

#define DEFAULT_MAX_MESSAGES 10000000
#define MAX_NUM_RCVS 1000001
#define MAX_TOPIC_NAME_LEN 80
#define DEFAULT_NUM_RCVS 100
#define MAX_NUM_CTXS 5
#define DEFAULT_NUM_CTXS 1
#define DEFAULT_TOPIC_ROOT "29west.example.multi"
#define DEFAULT_INITIAL_TOPIC_NUMBER 0
#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10
#define DEFAULT_LINGER_SECONDS 0

struct Options {

	char application_id_string[1024];
	long bufsize;
	char *end_flg_file;
	int end_on_end;
	const lbmmon_format_func_t *format;
	const lbmmon_transport_func_t *transport;
	char format_options_string[1024];
	int initial_topic_number;
	int linger;
	int monitor_context;
	int monitor_context_ivl;
	int monitor_receiver;
	int monitor_receiver_ivl;
	int num_ctxs;
	int num_rcvs;
	int pstats;
	int regid_offset;   		/* Offset for calculating registration IDs */
	char topicroot[80];
	char transport_options_string[1024];
	int verbose;
} options;

lbm_event_queue_t *evq = NULL;
lbm_rcv_t **rcvs = NULL;
int count = 0;
int msg_count = 0, total_msg_count = 0;
int byte_count = 0;
int unrec_count = 0, total_unrec_count = 0;
int close_recv = 0;
int burst_loss = 0, total_burst_loss = 0;
int rxs = 0;
int otrs = 0;
lbm_ulong_t lost = 0, last_lost = 0;
lbm_rcv_transport_stats_t * stats = NULL;
int nstats = DEFAULT_NUM_SRCS;

/*
 * For the elapsed time, calculate and print the msgs/sec and bits/sec as well
 * as any unrecoverable data.
 */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned int bytes, int unrec, lbm_ulong_t lost, int rxs, int otrs)
{
	char scale[] = {'\0', 'K', 'M', 'G'};
	int msg_scale_index = 0, bit_scale_index = 0, rps_scale_index = 0;
	double sec = 0.0, mps = 0.0, bps = 0.0, rps = 0.0;
	double kscale = 1000.0;
	
	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	rps = (double)rxs/sec;
	bps = (double)bytes*8/sec;
	
	while (mps >= kscale) {
		mps /= kscale;
		msg_scale_index++;
	}

	while (rps >= kscale) {
		rps /= kscale;
		rps_scale_index++;
	}
	
	while (bps >= kscale) {
		bps /= kscale;
		bit_scale_index++;
	}

	if ((rxs != 0) || (otrs != 0)) {
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]", sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rxs, otrs);
	}
	else{ 
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps", sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
	}
	
	if (lost != 0 || unrec != 0 || burst_loss != 0) {
		fprintf(fp, " [%lu pkts lost, %u msgs unrecovered, %d loss bursts]", lost, unrec, burst_loss);
		burst_loss = 0;
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

/* callback for setting the RegID based on extended info */
lbm_uint_t ume_rcv_regid_ex(lbm_ume_rcv_regid_ex_func_info_t *info, void *clientd)
{
	struct Options *opts = &options;
	lbm_uint_t regid = info->src_registration_id + opts->regid_offset;

	if (opts->verbose)
		printf("Store %u: %s [%s][%u] Flags %x. Requesting regid: %u (CD %p)\n", 
				info->store_index, info->store, info->source, info->src_registration_id, 
				info->flags, regid, info->source_clientd);
	return regid;
}

/* Callback received message handler (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;
	
	switch (msg->type) {
	case LBM_MSG_DATA:
		/*
		 * Data message received.
		 * All we do is increment the counters.
		 * We want to display aggregate reception rates for all
		 * receivers.
		 */
		msg_count++;
		total_msg_count++;
		byte_count += msg->len;
		if (opts->verbose) {
			printf("[%s][%s][%u]%s%s, %u bytes\n",
				   msg->topic_name, msg->source, msg->sequence_number,
				   ((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX-" : ""),
				   ((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""),
				   (unsigned int)msg->len);
		}
		if(msg->flags & LBM_MSG_FLAG_RETRANSMIT) rxs++;
		if(msg->flags & LBM_MSG_FLAG_OTR) otrs++;
		break;
	case LBM_MSG_BOS:
			printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
			printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		if (opts->end_on_end)
			close_recv = 1;
		break;
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		if (opts->verbose)
			printf("[%s], no sources found for topic\n", msg->topic_name);
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		unrec_count++;
		total_unrec_count++;
		if (opts->verbose) {
			printf("[%s][%s][%u], LOST\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		total_burst_loss++;
		if (opts->verbose) {
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
		total_msg_count++;
		byte_count += msg->len;
		if (opts->verbose) {
			printf("[%s][%s][%u], Request\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UME_REGISTRATION_SUCCESS_EX:
	case LBM_MSG_UME_REGISTRATION_COMPLETE_EX:
		/* Provided to enable quiet usage of lbmstrm with UME */
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
#endif

void process_cmdline(int argc, char **argv) {

	struct Options *opts = &options;
	int c, errflag = 0;
	
	/* Set default values */
	memset(opts, 0, sizeof(*opts));
	opts->bufsize = 8;
	opts->end_flg_file = NULL;
	opts->format = lbmmon_format_csv_module();
	opts->initial_topic_number = DEFAULT_INITIAL_TOPIC_NUMBER;
	opts->linger = DEFAULT_LINGER_SECONDS;
	opts->num_ctxs = DEFAULT_NUM_CTXS;
	opts->num_rcvs = DEFAULT_NUM_RCVS;
	opts->regid_offset = -1;
	strncpy(opts->topicroot, DEFAULT_TOPIC_ROOT, sizeof(opts->topicroot));
	opts->transport = lbmmon_transport_lbm_module();

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case 'B':
				opts->bufsize = atoi(optarg);
				break;
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'C':
				opts->num_ctxs = atoi(optarg);
				if (opts->num_ctxs > MAX_NUM_CTXS)
				{
					fprintf(stderr, "Too many contexts specified. "
									"Max number of contexts is %d\n", MAX_NUM_CTXS);
					errflag++;
				}
				break;
			case 'E':
				opts->end_on_end = 1;
				break;
			case 'e':
				opts->end_flg_file = optarg;
				break;
			case 'i':
				opts->initial_topic_number = atoi(optarg);
				break;
			case 'L':
				opts->linger = atoi(optarg);
				break;		
			case 'o':
				opts->regid_offset = atoi(optarg);
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			case 'r':
				strncpy(opts->topicroot, optarg, sizeof(opts->topicroot));
				break;
			case 'R':
				opts->num_rcvs = atoi(optarg);
				if (opts->num_rcvs > MAX_NUM_RCVS)
				{
					fprintf(stderr, "Too many receivers specified. "
									"Max number of receivers is %d\n", MAX_NUM_RCVS);
					errflag++;
				}
				break;
			case 's':
				opts->pstats++;
				break;
			case 'v':
				opts->verbose++;
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
						opts->transport = lbmmon_transport_lbm_module();
					}
					else if (strcasecmp(optarg, "udp") == 0)
					{
						opts->transport = lbmmon_transport_udp_module();
					}
					else if (strcasecmp(optarg, "lbmsnmp") == 0)
					{
						opts->transport = lbmmon_transport_lbmsnmp_module();
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
					strncpy(opts->transport_options_string, optarg, 
							sizeof(opts->transport_options_string));
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
						opts->format = lbmmon_format_csv_module();
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
					strncpy(opts->format_options_string, optarg, 
							sizeof(opts->format_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_APPID:
				if (optarg != NULL)
				{
					strncpy(opts->application_id_string, optarg, 
							sizeof(opts->application_id_string));
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
	if (errflag != 0)
	{
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}
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

int main(int argc, char **argv)
{
	struct Options *opts = &options;
	lbm_context_t *ctxs[MAX_NUM_CTXS];
	lbm_context_attr_t * cattr;
	lbm_topic_t *topic = NULL;
	lbm_rcv_topic_attr_t *rcv_attr;
	char topicname[LBM_MSG_MAX_TOPIC_LEN];
	int i = 0;
	int ctxidx = 0;
	FILE *end_flg_fp = NULL;
	lbmmon_sctl_t * monctl;
	char * transport_options = NULL;
	char * format_options = NULL;
	char * application_id = NULL;
	lbm_ulong_t lost_tmp;
	
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

	/* Process command line options */
	process_cmdline(argc, argv);

	stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
	if (stats == NULL)
	{
		fprintf(stderr, "can't allocate statistics array\n");
		exit(1);
	}

	printf("Using %d context(s)\n", opts->num_ctxs);
	if (opts->monitor_context || opts->monitor_receiver)
	{
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
		if (lbmmon_sctl_create(&monctl, opts->format, format_options, 
			opts->transport, transport_options) == -1)
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
	if(opts->bufsize != 0) {
		opts->bufsize *= 1024 * 1024;
		if (lbm_context_attr_setopt(cattr, "transport_tcp_receiver_socket_buffer",
									&opts->bufsize,sizeof(opts->bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: TCP %s\n", lbm_errmsg());
 			exit(1);
		}
		if (lbm_context_attr_setopt(cattr, "transport_lbtrm_receiver_socket_buffer",
									&opts->bufsize,sizeof(opts->bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: LBTRM %s\n", lbm_errmsg());
 			exit(1);
		}
		if (lbm_context_attr_setopt(cattr, "transport_lbtru_receiver_socket_buffer",
									&opts->bufsize,sizeof(opts->bufsize)) == LBM_FAILURE) {
 			fprintf(stderr, "lbm_context_attr_setopt: LBTRU %s\n", lbm_errmsg());
 			exit(1);
		}
	}

	/* Create one or more LBM contexts */
	for (i = 0; i < opts->num_ctxs; i++)
	{
		if (lbm_context_create(&(ctxs[i]), cattr, NULL, NULL) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (opts->monitor_context)
		{
			char appid[1024];
			char * appid_ptr = NULL;
			if (application_id != NULL)
			{
				snprintf(appid, sizeof(appid), "%s(%d)", application_id, i);
				appid_ptr = appid;
			}
			if (lbmmon_context_monitor(monctl, ctxs[i], appid_ptr, 
									   opts->monitor_context_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_context_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
	}
	
	/* After a context gets created, the attributes can be discarded */
	lbm_context_attr_delete(cattr);;

	if ((rcvs = malloc(sizeof(lbm_rcv_t *) * MAX_NUM_RCVS)) == NULL) {
		fprintf(stderr, "could not allocate receivers array\n");
		exit(1);
	}

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/* init rcv topic attributes */
	if (lbm_rcv_topic_attr_create(&rcv_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if(opts->regid_offset >= 0)
	{
		/*  This is relevant for UME only, but enables this example to be used 
		 *  with persistent streams.  There is no effect by doing this on non 
		 *  persistent streams or if an LBM only license is used
		 */
		lbm_ume_rcv_regid_ex_func_t id;

		id.func = ume_rcv_regid_ex;
		id.clientd = NULL;

		if (lbm_rcv_topic_attr_setopt(rcv_attr, "ume_registration_extended_function", 
												&id, sizeof(id)) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_attr_setopt:ume_registration_extended_function: %s\n",
							 lbm_errmsg());
			exit(1);
		}
		printf("Will use RegID offset %u.\n", opts->regid_offset);
	}

	/* Create all the receivers */
	printf("Creating %d receivers\n", opts->num_rcvs);
	ctxidx = 0;
	for (i = 0; i < opts->num_rcvs; i++) {
		sprintf(topicname, "%s.%d", opts->topicroot, (i + opts->initial_topic_number));
		topic = NULL;
		/* First lookup the desired topic */
		if (lbm_rcv_topic_lookup(&topic, ctxs[ctxidx], topicname, rcv_attr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		/*
		 * Create receiver passing in the looked up topic info.
		 * We use the same callback function for data received.
		 */
		if (lbm_rcv_create(&(rcvs[i]), ctxs[ctxidx], topic, rcv_handle_msg, NULL, evq) 
						   == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (opts->monitor_receiver)
		{
			char appid[1024];
			char * appid_ptr = NULL;
			if (application_id != NULL)
			{
				snprintf(appid, sizeof(appid), "%s(%d)", application_id, i);
				appid_ptr = appid;
			}
			if (lbmmon_rcv_monitor(monctl, rcvs[i], appid_ptr, opts->monitor_receiver_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_receiver_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		/* printf("Created receiver %d - '%s'\n",i,topicname); */
		if (i > 1 && (i % 1000) == 0)
			printf("Created %d receivers\n", i);
		ctxidx++;
		if (ctxidx >= opts->num_ctxs)
			ctxidx = 0;
	}
	printf("Created %d receivers. Will start calculating aggregate throughput.\n", opts->num_rcvs);
	
	/* Delete rcv topic attributes */
	lbm_rcv_topic_attr_delete(rcv_attr);

	if (opts->end_flg_file)
		unlink(opts->end_flg_file);

	/* Sleep/Wakeup every second and print out bandwidth stats. */
	end_flg_fp = NULL;
	while (end_flg_fp == NULL) {
		struct timeval starttv, endtv;

		current_tv(&starttv);
		
		SLEEP_SEC(1);

		/* Calculate aggregate transport level loss */
		/* Pass 0 for the print flag indicating interested in retrieving loss stats */
		lost = get_loss_or_print_stats(opts->num_ctxs, ctxs, 0);

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

		print_bw(stdout, &endtv, msg_count, byte_count, unrec_count, lost, rxs, otrs);
		
		msg_count = 0;
		byte_count = 0;
		unrec_count = 0;
		rxs = 0;
		otrs = 0;

		if (opts->pstats){
			/* Display transport level statistics */
			/* Pass opts->pstats for the print flag indicating interested in displaying stats */
			get_loss_or_print_stats(opts->num_ctxs, ctxs, opts->pstats);
		}

		if (opts->end_flg_file)
			end_flg_fp = fopen(opts->end_flg_file, "r");
		if (close_recv)
			break;
	}
	if (end_flg_fp != NULL) {  /* in case break loop for other reason */
		fclose(end_flg_fp);
		printf("%s detected, cleaning up....\n", opts->end_flg_file);
	}

	printf("Lingering for %d seconds...\n", opts->linger);
	SLEEP_SEC(opts->linger);

	if (opts->monitor_context || opts->monitor_receiver)
	{
		if (opts->monitor_context)
		{
			for (i = 0; i < opts->num_ctxs; ++i)
			{
				if (lbmmon_context_unmonitor(monctl, ctxs[i]) == -1)
				{
					fprintf(stderr, "lbmmon_context_unmonitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			}
		}
		if (opts->monitor_receiver)
		{
			for (i = 0; i < opts->num_rcvs; ++i)
			{
				if (lbmmon_rcv_unmonitor(monctl, rcvs[i]) == -1)
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

	/*
	 * Not strictly necessary (nor reached) in this example, but this is
	 * how to delete receivers and tear down a context.
	 */
	printf("Deleting receivers....\n");
	for (i = 0; i < opts->num_rcvs;) {
		lbm_rcv_delete(rcvs[i]);
		rcvs[i] = NULL;
		if (i > 1 && (i % 1000) == 0)
			printf("Deleted %d receivers\n",i);
		i++;
	}
	for (i = 0; i < opts->num_ctxs; i++) {
		lbm_context_delete(ctxs[i]);
		ctxs[i] = NULL;
	}
	free(rcvs);
	printf("Quitting.... received %u messages", total_msg_count);
	if (total_unrec_count > 0 || total_burst_loss > 0) {
		printf(", %u msgs unrecovered, %u loss bursts", total_unrec_count, total_burst_loss);
	}
	printf("\n");
	return 0;
}

