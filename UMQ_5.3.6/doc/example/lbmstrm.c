/*
"lbmstrm.c: application that sends messages to a given topic (multiple
"  sources) with rate control.

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
static const char *rcsid_example_lbmstrm = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmstrm.c#2 $";

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

#if defined(_MSC_VER)
#define TSTLONGLONG LONGLONG
#define TST_LARGE_INT_UNION LARGE_INTEGER
#else
#define TSTLONGLONG signed long long
typedef union {
  struct {
    unsigned long LowPart; long HighPart;
  } u;
  TSTLONGLONG QuadPart;
} TST_LARGE_INT_UNION;
#endif

/* high-res time stats at startup */
TST_LARGE_INT_UNION hrt_freq;
TST_LARGE_INT_UNION hrt_start_cnt;

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

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_MAX_MESSAGES 10000000
#define MAX_NUM_SRCS 100000
#define DEFAULT_NUM_SRCS 100
#define DEFAULT_NUM_THREADS 1
#define DEFAULT_MSGS_PER_SEC 10000
#define DEFAULT_TOPIC_ROOT "29west.example.multi"
#define DEFAULT_INITIAL_TOPIC_NUMBER 0
#define DEFAULT_MAX_NUM_TRANSPORTS 10
#define MAX_MESSAGES_INFINITE 0xEFFFFFFF

lbm_event_queue_t *evq = NULL;
lbm_src_t **srcs = NULL;
char *message = NULL;
int stats_timer_id = -1, done_sending = 0;
lbm_ulong_t stats_sec = 0;

const char Purpose[] = "Purpose: Send rate-controlled messages on multiple topics.";
const char Usage[] =
"Usage: %s [options]\n"
"  Topic names generated as a root, followed by a dot, followed by an integer.\n"
"  By default, the first topic created will be '29west.example.multi.0'\n"
"Available options:\n"
"  -c, --config=FILE         Use LBM configuration file FILE.\n"
"                            Multiple config files are allowed.\n"
"                            Example:  '-c file1.cfg -c file2.cfg'\n"
"  -h, --help                display this help and exit\n"
"  -H, --hf                  Use hot failover sources\n"
"  -i, --initial-topic=NUM   use NUM as initial topic number [0]\n"
"  -j, --late-join=NUM       enable Late Join with specified retention buffer size (in bytes)\n"
"  -l, --length=NUM          send messages of length NUM bytes [25]\n"
"  -L, --linger=NUM          linger for NUM seconds after done [10]\n"
"  -m, --message-rate=NUM    send at NUM messages per second [10000]\n"
"  -M, --messages=NUM        send maximum of NUM messages [10000000]\n"
"  -r, --root=STRING         use topic names with root of STRING [29west.example.multi]\n"
"  -R, --rate=[UM]DATA/RETR  Set transport type to LBT-R[UM], set data rate limit to\n"
"                            DATA bits per second, and set retransmit rate limit to\n"
"                            RETR bits per second.  For both limits, the optional\n"
"                            k, m, and g suffixes may be used.  For example,\n"
"                            '-R 1m/500k' is the same as '-R 1000000/500000'\n"
"  -s, --statistics=NUM      print stats every NUM seconds\n"
"  -S, --sources=NUM         use NUM sources [100]\n"
"  -t, --tight               tight loop (cpu-bound) for even message spacing\n"
"  -T, --threads=NUM         use NUM threads [1]\n"
"  -x, --bits=NUM			 use NUM bits for hot failover sequence number size (32 or 64)"

MONOPTS_SENDER
MONMODULEOPTS_SENDER;

const char * OptionString = "c:hHi:j:l:L:m:M:r:R:s:S:tT:x:";
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
	{ "help", no_argument, NULL, 'h' },
	{ "hf", no_argument, NULL, 'H' },
	{ "initial-topic", required_argument, NULL, 'i' },
	{ "late-join", required_argument, NULL, 'j' },
	{ "length", required_argument, NULL, 'l' },
	{ "linger", required_argument, NULL, 'L' },
	{ "message-rate", required_argument, NULL, 'm' },
	{ "messages", required_argument, NULL, 'M' },
	{ "root", required_argument, NULL, 'r' },
	{ "rate", required_argument, NULL, 'R' },
	{ "statistics", required_argument, NULL, 's' },
	{ "sources", required_argument, NULL, 'S' },
	{ "tight", no_argument, NULL, 't' },
	{ "threads", required_argument, NULL, 'T' },
	{ "bits", required_argument, NULL, 'x' },
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
	char transport_options_string[1024];		/* Transport Options given to lbmmon_sctl_create() */
	char format_options_string[1024];			/* Format Options given to lbmmon_sctl_create() */
	char application_id_string[1024];			/* Application ID given to lbmmon_context_monitor() */
	int totalmsgsleft;							/* Number of messages to be sent */
	size_t msglen;								/* Length of messages to be sent */
	unsigned long int latejoin_threshold;		/* Maximum Late Join buffer size, in bytes */
	int pause;									/* Pause interval between messages */
	int delay,linger;							/* Interval to linger before and after sending messages */
	int block;									/* Flag to control whether blocking sends are used	*/
	lbm_uint64_t rm_rate, rm_retrans;			/* Rate control values */
	lbm_ulong_t stats_sec;						/* Interval for dumping statistics */
	int verifiable_msgs;						/* Flag to control message verification (verifymsg.h) */
	int monitor_context;						/* Flag to control context level monitoring */
	int monitor_context_ivl;					/* Interval for context level monitoring */
	int monitor_source;							/* Flag to control source level monitoring */
	int monitor_source_ivl;						/* Interval for source level monitoring */
	lbmmon_transport_func_t * transport;		/* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;				/* Function pointer to chosen format module */
	char topicroot[80];							/* The topic to be sent on */
	int initial_topic_number;					/* Topic number to start at xxx.i */
	int msgs_per_sec;							/* Rate to run sources at */
	int num_thrds;								/* Number of threads to send on */
	int num_srcs;								/* Number of soruces to send on */
	int tight_loop;								/* Use a tight loop algorithm vs sleeping */
	char rm_protocol;							/* LBTRM or LBTRU protocol */
	int hf;										/* Use Hot Failover Sources */
	int bits;									/* HF sequence number bit size, 32 or 64 */
};
struct Options options;

void cur_usec_ofs(TSTLONGLONG *quadp)
{
	TST_LARGE_INT_UNION hrt_now_cnt;
	static TSTLONGLONG onemillion = 1000000;

#if defined(_MSC_VER)
	QueryPerformanceCounter(&hrt_now_cnt);
#else
	struct timeval now_tv;
	TSTLONGLONG perf_cnt;
	gettimeofday(&now_tv, NULL);
	perf_cnt = now_tv.tv_sec;
	perf_cnt *= 1000000;
	perf_cnt += now_tv.tv_usec;
	hrt_now_cnt.QuadPart = perf_cnt;
#endif

	*quadp = (((hrt_now_cnt.QuadPart - hrt_start_cnt.QuadPart) * onemillion)
												/ hrt_freq.QuadPart);
}  /* cur_usec_ofs */


/* Source event handler callback (passed into lbm_src_create()) */
int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
	switch (event) {
	case LBM_SRC_EVENT_CONNECT:
		{
			/* const char *clientname = (const char *)ed;

			printf("Receiver connect [%s]\n",clientname); */
		}
		break;
	case LBM_SRC_EVENT_DISCONNECT:
		{
			/* const char *clientname = (const char *)ed;

			printf("Receiver disconnect [%s]\n",clientname); */
		}
		break;
	case LBM_SRC_EVENT_WAKEUP:
		break;
	case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
	case LBM_SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
	case LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX:
	case LBM_SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX:
		/* Provided to enable quiet usage of lbmstrm with UME */
		break;
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

/* Print transport statistics */
void print_stats(FILE *fp, lbm_src_transport_stats_t *stats)
{
	fprintf(fp, "[%s]", stats->source);
	switch (stats->type) {
	case LBM_TRANSPORT_STAT_TCP:
		fprintf(fp, " buffered %lu, clients %lu\n", stats->transport.tcp.bytes_buffered,
				stats->transport.tcp.num_clients);
		break;
	case LBM_TRANSPORT_STAT_LBTRM:
		fprintf(fp, " sent %lu/%lu, txw %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu, rctlr %lu/%lu\n",
				stats->transport.lbtrm.msgs_sent, stats->transport.lbtrm.bytes_sent,
				stats->transport.lbtrm.txw_msgs, stats->transport.lbtrm.txw_bytes,
				stats->transport.lbtrm.naks_rcved, stats->transport.lbtrm.nak_pckts_rcved,
				stats->transport.lbtrm.naks_ignored, stats->transport.lbtrm.naks_rx_delay_ignored,
				stats->transport.lbtrm.naks_shed,
				stats->transport.lbtrm.rxs_sent,
				stats->transport.lbtrm.rctlr_data_msgs, stats->transport.lbtrm.rctlr_rx_msgs);
		break;
	case LBM_TRANSPORT_STAT_LBTRU:
		fprintf(fp, " clients %lu, sent %lu/%lu, naks %lu/%lu, ignored %lu/%lu, shed %lu, rxs %lu\n",
				stats->transport.lbtru.num_clients,
				stats->transport.lbtru.msgs_sent, stats->transport.lbtru.bytes_sent,
				stats->transport.lbtru.naks_rcved, stats->transport.lbtru.nak_pckts_rcved,
				stats->transport.lbtru.naks_ignored, stats->transport.lbtru.naks_rx_delay_ignored,
				stats->transport.lbtru.naks_shed,
				stats->transport.lbtru.rxs_sent);
		break;
	case LBM_TRANSPORT_STAT_LBTIPC:
		fprintf(fp, " clients %lu, sent %lu/%lu\n",
				stats->transport.lbtipc.num_clients,
				stats->transport.lbtipc.msgs_sent, stats->transport.lbtipc.bytes_sent);
		break;
	case LBM_TRANSPORT_STAT_LBTRDMA:
		fprintf(fp, " clients %lu, sent %lu/%lu\n",
				stats->transport.lbtrdma.num_clients,
				stats->transport.lbtrdma.msgs_sent, stats->transport.lbtrdma.bytes_sent);
		break;
	default:
		break;
	}
	fflush(fp);
}

/* Timer callback to handle periodic display of source statistics */
int handle_stats_timer(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_transport_stats_t stats[DEFAULT_MAX_NUM_TRANSPORTS];
	int num_transports = DEFAULT_MAX_NUM_TRANSPORTS;

	if (lbm_context_retrieve_src_transport_stats(ctx, &num_transports, stats) != LBM_FAILURE) {
		int scount = 0;

		for (scount = 0; scount < num_transports; scount++) {
			fprintf(stdout, "stats %u/%u:", scount+1, num_transports);
			print_stats(stdout, &stats[scount]);
		}
	} else {
		fprintf(stderr, "lbm_context_retrieve_src_transport_stats: %s\n", lbm_errmsg());
	}
	if (!done_sending) {
		if ((stats_timer_id = lbm_schedule_timer(ctx, handle_stats_timer, ctx, NULL, (stats_sec * 1000))) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	return 0;
}

#if defined(_WIN32)
#  define MAX_NUM_THREADS 16
int thrdidxs[MAX_NUM_THREADS];
int msgsleft[MAX_NUM_THREADS];
#else
#  define MAX_NUM_THREADS 16
int thrdidxs[MAX_NUM_THREADS];
int msgsleft[MAX_NUM_THREADS];
#endif /* _WIN32 */

/*
 * Per thread sending loop
 */
#if defined(_WIN32)
DWORD WINAPI sending_thread_main(void *arg)
#else
void *sending_thread_main(void *arg)
#endif /* _WIN32 */
{
	int i = 0, done = 0, block_cntr, thrdidx = *((int *)arg), rc = 0;
	lbm_uint64_t count = 0;
	lbm_src_send_ex_info_t hfexinfo;
	TSTLONGLONG msg_num = 0;
	TSTLONGLONG next_msg_usec;
	TSTLONGLONG cur_usec;
	TSTLONGLONG thrd_msgs_per_sec = (TSTLONGLONG)(options.msgs_per_sec / options.num_thrds);

	/* set up exinfo to send specified HF bit size */
	if (options.hf) {
		memset(&hfexinfo, 0, sizeof(hfexinfo));
		hfexinfo.flags = options.bits == 64 ? LBM_SRC_SEND_EX_FLAG_HF_64 : LBM_SRC_SEND_EX_FLAG_HF_32;
	}
	/*
	 * Send to each source in turn until we have sent the max number
	 * of messages total.
	 */
	block_cntr = 0;
	i = thrdidx;
	/* printf("msgs = %u\n", msgsleft[thrdidx]); */
	while (msgsleft[thrdidx] > 0 || msgsleft[thrdidx] == MAX_MESSAGES_INFINITE) {
		cur_usec_ofs(&cur_usec);

		next_msg_usec = (msg_num * 1000000) / thrd_msgs_per_sec;
		if (options.tight_loop) {
			/* burn CPU till it's time to send one or more msgs */
			while (cur_usec < next_msg_usec) {
				cur_usec_ofs(&cur_usec);
			}
		}
		else {
			/* sleep till it's time to send one or more msgs */
			while (cur_usec < next_msg_usec) {
				SLEEP_MSEC(20);
				cur_usec_ofs(&cur_usec);
			}
		}

		sprintf(message, "message %"PRIu64, count);

		if(options.hf) {
			if (options.bits == 64)
				hfexinfo.hf_sqn.u64 = count;
			else 
				hfexinfo.hf_sqn.u32 = (lbm_uint32_t)count;
			rc = lbm_hf_src_send_ex(srcs[i], message, options.msglen, 0, LBM_SRC_NONBLOCK, &hfexinfo) == LBM_FAILURE;
		}
		else {
			rc = lbm_src_send(srcs[i], message, options.msglen, LBM_SRC_NONBLOCK) == LBM_FAILURE;
		}

		if (rc == LBM_FAILURE) {
			if (lbm_errnum() == LBM_EWOULDBLOCK) {
				block_cntr++;
				if (block_cntr % 1000 == 0) {
					printf("LBM send blocked 1000 times\n");
				}
			} else {
				fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
		}

		++ msg_num;
		i += options.num_thrds;
		if (i >= options.num_srcs)
			i = thrdidx;
		if (msgsleft[thrdidx] != MAX_MESSAGES_INFINITE) {
			if (--msgsleft[thrdidx] <= 0) {
				done = 1;
				break;
			}
		}

		if (done)
			break;
	}  /* while */
	return 0;
}

void process_cmdline(int argc, char **argv,struct Options *opts)
{
	int c, errflag = 0;
	
	opts->initial_topic_number = DEFAULT_INITIAL_TOPIC_NUMBER;
	opts->msgs_per_sec = DEFAULT_MSGS_PER_SEC;
	opts->msglen = MIN_ALLOC_MSGLEN;
	opts->totalmsgsleft = DEFAULT_MAX_MESSAGES;
	opts->num_thrds = DEFAULT_NUM_THREADS;
	opts->num_srcs = DEFAULT_NUM_SRCS;
	opts->linger = 10;

	strcpy(opts->topicroot,DEFAULT_TOPIC_ROOT);
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();

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
			case 'H':
				opts->hf = 1;
				break;
			case 'i':
				opts->initial_topic_number = atoi(optarg);
				break;
			case 'j':
				if (sscanf(optarg, "%lu", &opts->latejoin_threshold) != 1)
					++errflag;
				break;
			case 'l':
				opts->msglen = atoi(optarg);
				break;
			case 'L':
				opts->linger = atoi(optarg);
				break;
			case 'm':
				opts->msgs_per_sec = atoi(optarg);
				break;
			case 'M':
				if (strncmp(optarg, "I", 1) == 0)
				{
					opts->totalmsgsleft = MAX_MESSAGES_INFINITE;
				}
				else
				{
					opts->totalmsgsleft = atoi(optarg);
				}
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			case 'r':
				strncpy(opts->topicroot, optarg, sizeof(opts->topicroot));
				break;
			case 'R':
				errflag += parse_rate(optarg, &opts->rm_protocol, &opts->rm_rate, &opts->rm_retrans);
				break;
			case 's':
				opts->stats_sec = atoi(optarg);
				break;
			case 'S':
				opts->num_srcs = atoi(optarg);
				if (opts->num_srcs > MAX_NUM_SRCS)
				{
					fprintf(stderr, "Too many sources specified. Max number of sources is %d.\n",MAX_NUM_SRCS);
					errflag++;
				}
				break;
			case 't':
				opts->tight_loop = 1;
				break;
			case 'T':
				opts->num_thrds = atoi(optarg);
				if (opts->num_thrds > MAX_NUM_THREADS)
				{
					fprintf(stderr, "Too many threads specified. Max number of threads is %d.\n",MAX_NUM_THREADS);
					errflag++;
				}
				break;
			case 'x':
				opts->bits = atoi(optarg);
				if (opts->bits != 32 && opts->bits != 64) {
					fprintf(stderr, "Hot failover bit size %u not 32 or 64, using 32 bit sequence numbers", opts->bits);
					opts->bits = 32;
				}
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
	if (errflag != 0)
	{
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	int i = 0;
	struct Options *opts = &options;
	lbm_context_t *ctx;
	lbm_topic_t *topic = NULL;
	lbm_src_topic_attr_t * tattr;
	lbm_context_attr_t * cattr;
	char topicname[LBM_MSG_MAX_TOPIC_LEN];
	char * application_id = NULL;
	lbmmon_sctl_t * monctl;
#if defined(_WIN32)
	HANDLE wthrdh[MAX_NUM_THREADS];
	DWORD wthrdids[MAX_NUM_THREADS];
#else
	pthread_t pthids[MAX_NUM_THREADS];
#endif /* _WIN32 */

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

	/* if message buffer is too small, then the sprintf will cause issues. So, allocate with a min size */
	if (opts->msglen < MIN_ALLOC_MSGLEN) {
		message = malloc(MIN_ALLOC_MSGLEN);
	} else {
		message = malloc(opts->msglen);
	}

	if (message == NULL) {
		fprintf(stderr, "could not allocate message buffer of size %u bytes\n", opts->msglen);
		exit(1);
	}
	
	memset(message, 0, opts->msglen);

	printf("%d msgs/sec\n", opts->msgs_per_sec);

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
	/* Create LBM context (passing in context attributes) */
	if (lbm_context_create(&ctx, cattr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(cattr);

	if (opts->monitor_context || opts->monitor_source)
	{
		char * transport_options = NULL;
		char * format_options = NULL;

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
	}
	if (opts->monitor_context)
	{
		if (lbmmon_context_monitor(monctl, ctx, application_id, opts->monitor_context_ivl) == -1)
		{
			fprintf(stderr, "lbmmon_context_monitor() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}
	}
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

	if ((srcs = malloc(sizeof(lbm_src_t *) * MAX_NUM_SRCS)) == NULL) {
		fprintf(stderr, "could not allocate sources array\n");
		exit(1);
	}

	/* create all the sources */
	printf("Creating %d sources\n", opts->num_srcs);
	for (i = 0; i < opts->num_srcs; i++) {
		sprintf(topicname, "%s.%d", opts->topicroot, (i + opts->initial_topic_number));
		topic = NULL;
		/* First allocate the desired topic */
		if (lbm_src_topic_alloc(&topic, ctx, topicname, tattr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		/*
		 * Create LBM source passing in the allocated topic and event
		 * handler. The source object is returned here in &(srcs[i]).
		 */
		if (lbm_src_create(&(srcs[i]), ctx, topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (opts->monitor_source)
		{
			char appid[1024];
			char * appid_ptr = NULL;
			if (application_id != NULL)
			{
				snprintf(appid, sizeof(appid), "%s(%d)", application_id, i);
				appid_ptr = appid;
			}
			if (lbmmon_src_monitor(monctl, srcs[i], appid_ptr, opts->monitor_source_ivl) == -1)
			{
				fprintf(stderr, "lbmmon_src_monitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		/* printf("Created source %d - '%s'\n",i,topicname); */
		if (i > 1 && (i % 1000) == 0)
			printf("Created %d sources\n", i);
	}
	printf("Created %d Sources. Will start sending data now.\n",opts->num_srcs);
	lbm_src_topic_attr_delete(tattr);

	SLEEP_SEC(2);

	/* Get the frequency and initial count for the high-res counter */
#if defined(_MSC_VER)
	QueryPerformanceCounter(&hrt_start_cnt);
	QueryPerformanceFrequency(&hrt_freq);
#else
	{
		struct timeval now_tv;
		TSTLONGLONG perf_cnt;

		gettimeofday(&now_tv, NULL);
		perf_cnt = now_tv.tv_sec;
		perf_cnt *= 1000000;
		perf_cnt += now_tv.tv_usec;
		hrt_start_cnt.QuadPart = perf_cnt;
		hrt_freq.QuadPart = 1000000;  /* unix */
	}
#endif

	printf("Using %d threads to send.\n",opts->num_thrds);
	/* Divide sending load amongst available threads */
	for (i = 1; i < opts->num_thrds; i++) {
#if defined(_WIN32)
		thrdidxs[i] = i;
		if (opts->totalmsgsleft == MAX_MESSAGES_INFINITE) {
			msgsleft[i] = MAX_MESSAGES_INFINITE;
		} else {
			msgsleft[i] = opts->totalmsgsleft / opts->num_thrds;
		}
		if ((wthrdh[i] = CreateThread(NULL, 0, sending_thread_main, &(thrdidxs[i]), 0,
									  &(wthrdids[i]))) == NULL) {
			fprintf(stderr, "could not create thread\n");
			exit(1);
		}
#else
		thrdidxs[i] = i;
		if (opts->totalmsgsleft == MAX_MESSAGES_INFINITE) {
			msgsleft[i] = MAX_MESSAGES_INFINITE;
		} else {
			msgsleft[i] = opts->totalmsgsleft / opts->num_thrds;
		}
		if (pthread_create(&(pthids[i]), NULL, sending_thread_main, &(thrdidxs[i])) != 0) {
			fprintf(stderr, "could not spawn thread\n");
			exit(1);
		}
#endif /* _WIN32 */
	}
	thrdidxs[0] = 0;
	if (opts->totalmsgsleft == MAX_MESSAGES_INFINITE) {
		msgsleft[0] = MAX_MESSAGES_INFINITE;
	} else {
		msgsleft[0] = opts->totalmsgsleft / opts->num_thrds;
	}
	if (opts->stats_sec > 0) {
		/* Schedule time to handle statistics display. */
		stats_sec = opts->stats_sec;
		if ((stats_timer_id = lbm_schedule_timer(ctx, handle_stats_timer, ctx, NULL, (stats_sec * 1000))) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	sending_thread_main(&(thrdidxs[0]));

	done_sending = 1;
	/* we do this before the linger, so some things may be in batching, etc. and not show up in stats. */
	handle_stats_timer(ctx, ctx);
	/*
	 * Linger allows transport to complete data transfer and recovery before
	 * context is deleted and socket is torn down.
	 */
	printf("Lingering for %d seconds...\n",opts->linger);
	SLEEP_SEC(opts->linger);

	if (opts->monitor_context || opts->monitor_source)
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
			for (i = 0; i < opts->num_srcs; ++i)
			{
				if (lbmmon_src_unmonitor(monctl, srcs[i]) == -1)
				{
					fprintf(stderr, "lbmmon_src_unmonitor() failed, %s\n", lbmmon_errmsg());
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
	 * Although not strictly necessary in this example, delete allocated
	 * sources and LBM context.
	 */
	printf("Deleting sources....\n");
	for (i = 0; i < opts->num_srcs; i++) {
		lbm_src_delete(srcs[i]);
		srcs[i] = NULL;
		if (i > 1 && (i % 1000) == 0)
			printf("Deleted %d sources\n",i);
	}
	lbm_context_delete(ctx);
	ctx = NULL;
	printf("Quitting....\n");
	free(srcs);
	free(message);
	return 0;
}





