/*
"lbmhfsrc.c: application that sends to a given topic (single
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
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/time.h>
	#include <signal.h>
#endif
#include <lbm/lbm.h>
#include "verifymsg.h"
#include "lbm-example-util.h"

static const char *rcsid_example_lbmhfsrc = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmhfsrc.c#2 $";

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_MAX_MESSAGES 10000000
#define DEFAULT_DELAY_B4CLOSE 5

#if defined(_WIN32)
/* Windows has _strtoui64 instead of posix strtoull */
#define strtoull _strtoui64

extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
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

char purpose[] = "Purpose: Send messages on a single topic.";
char usage[] =
"Usage: lbmhfsrc [options] topic\n"
"Available options:\n"
"  -c filename = Use LBM configuration file filename.\n"  
"                Multiple config files are allowed.\n"
"                Example:  '-c file1.cfg -c file2.cfg'\n"
"  -d delay = delay sending for delay seconds after source creation\n"
"  -h = help\n"
"  -i init = start at message init instead of 0\n"
"  -l len = send messages of len bytes\n"
"  -L linger = linger for linger seconds before closing context\n"
"  -M msgs = send msgs number of messages\n"
"  -N NUM = send on channel NUM\n"
"  -P msec = pause after each send msec milliseconds\n"
"  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                     DATA bits per second, and set retransmit rate limit to\n"
"                     RETR bits per second.  For both limits, the optional\n"
"                     k, m, and g suffixes may be used.  For example,\n"
"                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
"  -s sec = print stats every sec seconds\n"
"  -t filename = use filename contents as a recording of message sequence numbers\n"
"  -V = construct verifiable messages\n"
"  -x bits = Use 32 or 64 bits for hot-failover sequence numbers\n"
;

/* For the elapsed time, calculate and print the msgs/sec and bits/sec */
void print_bw(FILE *fp, struct timeval *tv, unsigned int msgs, unsigned int bytes)
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

/* Logging callback */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

int stats_timer_id = -1;
int done_sending = 0;
int verifiable_msgs = 0;
lbm_ulong_t stats_sec = 0;

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
	if (!done_sending) {
		if ((stats_timer_id = lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, (stats_sec * 1000))) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	double secs = 0.0;
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src;
	lbm_src_topic_attr_t * tattr;
	lbm_context_attr_t * cattr;
	struct timeval starttv, endtv;
	int bytes_sent = 0;
	lbm_uint64_t init_count = 0 , count = 0 ;
	unsigned long msgs = DEFAULT_MAX_MESSAGES;
	int linger = DEFAULT_DELAY_B4CLOSE;
	char *message = NULL;
	int c, errflag = 0, pause = 0, delay = 1, bits = 32;
	size_t msglen = MIN_ALLOC_MSGLEN;
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	FILE *tapeptr = NULL;
	unsigned long totmsgs = 0;

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

	while ((c = getopt(argc, argv, "c:d:hi:L:l:M:P:R:s:t:Vx:")) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'i':
			init_count = strtoul(optarg, NULL, 0);
			break;
		case 'L':
			linger = atoi(optarg);
			break;
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'M':
			msgs = strtoul(optarg, NULL, 0);
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'P':
			pause = atoi(optarg);
			break;
		case 'R':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 's':
			stats_sec = atoi(optarg);
			break;
		case 't':
			if (strcmp(optarg, "-") == 0) {
				tapeptr = stdin;
			} else {
				if ((tapeptr = fopen(optarg, "r")) == NULL) {
					fprintf(stderr, "could not open tape file <%s>\n", optarg);
					exit(1);
				}
			}
			break;
		case 'V':
			verifiable_msgs = 1;
			break;
		case 'x':
			bits = atoi(optarg);
			if (bits != 32 && bits != 64) {
				fprintf(stderr, "-x %d invalid, must be 32 or 64\n", optarg);
				exit(1);
			}
			break;
		default:
			errflag++;
			break;
		}
	}
	if (errflag || (optind == argc)) {
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}
	if (verifiable_msgs != 0)
	{
		size_t min_msglen = minimum_verifiable_msglen();
		if (msglen < min_msglen)
		{
			printf("Specified message length %u is too small for verifiable messages.\n", msglen);
			printf("Setting message length to minimum (%u).\n", min_msglen);
			msglen = min_msglen;
		}
	}
	/* Setup logging callback */
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}
	
	/* if message buffer is too small, then the sprintf will cause issues. So, allocate with a min size */
	if (msglen < MIN_ALLOC_MSGLEN) {
		message = malloc(MIN_ALLOC_MSGLEN);
	} else {
		message = malloc(msglen);
	}
	
	if (message == NULL) {
		fprintf(stderr, "could not allocate message buffer of size %u bytes\n", msglen);
		exit(1);
	}
	
	memset(message, 0, msglen);
	
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
 	if (rm_rate != 0) {
 		printf("Sending with LBT-R%c data rate limit %" PRIu64 ", retransmission rate limit %" PRIu64 "\n", 
			rm_protocol,rm_rate, rm_retrans);
		/* Set transport attribute to LBT-RM */
		switch(rm_protocol) {
		case 'M':
 			if (lbm_src_topic_attr_str_setopt(tattr, "transport", "LBTRM") != 0) {
 				fprintf(stderr, "lbm_src_topic_str_setopt:transport: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RM data rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtrm_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RM retransmission rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtrm_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
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
 			if (lbm_context_attr_setopt(cattr, "transport_lbtru_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtru_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RU retransmission rate attribute */
 			if (lbm_context_attr_setopt(cattr, "transport_lbtru_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
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

	/* Allocate the desired topic */
	if (lbm_src_topic_alloc(&topic, ctx, argv[optind], tattr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_src_topic_attr_delete(tattr);

	/*
	 * Create LBM HF source passing in the allocated topic and event
	 * handler. The source object is returned here in &src.
	 */
	if (lbm_hf_src_create(&src, ctx, topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_hf_src_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if (stats_sec > 0) {
		/* Schedule time to handle statistics display. */
		if ((stats_timer_id = lbm_schedule_timer(ctx, handle_stats_timer, src, NULL, (stats_sec * 1000))) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	if (delay > 0) {
		printf("Will start sending in %d second%s...\n", delay, ((delay > 1) ? "s" : ""));
		SLEEP_SEC(delay);
	}
	printf("Sending %u messages of size %u bytes to topic [%s]\n",
		   msgs, msglen ,argv[optind]);
	current_tv(&starttv);
	if (tapeptr != NULL) {
		char line[256], hf_sqn_str[256], hf_sqn_opt[256];
		lbm_hf_sequence_number_t hf_sqn;
		lbm_src_send_ex_info_t exinfo, exinfo_opt, *exinfo_ptr = NULL;
		int send_reset = 0;

		memset(&exinfo, 0, sizeof(exinfo));
		memset(&exinfo_opt, 0, sizeof(exinfo_opt));

		/* set up exinfo objects used to send HF messages */
		if (bits == 32) {
			exinfo.flags |= LBM_SRC_SEND_EX_FLAG_HF_32;
			exinfo_opt.flags |= LBM_SRC_SEND_EX_FLAG_HF_32;
		}
		else {
			exinfo.flags |= LBM_SRC_SEND_EX_FLAG_HF_64;
			exinfo_opt.flags |= LBM_SRC_SEND_EX_FLAG_HF_64;
		}
		exinfo_opt.flags |= LBM_SRC_SEND_EX_FLAG_HF_OPTIONAL;

		while (1) {
			if (fgets(line, sizeof(line), tapeptr) == NULL) {
				if (feof(tapeptr)) {
					break;
				} else {
					fclose(tapeptr);
					fprintf(stderr, "error reading tape file\n");
					exit(1);
				}
			}
			exinfo_ptr = NULL;
			send_reset = 0;

			/* parse line "#o|r" */
			switch (sscanf(line, "%[0-9]%[o|r]", hf_sqn_str, hf_sqn_opt)) {
			case 1:
				exinfo_ptr = &exinfo;
				break;
			case 2:
				/* optional or reset */
				if (memcmp(hf_sqn_opt, "o", 1) == 0) {
					exinfo_ptr = &exinfo_opt;
				}
				else {
					exinfo_ptr = &exinfo;
					send_reset = 1;
				}
				break;
			default:
				fprintf(stderr, "error parsing tape file line [%s]\n", line);
				exit(1);
				break;
			}
			
			if (bits == 32) {
				hf_sqn.u32 = strtoul(hf_sqn_str, NULL, 10);
			}
			else {
				hf_sqn.u64 = strtoull(hf_sqn_str, NULL, 10);
			}
			exinfo_ptr->hf_sqn = hf_sqn;

			if (send_reset) {
				/* send a HF receiver reset message */
				if (lbm_hf_src_send_rcv_reset(src, LBM_MSG_FLUSH, exinfo_ptr) == LBM_FAILURE) {
					fprintf(stderr, "lbm_hf_src_send_rcv_reset: %s\n", lbm_errmsg());
					exit(1);
				}
			}
			else {
				if (verifiable_msgs) {
					construct_verifiable_msg(message, msglen);
				} else if (bits == 32) {
					sprintf(message, "message %d", hf_sqn.u32);
				}
				else {
					sprintf(message, "message %" PRIu64, hf_sqn.u64);
				}
			
				/* Send hf message using allocated source */
				if (lbm_hf_src_send_ex(src, message, msglen, 0, 0, exinfo_ptr) == LBM_FAILURE) {
					fprintf(stderr, "lbm_hf_src_send: %s\n", lbm_errmsg());
					exit(1);
				}
				bytes_sent += msglen;
			}
			count++;
			if (pause > 0) {
				SLEEP_MSEC(pause);
			}
		}
		fclose(tapeptr);
	} else {
		lbm_src_send_ex_info_t exinfo;
		memset(&exinfo, 0, sizeof(exinfo));
		
		/* flag the exinfo as 64 or 32 bit */
		exinfo.flags |= (bits == 64 ? LBM_SRC_SEND_EX_FLAG_HF_64 : LBM_SRC_SEND_EX_FLAG_HF_32);
		for (count = init_count, totmsgs = 0; totmsgs < msgs; count++, totmsgs++) {
			memset(message, 0, msglen);
			if (verifiable_msgs) {
				construct_verifiable_msg(message, msglen);
			} else {
				sprintf(message, "message %"PRIu64, count);
			}
			
			/* set the HF sequence number */
			if (bits == 32) {
				exinfo.hf_sqn.u32 = (lbm_uint_t)count;
			}
			else {
				exinfo.hf_sqn.u64 = count;
			}

			/* Send hf message using allocated source */
			if (lbm_hf_src_send_ex(src, message, msglen, 0, 0, &exinfo) == LBM_FAILURE) {
				fprintf(stderr, "lbm_hf_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
			bytes_sent += msglen;
			if (pause > 0) {
				SLEEP_MSEC(pause);
			}
		}
	}
	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);
	secs = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
	printf("Sent %u messages of size %u bytes in %.04g seconds.\n",
			count, msglen, secs);
	print_bw(stdout, &endtv, count, bytes_sent);
	/*
	 * Sleep for a bit so that batching gets out all the queued messages,
	 * if any.  If we just exit, then some messages may not have been sent by
	 * TCP yet.
	 */
	done_sending = 1;
	print_stats(stdout, src);
	if (linger > 0) {
		printf("Lingering for %d seconds...\n", linger);
		SLEEP_SEC(linger);
	}
	/* Deallocate source and LBM context */
	lbm_src_delete(src);
	src = NULL;
	SLEEP_SEC(5);
	lbm_context_delete(ctx);
	ctx = NULL;
	SLEEP_SEC(5);
	free(message);
	return 0;
}

