/*
"lbmhfxrcv.c: application that receives messages from a given topic
"  (single receiver).

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
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "monmodopts.h"
#include "verifymsg.h"
#include "replgetopt.h"
#include "lbm-example-util.h"

static const char *rcsid_example_lbmrcv = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmhfxrcv.c#2 $";

#if defined(_WIN32)
#   define SLEEP_SEC(x) Sleep(x*1000)
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

#define MAX_INTERFACES 10

const char purpose[] = "Purpose: Receive messages on a single topic.";
const char usage[] =
"Usage: lbmrcv [-aACdEfhqsSvV] [-I interface] [-c filename] [-r msgs] [-U losslev] topic\n"
"Available options:\n"
"  -a, --arrival-order  deliver messages in the order that they arrive.\n"
"  -A, --ascii          display messages as ASCII text (-A -A = newlines after each msg)\n"
"  -c, --config=FILE    Use LBM configuration file FILE.\n"
"                       Multiple config files are allowed.\n"
"                       Example:  '-c file1.cfg -c file2.cfg'\n"
"  -C, --context-stats  fetch context rather than receiver stats\n"
"  -d, --deliver-dups	Enable duplicate delivery\n"
"  -E, --exit           exit when source stops sending\n"
"  -h, --help           display this help and exit\n"
"  -I, --iface=CIDR		create a context on the interface specified by CIDR\n"
"						Multiple interfaces are allowed.\n"
"						Example: '-I 10.29.1.0/24 -I 10.29.2.0/24'\n"
"  -q, --eventq         use an LBM event queue\n"
"  -r, --msgs=NUM       exit after NUM messages\n"
"  -O, --orderchecks     Enable message order checking\n"
"  -s, --stats=NUM      print LBM statistics every NUM seconds\n"
"  --max-sources=NUM    allow up to NUM sources (for statistics gathering purposes)\n"
"  -S, --stop           exit when source stops sending, and print throughput summary\n"
"  -U, --losslev=NUM    exit after NUM% unrecoverable loss\n"
"  -v, --verbose        be verbose about incoming messages (-v -v = be even more verbose)\n"
"  -V, --verify         verify message contents\n"
MONOPTS_RECEIVER
MONMODULEOPTS_SENDER;

const char * OptionString = "Aac:CdEfI:Oqr:N:s:SU:vV";
#define OPTION_MONITOR_RCV 0
#define OPTION_MONITOR_CTX 1
#define OPTION_MONITOR_TRANSPORT 2
#define OPTION_MONITOR_TRANSPORT_OPTS 3
#define OPTION_MONITOR_FORMAT 4
#define OPTION_MONITOR_FORMAT_OPTS 5
#define OPTION_MONITOR_APPID 6
#define OPTION_MAX_SOURCES 7
const struct option OptionTable[] = {
	{ "arrival-order", no_argument, NULL, 'a' },
	{ "ascii", no_argument, NULL, 'A' },
	{ "config", required_argument, NULL, 'c' },
	{ "context-stats", no_argument, NULL, 'C' },
	{ "deliver-dups", no_argument, NULL, 'd' },
	{ "exit", no_argument, NULL, 'E' },
	{ "help", no_argument, NULL, 'h' },
	{ "iface", required_argument, NULL, 'I' },
	{ "eventq", no_argument, NULL, 'q' },
	{ "msgs", required_argument, NULL, 'r' },
	{ "stats", required_argument, NULL, 's' },
	{ "summary", no_argument, NULL, 'S' },
	{ "losslev", required_argument, NULL, 'U' },
	{ "orderchecks", required_argument, NULL, 'O' },
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
	int arrival_order;			  /* Flag to turn on arrival-order delivery */
	int ascii;                    /* Flag to display messages as ASCII text */
	int context_stats;            /* Flag to fetch context rather than receiver stats */
	int duplicates;				  /* Flag to turn on duplicate delivery */
	int end_on_end;               /* Flag to end program when source stops sending */
	int eventq;                   /* Flag to use an LBM event queue for the receiver */
	int reap_msgs;                /* If nonzero, end when msgs rcv'd >= reap_msgs */
	int stats_ivl;                /* Interval for dumping statistics, in seconds */
	int summary;                  /* Flag to show summary when source stops sending */
	int losslev;                  /* If nonzero, end if % lost to rcv'd msgs > losslev */
	int verbose;                  /* Flag to control program verbosity */
	int verify_msgs;              /* Flag to use message verification (verifymsg.h) */
	char *topic;                  /* The topic on which to receive messages */
	int orderchecks;              /* Flag to turn on order checks */

	/* LBM monitoring options */
	int max_sources;                     /* Maximum number of source statistics to display */
	char transport_options_string[1024]; /* Transport options given to lbmmon_sctl_create() */
	char format_options_string[1024];    /* Format options given to lbmmon_sctl_create()  */
	char application_id_string[1024];    /* Application ID given to lbmmon_context_monitor() */
	int monitor_context;           	    /* Flag to control context level monitoring */
	int monitor_context_ivl;             /* Interval for context level monitoring */
	int monitor_receiver;                /* Flag to control receiver level monitoring */
	int monitor_receiver_ivl;            /* Interval for receiver level monitoring */
	lbmmon_transport_func_t * transport; /* Function pointer to chosen transport module */
	lbmmon_format_func_t * format;       /* Function pointer to chosen format module  */
	char *interfaces[MAX_INTERFACES];
	int num_ifaces;
} options;


#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10


int msg_count = 0;
int rx_msg_count = 0;
int otr_msg_count = 0;
int total_msg_count = 0;
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
int opmode;								/* operational mode of LBM: sequential or embedded */
lbm_context_t *ctxs[MAX_INTERFACES];					/* ptr to context object */
lbm_hfx_rcv_t *hfxrcvs[MAX_INTERFACES];
int num_ctxs = 0;

struct timeval data_start_tv, data_end_tv; /* to track time since first message rcv'd */
struct timeval starttv, endtv; 	/* to track time between printing bandwidth stats */
struct timeval stattv; 				/* to track time between printing LBM transport stats */
int timer_id = -1;

lbm_uint_t expected_sqn = 0;
lbm_ulong_t lost, last_lost = 0;
lbm_rcv_transport_stats_t *stats = NULL;
int nstats;

char saved_source[LBM_MSG_MAX_SOURCE_LEN] = "";
lbm_event_queue_t *evq = NULL;

/*
 * For the elapsed time, calculate and print the msgs/sec, bits/sec, and
 * loss stats
 */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned int bytes, int unrec, lbm_ulong_t lost, int rx_msgs, int otr_msgs)
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

	if ((rx_msgs != 0) || (otr_msgs != 0))
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rx_msgs, otr_msgs);
	else
		fprintf(fp, "%-6.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
	if (lost != 0 || unrec != 0 || burst_loss != 0) {
		fprintf(fp, " [%lu pkts lost, %u msgs unrecovered, %d loss bursts]",
			lost, unrec, burst_loss);
	}
	fprintf(fp, "\n");
	fflush(fp);
	burst_loss = 0;
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
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

/* Helper function for rcv_handle_msg callback */
int check_optional_end_conditions()
{
	struct Options *opts = &options;

	if ((opts->reap_msgs > 0 && total_msg_count >= opts->reap_msgs) || close_recv) {
		/*
		 * Close receiver if we've received all we
		 * wanted or if the sender has gone away.
		 */
		printf("Quitting.... received %u messages\n", total_msg_count);

		close_recv = 1;
		return 1;
	}
	if ((opts->losslev > 0) && (total_msg_count > 0) &&
	   ((100 * total_unrec_count) / total_msg_count) >= opts->losslev) {
		/*
		 * Close receiver if unrecoverable loss reaches or exceeds losslev %
		 */
		printf("Quitting.... %d msgs unrecovered, %d msgs received (losslev %d%%)\n",
			total_unrec_count, total_msg_count,
			((100 * total_unrec_count) / total_msg_count));

		close_recv = 1;
		return 1;
	}
	return 0;
}

/* Handler for unrecoverable loss from multicast immediate messages */
int rcv_handle_mim_unrecloss(const char *source_name, lbm_uint_t sqn, void *clientd)
{
	struct Options *opts = &options;

	unrec_count++;
	total_unrec_count++;
	if (opts->verbose)
		printf("MIM Loss: [%s][%u]\n", source_name, sqn);

	return 1;
}

/*
 * Handler for immediate messages directed to NULL topic
 * callback is set as a parameter of lbm_context_rcv_immediate_msgs()
 */
int rcv_handle_immediate_msg(lbm_context_t *ctx, lbm_msg_t *msg, void *clientd)
{
	struct Options *opts = &options;

	if (close_recv)
		return 0; /* skip any new messages if we're just waiting to exit */

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
			fflush(stdout);
		}

		if (opts->verbose) {
			printf("IM [%s][%u], %lu bytes\n",
					msg->source, msg->sequence_number, msg->len);
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
			fflush(stdout);
		}
		if (opts->verbose) {
			printf("IM Request [%s][%u], %lu bytes\n",
					msg->source, msg->sequence_number, msg->len);
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

/* Received message handler (passed into lbm_hfx_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	static int lastseq = -1;
	struct Options *opts = &options;

	if (close_recv)
		return 0; /* skip any new messages if we're just waiting to exit */

	switch (msg->type) {
	case LBM_MSG_DATA:
		if(options.orderchecks && msg->sequence_number != lastseq + 1 && lastseq != -1)
			printf("*** Warning - misordered seq num %d %d\n",lastseq,msg->sequence_number);
		lastseq = msg->sequence_number;
		
		/* Data message received */
		(stotal_msg_count == 0) ? current_tv (&data_start_tv) : current_tv(&data_end_tv);
		msg_count++;
		total_msg_count++;
		stotal_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		total_byte_count += msg->len;

		if (msg->flags & LBM_MSG_FLAG_RETRANSMIT)
			rx_msg_count++;
		if (msg->flags & LBM_MSG_FLAG_OTR)
			otr_msg_count++;

		if (opts->ascii) {
			int n = msg->len;
			const char *p = msg->data;
			printf("[%s]", opts->interfaces[(int)clientd]);
			while (n--)
			{
				putchar(*p++);
			}
			if (opts->ascii > 1) putchar('\n');
			fflush(stdout);
		}
		if (opts->verbose)
		{
			printf("[@%d.%06d]", msg->tsp.tv_sec, msg->tsp.tv_usec);
			printf("[%s][%s][%u]%s%s%s%s, %lu bytes\n",
				msg->topic_name, msg->source, msg->sequence_number,
				((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX-" : ""),
				((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""),
				((msg->flags & LBM_MSG_FLAG_HF_DUPLICATE) ? "-HFDUP-" : ""),
				((msg->flags & LBM_MSG_FLAG_HF_PASS_THROUGH) ? "-PASS-" : ""),
				msg->len);

			if (opts->verbose > 1)
				dump(msg->data, msg->len);
		}
		if (opts->verify_msgs)
		{
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
			printf("[%s][%s][%u], LOST\n",
					msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		if (opts->verbose) {
			printf("[%s][%s][%u], LOSS BURST\n",
					msg->topic_name, msg->source, msg->sequence_number);
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
		lastseq = -1;
		subtotal_msg_count = 0;
		/*
		 * Set saved_source[0] to NULL terminate the string. We are
		 * only printing stats for 1 session at a time. So, when we
		 * get an EOS indication, we NULL out the string so we wait
		 * for the next message to save the source again.
		 */
		saved_source[0] = 0;
		/* When verifying sequence numbers, multiple sources or EOS and new sources will cause
		 * the verification to fail as we don't track the numbers on a per source basis.
		 */
		if (opts->end_on_end)
			close_recv = 1;
		break;
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		printf("[%s], no sources found for topic\n", msg->topic_name);
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	if (check_optional_end_conditions()) {
		/*
		 * If we've received all that we wanted or the source has
		 * gone away or unrecoverable loss has exceeded losslev%,
		 * unblock the event queue dispatcher (forcing it to return).
		 */
		if (opts->eventq) { /* if using an event queue, unblock it */
			if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
				fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
				exit(1);
			}
		} else { /* we have to wait for the sleep in the main thread */
			close_recv = 1; /* so stop processing new messages until then */
		}
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}


/* Event queue monitor callback (passed into lbm_event_queue_create()) */
int evq_monitor(lbm_event_queue_t *evq, int event, size_t evq_size,
				lbm_ulong_t event_delay_usec, void *clientd)
{
	printf("event queue threshold exceeded - event %x, sz %lu, delay %lu\n",
		   event, evq_size, event_delay_usec);
	return 0;
}

/*
 * Timer handler (passed into lbm_schedule_timer()) used to print bandwidth
 * usage stats once per second and LBM stats every opts->stat_ivl seconds.
 */
int rcv_handle_tmo(lbm_context_t *ctx, const void *clientd)
{
	struct Options *opts = &options;
	lbm_ulong_t lost_tmp;
	int flPrintStats = 0;
	int count = 0;
	int i=0;
	int have_stats = 0, set_nstats;
	if (!opts->stats_ivl && opts->ascii)
		return 0;

	timer_id = -1;
	current_tv(&endtv);

	if ( opts->stats_ivl ) {
		flPrintStats = ( ( endtv.tv_sec > stattv.tv_sec ) ||
			 ( endtv.tv_sec == stattv.tv_sec && endtv.tv_usec >= stattv.tv_usec ) ) ? 1 : 0;
	}

	/* Loop over all contexts */
	lost = 0;
	count = 0;
	do {
		/* Retrieve either context or receiver stats */
		if (opts->context_stats)
		{
			while (!have_stats){
				set_nstats = nstats;
				if (lbm_context_retrieve_rcv_transport_stats(ctxs[i], &set_nstats, stats) == LBM_FAILURE){
					/* Double the number of stats passed to the API to be retrieved */
					/* Do so until we retrieve stats successfully or hit the max limit */
					nstats *= 2;
					if (nstats > DEFAULT_MAX_NUM_SRCS){
						fprintf(stderr, "Cannot retrieve all context stats (%s).  Maximum number of sources = %d.\n",
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

		else
		{
			while (!have_stats){
				set_nstats = nstats;
				if (lbm_rcv_retrieve_all_transport_stats(lbm_rcv_from_hfx_rcv(hfxrcvs[i]), &set_nstats, stats) == LBM_FAILURE){
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

		for (; count < set_nstats; count++)
		{
			if ( flPrintStats ) {
				if (set_nstats > 1)
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
	}
	while(++i < opts->num_ifaces);

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

		print_bw(stdout, &endtv, msg_count, byte_count, unrec_count, lost, rx_msg_count, otr_msg_count);
	}

	msg_count = 0;
	rx_msg_count = 0;
	otr_msg_count = 0;
	byte_count = 0;
	unrec_count = 0;

	if ( flPrintStats ) {
		current_tv ( &stattv );
		stattv.tv_sec += opts->stats_ivl;
	}

	current_tv(&starttv);
	/* Restart timer */
	if ((timer_id = lbm_schedule_timer(ctx, rcv_handle_tmo, NULL, evq, 1000)) == -1) {
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

	memset(opts, 0, sizeof(*opts));
 	opts->max_sources = DEFAULT_NUM_SRCS;
	opts->transport_options_string[0] = '\0';
	opts->format_options_string[0] = '\0';
	opts->application_id_string[0] = '\0';
	opts->transport = (lbmmon_transport_func_t *) lbmmon_transport_lbm_module();
	opts->format = (lbmmon_format_func_t *) lbmmon_format_csv_module();

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF) {
		switch (c) {
		case 'a':
			opts->arrival_order = 1;
			break;
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
		case 'd':
			opts->duplicates = 1;
			break;
		case 'E':
			opts->end_on_end = 1;
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'I':
			if(opts->num_ifaces==MAX_INTERFACES) {
				fprintf(stderr, "The current version of lbmhfxrcv supports %d interfaces.  For more, increase the value of MAX_INTERFACES in lbmhfxrcv.c and recompile.\n\n", MAX_INTERFACES);
				++errflag;
			} else {

				opts->interfaces[opts->num_ifaces++] = optarg;
			}
			break;
		case 'q':
			opts->eventq = 1;
			break;
		case 'O':
			opts->orderchecks = 1;
			break;
		case 'r':
			opts->reap_msgs = atoi(optarg);
			opts->summary = 1;
			break;
		case 's':
			opts->stats_ivl = atoi(optarg);
			break;
		case 'S':
			opts->end_on_end = 1;
			opts->summary = 1;
			break;
		case 'U':
			opts->losslev = atoi(optarg);
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
		default:
			errflag++;
			break;
		}
	}

	if (opts->losslev > 100 || opts->losslev < 0) {
		fprintf(stderr,"Loss level percentage must be a number between 0 and 100.\n");
		errflag++;
	}

	if (errflag || (optind == argc)) {
		/* An error occurred processing the command line - dump the LBM version, usage and exit */
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	opts->topic = argv[optind];
}

int main(int argc, char **argv)
{
	struct Options *opts = &options;
	lbm_context_attr_t * ctx_attr; /* ptr to attributes for creating context */
	lbm_hfx_attr_t *hfx_attr;
	lbm_rcv_topic_attr_t *rattr;
	lbm_hfx_t *hfx;
	size_t optlen; /* to be set to length of retrieved data in LBM getopt calls */
	/* following variables are for gathering and displaying statistics */
	lbmmon_sctl_t * monctl = NULL;

	double total_time = 0.0;
	double total_mps = 0.0;
	double total_bps = 0.0;
	/* following variables are for options we want to retrieve via getopt calls */
	int i;

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

	/* Process command line options */
	process_cmdline(argc, argv, opts);

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

	/* Retrieve default / configuration-modified context settings */
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
	/*
	 * Check if operational mode is set to "sequential" meaning that all
	 * LBM processing will be done on this thread rather than on a separate
	 * thread (see while loop below).
	 */
	optlen = sizeof(opmode);
	if (lbm_context_attr_getopt(ctx_attr, "operational_mode", &opmode, &optlen) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_attr_getopt - operational mode: %s\n", lbm_errmsg());
		exit(1);
	}
	if (opmode == LBM_CTX_ATTR_OP_SEQUENTIAL) {
		printf("LBM is in sequential mode.\n");

		if (opts->eventq) {
			printf("Running an event queue on the same thread as the context can cause deadlock.\n");
			printf("Please use embedded mode or run this example again without an event queue.\n");
			exit(1);
		}
	}

	/* Set handler for unrecoverable loss from a MIM source */
	{
		lbm_mim_unrecloss_func_t unrecloss;

		unrecloss.func = rcv_handle_mim_unrecloss;
		unrecloss.clientd = NULL;
		if (lbm_context_attr_setopt(ctx_attr, "mim_unrecoverable_loss_function",
									&unrecloss, sizeof(unrecloss)) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_setopt - mim_unrecoverable_loss_function: %s\n",
					lbm_errmsg());
			exit(1);
		}
	}

	if (opts->eventq) {
		/* Create an event queue and associate it with a callback */
		if (lbm_event_queue_create(&evq, evq_monitor, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* Initialize immediate message handler (for topicless immediate sends) */
	{
		lbm_context_rcv_immediate_msgs_func_t topicless_im_rcv_func;

		topicless_im_rcv_func.clientd = NULL;
		topicless_im_rcv_func.evq = evq;
		topicless_im_rcv_func.func = rcv_handle_immediate_msg;
		if (lbm_context_attr_setopt(ctx_attr, "immediate_message_receiver_function",
				&topicless_im_rcv_func, sizeof(topicless_im_rcv_func)) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_rcv_immediate_msgs: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* Create an lbm context for each interface specified. */
	if(opts->num_ifaces > 0)
	{

		for(i=0;i<opts->num_ifaces;i++) {

			/* Set the interface options */
			if(lbm_context_attr_str_setopt(ctx_attr, "resolver_multicast_interface", opts->interfaces[i]) 
					== LBM_FAILURE) {
				fprintf(stderr, "lbm_context_attr_str_setopt: %s\n", lbm_errmsg());
				exit(1);
			}

			if (lbm_context_create(&ctxs[i], ctx_attr, NULL, NULL) == LBM_FAILURE) {
				fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
				exit(1);
			}
		}

	}
	else
	{
		/* Just create a single context */
		if (lbm_context_create(&ctxs[0], ctx_attr, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
			exit(1);
		}

	}
	lbm_context_attr_delete(ctx_attr); /* attributes can be discarded after context creation */

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/* Create an event queue for the receiver if the -q cmdline option was used.
	 * Note that using an event queue is a design decision and is made optional
	 * in this program only for the purpose of demonstration.
	 */
	if (opts->eventq) {
		printf("Using an LBM event queue.\n");
	}

	if(lbm_hfx_attr_create(&hfx_attr) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_hfx_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if(opts->duplicates)
	{
		if(lbm_hfx_attr_str_setopt(hfx_attr, "duplicate_delivery", "1") == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_hfx_attr_str_setopt: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	if(opts->arrival_order)
	{
		/* -1 is arrival order with reassembly */
		if(lbm_hfx_attr_str_setopt(hfx_attr, "ordered_delivery", "-1") == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_hfx_attr_str_setopt: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* Create an HFX object.*/
	if(lbm_hfx_create(&hfx, hfx_attr, opts->topic, rcv_handle_msg, evq) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_hfx_create: %s\n", lbm_errmsg());
		exit(1);
	}

	lbm_hfx_attr_delete(hfx_attr);

	/*
	 * Create receiver objects.
	 */
	if(lbm_rcv_topic_attr_create(&rattr) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_rcv_topic_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}

	i=0;
	do {
		if(opts->num_ifaces > 0) {
			if(lbm_rcv_topic_attr_str_setopt(rattr, "transport_tcp_interface", opts->interfaces[i]) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_rcv_topic_attr_str_setopt: %s\n", lbm_errmsg());
				exit(1);
			}
			if(lbm_rcv_topic_attr_str_setopt(rattr, "transport_lbtru_interface", opts->interfaces[i]) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_rcv_topic_attr_str_setopt: %s\n", lbm_errmsg());
				exit(1);
			}
		}
		if (lbm_hfx_rcv_create(&hfxrcvs[i], hfx, ctxs[i], rattr, (void *)i)
				== LBM_FAILURE) {
			fprintf(stderr, "lbm_hfx_rcv_create: %s\n", lbm_errmsg());
			exit(1);
		}
		i++;
	} while(i<opts->num_ifaces);

	lbm_rcv_topic_attr_delete(rattr);

	current_tv(&starttv);
	/* Start up a timer to print bandwidth utilization and/or LBM stats every second */
	/* We pass our receiver to the timer's handler callback through the client data parameter */
	if ((timer_id = lbm_schedule_timer(ctxs[0], rcv_handle_tmo, NULL, evq, 1000)) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
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
		if (lbmmon_sctl_create(&monctl, opts->format, format_options,
										opts->transport, transport_options) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_create() failed, %s\n", lbmmon_errmsg());
			exit(1);
		}
		if (opts->monitor_context)
		{
			i=0;
			do {
				if (lbmmon_context_monitor(monctl, ctxs[i++], application_id, opts->monitor_context_ivl) == -1)
				{
					fprintf(stderr, "lbmmon_context_monitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			} while(i<opts->num_ifaces);
		}
		else /* monitor receiver */
		{
			i=0;
			do {
				if (lbmmon_rcv_monitor(monctl, lbm_rcv_from_hfx_rcv(hfxrcvs[i++]), application_id, opts->monitor_receiver_ivl) == -1)
				{
					fprintf(stderr, "lbmmon_rcv_monitor() failed, %s\n", lbmmon_errmsg());
					exit(1);
				}
			} while(i<opts->num_ifaces);

		}
	}

	if ( opts->stats_ivl ) {
		current_tv ( &stattv );
		stattv.tv_sec += opts->stats_ivl;
	}

	while (1) {
		if (opmode == LBM_CTX_ATTR_OP_SEQUENTIAL) {
			/* Operational mode is set to sequential, meaning no separate thread
			 * was created for the LBM context. Therefore, we have to call this
			 * function to do LBM processing (including invoking callbacks).
			 */
			if(opts->num_ifaces == 0) {
				lbm_context_process_events(ctxs[0], 1000);
			} else {
				/* Just round-robin the contexts */
				for(i=0;i<opts->num_ifaces;i++) {
					/* Preemptively unblock the context so the process
					   events call will return if there's nothing
					   to be done. */
					lbm_context_unblock(ctxs[i]);
					lbm_context_process_events(ctxs[i], 1);
				}
			}
		} else if (opts->eventq) { /* embedded mode */
			/*
			 * Dispatch event queue indefinitely (only return upon error or when
			 * unblocked with lbm_event_dispatch_unblock() in one of our callbacks).
			 */
			if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
				fprintf(stderr, "lbm_event_dispatch returned error: %s\n", lbm_errmsg());
				break;
			}
		} else { /* embedded mode, no event queue */
			/*
			 * Just sleep for 1 second. LBM processing is
			 * done in its own thread.
			 */
			SLEEP_SEC(1);
		}
		/* Check if we should exit */
		if (close_recv) {
			break;
		}
	}

	if (opts->summary) {
		total_time = ((double)data_end_tv.tv_sec + (double)data_end_tv.tv_usec / 1000000.0)
					- ((double)data_start_tv.tv_sec + (double)data_start_tv.tv_usec / 1000000.0);
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
			printf ("Avg. throughput   : %-5.4g Kmsgs/sec, %-5.4g Mbps\n\n",
									total_mps/1000.0, total_bps/1000000.0);
		}

	}

	SLEEP_SEC(5);


	/* Clean up LBM objects */
	if (opts->monitor_receiver)
	{
		i=0;
		do {
			if (lbmmon_rcv_unmonitor(monctl, lbm_rcv_from_hfx_rcv(hfxrcvs[i++])) == -1)
			{
				fprintf(stderr, "lbmmon_rcv_unmonitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		} while(i<opts->num_ifaces);
	}
	else if (opts->monitor_context)
	{
		i=0;
		do {
			if (lbmmon_context_unmonitor(monctl, ctxs[i++]) == -1)
			{
				fprintf(stderr, "lbmmon_context_unmonitor() failed, %s\n", lbmmon_errmsg());
				exit(1);
			}
		}
		while(i<opts->num_ifaces);
		
	}
	if (monctl != NULL)
	{
		if (lbmmon_sctl_destroy(monctl) == -1)
		{
			fprintf(stderr, "lbmmon_sctl_destoy() failed(), %s\n", lbmmon_errmsg());
			exit(1);
		}
	}

        	
	if (timer_id != -1) {
		lbm_cancel_timer(ctxs[0], timer_id, NULL);
	}

	/* The shutdown order here is very important.
	   1) hfx_rcvs
	   2) event queue
	   3) contexts
	   4) hfx
	 */

	i=0;
	do {
		lbm_hfx_rcv_delete(hfxrcvs[i++]);
	} while(i<opts->num_ifaces);

	if (opts->eventq) {
		lbm_event_queue_delete(evq);
	}

	i=0;
	do {
		lbm_context_delete(ctxs[i++]);
	} while(i<opts->num_ifaces);

	lbm_hfx_delete(hfx);

	return 0;
}

