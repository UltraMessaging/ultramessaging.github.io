/*
"umetest.c: application that sends to a given topic (single
"  source) at a rate-limited pace, using blocking send. Understands UME.

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
	#if defined(__TANDEM)
		#include <sys/sem.h>
		#include <strings.h>
		#if defined(HAVE_TANDEM_SPT)
			#include <ktdmtyp.h>
			#include <spthread.h>
		#else
			#include <pthread.h>
		#endif
	#else
		#include <pthread.h>
		#if defined(__VMS)
			#include ppl$routines
		#else
			#include <semaphore.h>
		#endif
	#endif
#endif
#include "replgetopt.h"
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "umeblocksrc.h"
#include "monmodopts.h"
#include "verifymsg.h"
#include "lbm-example-util.h"

#define VERBOSE_PRINT(a, ...)  \
	do { \
		if(verbose) { \
			fprintf(stderr, a, ##__VA_ARGS__); \
		} \
	} while(0)


unsigned long appsent, stablerecv;

const char *OptionString = "c:l:M:vht:";
const struct option OptionTable[] =
{
	{ "config", required_argument, NULL, 'c' },
	{ "length", required_argument, NULL, 'l' },
	{ "message", required_argument, NULL, 'M' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

struct Options
{
	char confname[256];    /* Config file name */
	int amount;            /* Number of messages to send */
	int length;            /* The length of the msgs to send */
	int verbose;           /* Indicate wether to be verbose or not */
	char *topic;           /* Topic to register with */
};

const char Purpose[] = "Purpose: Send messages on a single topic using blocking send.";
const char Usage[]   = 
"Usage: %s [options] topic\n"
"Available options:\n"
"  -c, --config=FILE      use LBM configuration file FILE\n"
"  -l, --length=NUM       send messages of NUM bytes\n"
"  -M, --messages=NUM     send NUM messages\n"
"  -v, --verbose          print additional info in verbose form\n";

int verbose = 0;

void process_cmdline(int argc, char **argv, struct Options *opts)
{
	int errflag = 0;
	char c;

	/* Set default values */
	opts->confname[0] = '\0';
	opts->amount      = 200;
	opts->length      = 1000;
	opts->verbose     = 0;
	opts->topic       = NULL;

	while((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch(c)
		{
			case 'c':
				strncpy(opts->confname, optarg, sizeof(opts->confname));
				break;

			case 'M':
				opts->amount = atoi(optarg);
				break;

			case 'l':
				opts->length = atoi(optarg);
				break;

			case 'v':
				opts->verbose = 1;
				break;

			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);

			default:
				errflag++;
				break;
		}
	}

	if(errflag != 0 || optind == argc || opts->length <= 0 || opts->amount <= 0)
	{
		fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	opts->topic = argv[optind];
}

int ume_event_callback(lbm_src_t *src, int event, void *ed, void *cd)
{
	switch(event)
	{
		case LBM_SRC_EVENT_UME_REGISTRATION_ERROR:
 			VERBOSE_PRINT("Registration error: %s\n", (char*) ed);
			break;

		case LBM_SRC_EVENT_UME_STORE_UNRESPONSIVE:
 			VERBOSE_PRINT("Store unresponsive: %s\n", (char*) ed);
			break;

		case LBM_SRC_EVENT_UME_MESSAGE_STABLE:
 			VERBOSE_PRINT("STABLE%s\n", " ");
			stablerecv++;
			break;

		case LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX:
		{
			lbm_src_event_ume_ack_ex_info_t *info = (lbm_src_event_ume_ack_ex_info_t*) ed;

			if(verbose)
			{
				fprintf(stderr, "STABLE_EX: UME store %u: %s message stable. SQN %x (cd %p). Flags %x ", info->store_index, info->store, info->sequence_number, info->msg_clientd, info->flags);

				if (info->flags & LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE)
					fprintf(stderr, "IA ");

				if (info->flags & LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTERGROUP_STABLE)
					fprintf(stderr, "IR ");

				if (info->flags & LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE)
					fprintf(stderr, "STABLE ");

				if (info->flags & LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STORE)
					fprintf(stderr, "STORE ");

				fprintf(stderr, "\n");
			}

			appsent++;

			break;
		}

		case LBM_SRC_EVENT_SEQUENCE_NUMBER_INFO:
		{
			lbm_src_event_sequence_number_info_t *info = (lbm_src_event_sequence_number_info_t*) ed;
			VERBOSE_PRINT("Sequence number info. first: %d last: %d\n", info->first_sequence_number, info->last_sequence_number);

			break;
		}

		case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED:
 			VERBOSE_PRINT("Reclaimed%s\n", " ");

			break;

		case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
			VERBOSE_PRINT("Reclaimed%s\n", " ");

			break;

		default:
 			VERBOSE_PRINT("callback event: %d ed: %p cd: %p\n", event, ed, cd);
			break;
	}

	return 0;
}

/* Timer callback to handle periodic display of source statistics */
int handle_stats_timer(lbm_context_t *ctx, const void *clientd)
{

	return 0;
}

int lbm_log_msg(int level, const char *message, void *clientd)
{
	fprintf(stderr, "lbm_log_msg: LOG Level %d: %s\n", level, message);

	return 0;
}

void print_bw(FILE *fp, struct timeval *tv, size_t msgs, unsigned long long bytes)
{
	double sec = 0.0, mps = 0.0, bps = 0.0;
	double kscale = 1000.0, mscale = 1000000.0;
	char mgscale = 'K', bscale = 'K';

	if (tv->tv_sec == 0 && tv->tv_usec == 0) return;/* avoid div by 0 */
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	mps = (double)msgs/sec;
	bps = ((double)(bytes<<3))/sec;  /* Multiply by 8 and divide */
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
	case LBM_TRANSPORT_STAT_LBTIPC:
		fprintf(fp, "LBT-IPC, clients %lu, sent %lu/%lu, app sent %lu stable %lu inflight %lu\n",
				stats.transport.lbtipc.num_clients,
				stats.transport.lbtipc.msgs_sent, stats.transport.lbtipc.bytes_sent,
				appsent,stablerecv,stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
		break;
	case LBM_TRANSPORT_STAT_LBTRDMA:
		fprintf(fp, "LBT-RDMA, clients %lu, sent %lu/%lu, app sent %lu stable %lu inflight %lu\n",
				stats.transport.lbtrdma.num_clients,
				stats.transport.lbtrdma.msgs_sent, stats.transport.lbtrdma.bytes_sent,
				appsent,stablerecv,stablerecv > appsent ? stablerecv - appsent : appsent - stablerecv);
		break;
	default:
		break;
	}
	fflush(fp);
}


int main(int argc, char *argv[])
{
	unsigned long long bytes_sent = 0;
	struct Options opts;
	struct timeval starttv, endtv;
	char *message = NULL;
	double secs = 0.0;
	lbm_src_topic_attr_t *tattr;
	lbm_context_attr_t *cattr;
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	ume_block_src_t *src;
	lbm_src_send_ex_info_t exinfo;
	int i;

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


	/* Process the command line */
	process_cmdline(argc, argv, &opts);

	/* Setup logging */
	if(lbm_log(lbm_log_msg, NULL) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}

	/* LBM Config */
	if(opts.confname[0] != '\0')
	{
		if(lbm_config(opts.confname) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	/* Setup context attribute settings */
	if(lbm_context_attr_create(&cattr) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_context_attr_init: %s\n", lbm_errmsg());
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
		if(lbm_src_topic_attr_create_from_xml(&tattr, ctx_name, argv[optind]) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_src_topic_attr_create_from_xml: %s\n", lbm_errmsg());
			exit(1);
		}
	}	

	/* Create LBM context */
	if(lbm_context_create(&ctx, cattr, NULL, NULL) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(cattr);

	/* Allocate the desired topic */
	if(lbm_src_topic_alloc(&topic, ctx, argv[optind], tattr) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Create the auxilary source */
	if(ume_block_src_create(&src, ctx, topic, tattr, ume_event_callback, NULL, NULL) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
		exit(1);
	}

	message = (char*) malloc(opts.length);

	if(message == NULL)
	{
		fprintf(stderr, "Could not allocate message buffer of size %u bytes\n", opts.length);
		exit(1);
	}

	memset(message, 0, opts.length);

	current_tv(&starttv);
	printf("Sending %d messages on topic %s\n", opts.amount, argv[optind]);
	for(i=0; i<opts.amount; i++)
	{
		exinfo.flags = LBM_SRC_SEND_EX_FLAG_UME_CLIENTD;
		exinfo.ume_msg_clientd = (void*) (((lbm_uint_t) i) + 1);

		/* Send the message. */
		if(ume_block_src_send_ex(src, message, opts.length, 0, &exinfo) == LBM_FAILURE)
		{
			/* Gracefully handle the error. */
			fprintf(stderr, "Error: %d  %s\n", lbm_errnum(), lbm_errmsg());
			exit(1);
		}

		bytes_sent += (unsigned long long) opts.length;
		appsent++;
	}

	/* Print Stats */
	current_tv(&endtv);
	endtv.tv_sec  -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);

	secs = (double) endtv.tv_sec + (double) endtv.tv_usec / 1000000.0;
	printf("Sent %u messages of size %u bytes in %.04g seconds.\n", opts.amount, opts.length, secs);
	print_bw(stdout, &endtv, (size_t) i, bytes_sent);

	printf("Used %s for blocking\n", UME_BLOCKING_TYPE);
	printf("Lingering for 5 seconds.\n");
	SLEEP_SEC(5);

	/* Free the created items */
	lbm_src_topic_attr_delete(tattr);
	ume_block_src_delete(src);
	lbm_context_delete(ctx);

	free(message);

	return 0;
}

