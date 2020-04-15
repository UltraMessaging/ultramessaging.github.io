/*
"lbmsrc.c: application that sends to a given topic (single
"  source) as fast as it can.

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
	#ifdef __TANDEM
		#include <strings.h>
	#endif
#endif
#include "replgetopt.h"
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "monmodopts.h"
#include "verifymsg.h"
#include "lbm-example-util.h"

static const char *rcsid_example_lbmsrc = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmsrc.c#2 $";

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_MAX_MESSAGES 10000000
#define DEFAULT_DELAY_B4CLOSE 5

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

const char Purpose[] = "Purpose: Send messages on a single topic.";
const char Usage[] =
"Usage: %s [options] topic\n"
"Available options:\n"
"  -c, --config=FILE         Use LBM configuration file FILE.\n"
"                            Multiple config files are allowed.\n"
"                            Example:  '-c file1.cfg -c file2.cfg'\n"
"  -d, --delay=NUM           delay sending for NUM seconds after source creation\n"
"  -h, --help                display this help and exit\n"
"  -j, --late-join=NUM       enable Late Join with specified retention buffer size (in bytes)\n"
"  -l, --length=NUM          send messages of NUM bytes\n"
"  -L, --linger=NUM          linger for NUM seconds before closing context\n"
"  -M, --messages=NUM        send NUM messages\n"
"  -n, --non-block           use non-blocking I/O\n"
"  -N, --channel=NUM         send on channel NUM\n"
"  -P, --pause=NUM           pause NUM milliseconds after each send\n"
"  -R, --rate=[UM]DATA/RETR  Set transport type to LBT-R[UM], set data rate limit to\n"
"                            DATA bits per second, and set retransmit rate limit to\n"
"                            RETR bits per second.  For both limits, the optional\n"
"                            k, m, and g suffixes may be used.  For example,\n"
"                            '-R 1m/500k' is the same as '-R 1000000/500000'\n"
"  -s, --statistics=NUM      print statistics every NUM seconds\n"
"  -V, --verifiable          construct verifiable messages\n"
MONOPTS_SENDER
MONMODULEOPTS_SENDER;

const char * OptionString = "c:d:j:hL:l:M:nN:P:R:s:V";
#define OPTION_MONITOR_SRC 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
const struct option OptionTable[] =
{
	{ "config", required_argument, NULL, 'c' },
	{ "delay", required_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ "late-join", required_argument, NULL, 'j' },
	{ "length", required_argument, NULL, 'l' },
	{ "linger", required_argument, NULL, 'L' },
	{ "messages", required_argument, NULL, 'M' },
	{ "non-block", no_argument, NULL, 'n' },
	{ "pause", required_argument, NULL, 'P' },
	{ "rate", required_argument, NULL, 'R' },
	{ "statistics", required_argument, NULL, 's' },
	{ "verifiable", no_argument, NULL, 'V' },
	{ "monitor-src", required_argument, NULL, OPTION_MONITOR_SRC },
	{ "monitor-ctx", required_argument, NULL, OPTION_MONITOR_CTX },
	{ "monitor-transport", required_argument, NULL, OPTION_MONITOR_TRANSPORT },
	{ "monitor-transport-opts", required_argument, NULL, OPTION_MONITOR_TRANSPORT_OPTS },
	{ "monitor-format", required_argument, NULL, OPTION_MONITOR_FORMAT },
	{ "monitor-format-opts", required_argument, NULL, OPTION_MONITOR_FORMAT_OPTS },
	{ "monitor-appid", required_argument, NULL, OPTION_MONITOR_APPID },
	{ "channel", required_argument, NULL, 'N' },
	{ NULL, 0, NULL, 0 }
};

int blocked = 0;

/* For the elapsed time, calculate and print the msgs/sec and bits/sec */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned long long bytes)
{
	char scale[] = {'\0', 'K', 'M', 'G'};
	int msg_scale_index = 0, bit_scale_index = 0;
	double sec = 0.0, mps = 0.0, bps = 0.0;
	double kscale = 1000.0;
	
	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */	
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	bps = ((double)(bytes<<3))/sec;
	
	while (mps >= kscale) {
		mps /= kscale;
		msg_scale_index++;
	}
	
	while (bps >= kscale) {
		bps /= kscale;
		bit_scale_index++;
	}
	
	fprintf(fp, "%.04g secs. %.04g %cmsgs/sec. %.04g %cbps\n", sec,
			mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
	fflush(fp);
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_src_t *src)
{
	lbm_src_transport_stats_t stats;

	/* Retrieve source transport statistics */
	if (lbm_src_retrieve_transport_stats(src, &stats) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_retrieve_stats: %s\n", lbm_errmsg());
		exit(1);
	}
	switch (stats.type) {
	case LBM_TRANSPORT_STAT_TCP:
		fprintf(fp, "TCP, buffered %lu, clients %lu\n",stats.transport.tcp.bytes_buffered,
				stats.transport.tcp.num_clients);
		break;
	case LBM_TRANSPORT_STAT_LBTRM:
		fprintf(fp, "LBT-RM, sent %lu/%lu, txw %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu, rctlr %lu/%lu\n",
				stats.transport.lbtrm.msgs_sent, stats.transport.lbtrm.bytes_sent,
				stats.transport.lbtrm.txw_msgs, stats.transport.lbtrm.txw_bytes,
				stats.transport.lbtrm.naks_rcved, stats.transport.lbtrm.nak_pckts_rcved,
				stats.transport.lbtrm.naks_ignored, stats.transport.lbtrm.naks_rx_delay_ignored,
				stats.transport.lbtrm.naks_shed,
				stats.transport.lbtrm.rxs_sent,
				stats.transport.lbtrm.rctlr_data_msgs, stats.transport.lbtrm.rctlr_rx_msgs);
		break;
	case LBM_TRANSPORT_STAT_LBTRU:
		fprintf(fp, "LBT-RU, clients %lu, sent %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu\n",
				stats.transport.lbtru.num_clients,
				stats.transport.lbtru.msgs_sent, stats.transport.lbtru.bytes_sent,
				stats.transport.lbtru.naks_rcved, stats.transport.lbtru.nak_pckts_rcved,
				stats.transport.lbtru.naks_ignored, stats.transport.lbtru.naks_rx_delay_ignored,
				stats.transport.lbtru.naks_shed,
				stats.transport.lbtru.rxs_sent);
		break;
	case LBM_TRANSPORT_STAT_LBTIPC:
		fprintf(fp, "LBT-IPC, clients %lu, sent %lu/%lu\n",
				stats.transport.lbtipc.num_clients,
				stats.transport.lbtipc.msgs_sent, stats.transport.lbtipc.bytes_sent);
		break;
	case LBM_TRANSPORT_STAT_LBTRDMA:
		fprintf(fp, "LBT-RDMA, clients %lu, sent %lu/%lu\n",
				stats.transport.lbtrdma.num_clients,
				stats.transport.lbtrdma.msgs_sent, stats.transport.lbtrdma.bytes_sent);
		break;
	default:
		break;
	}
	fflush(fp);
}

void print_help_exit(char **argv, int exit_value){
	fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
	fprintf(stderr, Usage, argv[0]);
	exit(exit_value);
}

/* Logging callback */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

struct TimerControl {
	int stats_timer_id;
	lbm_ulong_t stats_msec;
	int stop_timer;
} timer_control = { -1, 0, 0 };


/* Source event handler callback (passed into lbm_src_create()) */
int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
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
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

/* Timer callback to handle periodic display of source statistics */
int handle_stats_timer(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;

	print_stats(stdout, src);
	if (!timer_control.stop_timer) {
		/* Schedule timer to call the function handle_stats_timer() to dump the current statistics */
		if ((timer_control.stats_timer_id = 
			lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, timer_control.stats_msec)) == -1)
		{
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	return 0;
}

struct Options {
	char transport_options_string[1024];	/* Transport Options given to lbmmon_sctl_create() */
	char format_options_string[1024];		/* Format Options given to lbmmon_sctl_create() */
	char application_id_string[1024];		/* Application ID given to lbmmon_context_monitor()	*/
	unsigned int msgs;						/* Number of messages to be sent */
	size_t msglen;							/* Length of messages to be sent */
	unsigned long int latejoin_threshold; 	/* Maximum Late Join buffer size, in bytes */
	int pause;								/* Pause interval between messages */
	int delay,linger;					/* Interval to linger before and after sending messages	*/
	int block;								/* Flag to control whether blocking sends are used	*/
	lbm_uint64_t rm_rate;				/* Rate control values */
	lbm_uint64_t rm_retrans;			/* Rate control values */
	char rm_protocol;						/* Rate control protocol */
	lbm_ulong_t stats_sec;					/* Interval for dumping statistics */
	int verifiable_msgs;				/* Flag to control message verification (verifymsg.h) */
	int monitor_context;					/* Flag to control context level monitoring */
	int monitor_context_ivl;				/* Interval for context level monitoring */
	int monitor_source;						/* Flag to control source level monitoring */
	int monitor_source_ivl;					/* Interval for source level monitoring */
	lbmmon_transport_func_t * transport;	/* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;			/* Function pointer to chosen format module */
	char *topic;							/* The topic to be sent on */
	long channel_number;					/* The channel (sub-topic) number to use */
};

void process_cmdline(int argc, char **argv,struct Options *opts)
{
	int c,errflag = 0;

	/* Set default option values */
	memset(opts, 0, sizeof(*opts));
	opts->latejoin_threshold = 0;
	opts->linger = DEFAULT_DELAY_B4CLOSE;
	opts->delay = 1;
	opts->msglen = MIN_ALLOC_MSGLEN;
	opts->msgs = DEFAULT_MAX_MESSAGES;
	opts->block = 1;
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();
	opts->channel_number = -1;
	opts->rm_protocol = 'M';

	/* Process the command line options, setting local variables with values */
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
			case 'd':
				opts->delay = atoi(optarg);
				break;
			case 'j':
				if (sscanf(optarg, "%lu", &opts->latejoin_threshold) != 1)
					++errflag;
				break;
			case 'L':
				opts->linger = atoi(optarg);
				break;
			case 'l':
				opts->msglen = atoi(optarg);
				break;
			case 'M':
				opts->msgs = atoi(optarg);
				break;
			case 'n':
				opts->block = 0;
				break;
			case 'N':
				opts->channel_number = atol(optarg);
				break;
			case 'h':
				print_help_exit(argv, 0);
			case 'P':
				opts->pause = atoi(optarg);
				break;
			case 'R':
				errflag += parse_rate(optarg, &opts->rm_protocol, &opts->rm_rate, &opts->rm_retrans);
				break;
			case 's':
				opts->stats_sec = atoi(optarg);
				break;
			case 'V':
				opts->verifiable_msgs = 1;
				break;
			case OPTION_MONITOR_CTX:
				opts->monitor_context = 1;
				opts->monitor_context_ivl = atoi(optarg);
				break;
			case OPTION_MONITOR_SRC:
				opts->monitor_source = 1;
				opts->monitor_source_ivl = atoi(optarg);
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
		/* An error occurred processing the command line - print help and exit */
		print_help_exit(argv, 1); 		
	}
	opts->topic = argv[optind];
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
	lbm_set_lbtrm_src_loss_rate(LossRate);
	lbm_set_lbtru_src_loss_rate(LossRate);
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
	lbm_set_lbtrm_src_loss_rate(LossRate);
	lbm_set_lbtru_src_loss_rate(LossRate);
}

static
void
SigUsr2Handler(int signo)
{
	LossRate = 0;
	lbm_set_lbtrm_src_loss_rate(LossRate);
	lbm_set_lbtru_src_loss_rate(LossRate);
}
#endif

int main(int argc, char **argv)
{
	struct Options options,*opts = &options;
	double secs = 0.0;
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src;
	lbm_src_topic_attr_t * tattr;
	lbm_context_attr_t * cattr;
	struct timeval starttv, endtv;
	int count = 0;
	unsigned long long bytes_sent = 0;
	char *message = NULL;
	lbmmon_sctl_t * monctl;
	lbm_src_channel_info_t *chn = NULL;
	lbm_src_send_ex_info_t info;
	int err;

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

	/* Process the different options set by the command line processing */
	process_cmdline(argc,argv,opts);

	/* If set, check the requested message length is not too small */
	if (opts->verifiable_msgs != 0)
	{
		size_t min_msglen = minimum_verifiable_msglen();
		if (opts->msglen < min_msglen)
		{
			printf("Specified message length %u is too small for verifiable messages.\n", (unsigned) opts->msglen);
			printf("Setting message length to minimum (%u).\n", (unsigned) min_msglen);
			opts->msglen = min_msglen;
		}
	}
	
	/* Setup logging callback */
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}

	/* if message buffer is too small, then the sprintf will cause issues. So, allocate with a min size */
	if (opts->msglen < MIN_ALLOC_MSGLEN) {
		message = malloc(MIN_ALLOC_MSGLEN);
	} else {
		message = malloc(opts->msglen);
	}
	
	if (message == NULL) {
		fprintf(stderr, "could not allocate message buffer of size %u bytes\n",(unsigned) opts->msglen);
		exit(1);
	}
	
	memset(message, 0, opts->msglen);

	/* Retrieve current context settings */
	if (lbm_context_attr_create(&cattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}

	/* Retrieve current source topic settings */
	if (lbm_src_topic_attr_create(&tattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_src_topic_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}

	/* If the user specified a rate control, set the transport to LBM-RM in the topic attribute structure and
	 * and the requested rate control values in the context attribute structure
	 */
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

	/* If user specified a Late Join threshold, set the value in the context attribute structure */
	if (opts->latejoin_threshold > 0)
	{
		if (lbm_src_topic_attr_str_setopt(tattr, "late_join", "1") != 0) {
			fprintf(stderr,"lbm_src_topic_attr_str_setopt:late_join: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_src_topic_attr_setopt(tattr, "retransmit_retention_size_threshold",
					&opts->latejoin_threshold, sizeof(opts->latejoin_threshold)) != 0) {
			fprintf(stderr,"lbm_src_topic_attr_setopt:retransmit_retention_size_threshold: %s\n", lbm_errmsg());
			exit(1);
		}

		printf("Enabled Late Join with message retention threshold set to %lu bytes.\n", opts->latejoin_threshold);

	}

	/* Create LBM context (passing in context attributes) */
	if (lbm_context_create(&ctx, cattr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(cattr);

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/* Allocate the desired topic */
	if (lbm_src_topic_alloc(&topic, ctx, opts->topic, tattr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_src_topic_attr_delete(tattr);

	/*
	 * Create LBM source passing in the allocated topic and event
	 * handler. The source object is returned here in &src.
	 */
	if (lbm_src_create(&src, ctx, topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
		exit(1);
	}

	/* If a statistics were requested, setup an LBM timer to the dump the statistics */
	if (opts->stats_sec > 0) {
		timer_control.stats_msec = opts->stats_sec * 1000;

		/* Schedule timer to call the function handle_stats_timer() to dump the current statistics */
		if ((timer_control.stats_timer_id = 
			lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, timer_control.stats_msec)) == -1)
		{
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* If monitoring options were selected, setup lbmmon */
	if (opts->monitor_context || opts->monitor_source)
	{
		char * transport_options = NULL;
		char * format_options = NULL;
		char * application_id = NULL;

		/* lbmmon_sctl_create, lbmmon_context_monitor and lbmmon_src_monitor
		 * must be set to NULL or a valid value. Use local pointers to point
		 * to the options array if a valid value was provided on the command line.
		 */
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

		/* Create the source monitor controller based on requested options */
		if (lbmmon_sctl_create(&monctl, opts->format, format_options, opts->transport, transport_options) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_create() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}

		/* Register the source/context for monitoring */
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

	/* Give the system a chance to cleanly initialize.
	 * When using LBT-RM, this allows topic resolution to occur and
	 * existing receivers to be aware of this new ssource.
	 */
	if (opts->delay > 0) {
		printf("Will start sending in %d second%s...\n", opts->delay, ((opts->delay > 1) ? "s" : ""));
		SLEEP_SEC(opts->delay);
	}

	if (opts->channel_number >= 0)
	{
		printf("Sending on channel %ld\n", opts->channel_number);
		if(lbm_src_channel_create(&chn, src, opts->channel_number) != 0) {
			fprintf(stderr, "lbm_src_channel_create: %s\n", lbm_errmsg());
			exit(1);
		}

		memset(&info, 0, sizeof(lbm_src_send_ex_info_t));
		info.flags = LBM_SRC_SEND_EX_FLAG_CHANNEL;
		info.channel_info = chn;	
	}

	/* Start sending messages to whomever is listeningg */
	printf("Sending %u messages of size %u bytes to topic [%s]\n",
		   opts->msgs, (unsigned)opts->msglen, opts->topic);
	current_tv(&starttv); /* Store the start time */
	for (count = 0; count < opts->msgs; ) {
		/* Create a dummy message to send */
		if (opts->verifiable_msgs) {
			construct_verifiable_msg(message, opts->msglen);
		} else {
			sprintf(message, "message %u", count);
		}

		/* Set the blocked flag to indicate we are blocked trying to send a message */
		blocked = 1;

		/* Send message using allocated source */
		if (chn != NULL)
			err = lbm_src_send_ex(src, message, opts->msglen, opts->block ? 0 : LBM_SRC_NONBLOCK, &info);
		else
			err = lbm_src_send(src, message, opts->msglen, opts->block ? 0 : LBM_SRC_NONBLOCK);
		if ( err == LBM_FAILURE) {
			if (lbm_errnum() == LBM_EWOULDBLOCK)
			{
				/* The rate controller indicates to applications that the application is
				 * exceeding the data rate by returning LBM_EWOULDBLOCK.
				 * The application must wait for the WAKEUP event in the source event
				 * handler call back function handle_src_event(). This example program
				 * chooses to use global variable blocked between handle_src_event() and
				 * the following loop to unblock transmission.
				 */
				while (blocked)
				{
					SLEEP_MSEC(100);
				}
				/*
				 * Rate controller indicated that the rate is no longer exceeeded and
				 * handle_src_event() unlocked the loop above so transmission can resume.
				 * Simply reloop and send a new message.
				 */
				continue;
			}
			else
			{
				/* Unexpected error occurred */
				fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
		}
		blocked = 0;
		bytes_sent += (unsigned long long) opts->msglen;
		count++;

		/* The user requested to pause between each packet, do so */
		if (opts->pause > 0) {
			SLEEP_MSEC(opts->pause);
		}
	}

	/* Calculate the time it took to send the messages and dump */
	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);
	secs = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
	printf("Sent %u messages of size %u bytes in %.04g seconds.\n",
			count, (unsigned)opts->msglen, secs);
	print_bw(stdout, &endtv, count, bytes_sent);

	/* Stop rescheduling the stats timer */
	timer_control.stop_timer = 1;

	/*
	 * Sleep for a bit so that batching gets out all the queued messages,
	 * if any.  If we just exit, then some messages may not have been sent by
	 * TCP yet.
	 */
	if(opts->stats_sec > 0 && opts->stats_sec > opts->linger) {
		printf("Delaying to catch last stats timer... \n");
		SLEEP_SEC((opts->stats_sec - opts->linger) + 1);
	}
	else
		print_stats(stdout, src);
	if (opts->linger > 0) {
		printf("Lingering for %d seconds...\n", opts->linger);
		SLEEP_SEC(opts->linger);
	}

	/* If the user requested monitoring, unregister the monitors etc  */
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

	if (opts->channel_number >= 0)
	{
	if(lbm_src_channel_delete(chn) != 0) {
			fprintf(stderr, "lbm_src_channel_delete: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	printf("Deleting source\n");

	/* Deallocate source and LBM context */
	lbm_src_delete(src);
	src = NULL;
	printf("Deleting context\n");
	lbm_context_delete(ctx);
	ctx = NULL;

	/* Free the message buffer used for sending */
	free(message);
	return 0;
}

