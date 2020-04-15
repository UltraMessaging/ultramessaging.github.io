/*
"lbmhfrcv.c: application that receives messages from a given topic
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
#else
        #include <unistd.h>
        #include <netinet/in.h>
        #include <arpa/inet.h>
	#include <signal.h>
        #include <sys/time.h>
#endif
#include <lbm/lbm.h>
#include "verifymsg.h"
#include "lbm-example-util.h"

static const char *rcsid_example_lbmhfrcv = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmhfrcv.c#2 $";

#if defined(_WIN32)
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

char purpose[] = "Purpose: Receive messages on a single topic.";
char usage[] =
"Usage: [-AEhsvV] [-c filename] [-r msgs] [-U losslev] topic\n"
"       -A = display messages as ASCII text\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -E = exit after source ends\n"
"       -h = help\n"
"       -r msgs = delete receiver after msgs messages\n"
"       -s = print statistics along with bandwidth\n"
"       -S = Exit after source ends, print throughput summary\n"
"       -v = be verbose about incoming messages (-v -v = be even more verbose)\n"
"       -V = verify message contents\n"
;


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
int pstats = 0;
int total_unrec_count = 0;
int burst_loss = 0;
int verbose = 0;
int opmode_seq = 0;
int reap_msgs = 0;
int end_on_end = 0;
int ascii = 0;
int close_recv = 0;
int summary = 0;

struct timeval data_start_tv;
struct timeval data_end_tv;

int verify_msgs = 0;
lbm_uint_t expected_sqn = 0;
lbm_ulong_t lost = 0, last_lost = 0;
lbm_rcv_transport_stats_t *stats = NULL;
int nstats = DEFAULT_NUM_SRCS;

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
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rx_msgs, otr_msgs);
	else
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);

	if (lost != 0 || unrec != 0 || burst_loss != 0) {
		fprintf(fp, " [%lu pkts lost, %u msgs unrecovered, %d bursts]",
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
		fprintf(fp, " [%s], received %lu, LBM %lu/%lu/%lu\n",stats.source,stats.transport.tcp.bytes_rcved,
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

/*
 * Handler for immediate messages directed to NULL topic
 * (passed into lbm_context_rcv_immediate_msgs()
 */
int rcv_handle_immediate_msg(lbm_context_t *ctx, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Data message received */
		msg_count++;
		total_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		if (ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (ascii > 1) putchar('\n');
			fflush(stdout);
		}
		if (verbose) {
			printf("IM [%s][%u], %u bytes\n", msg->source,
				   msg->sequence_number, msg->len);
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_REQUEST:
		/* Request message received (no response processed here) */
		msg_count++;
		total_msg_count++;
		subtotal_msg_count++;
		byte_count += msg->len;
		if (ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (ascii > 1) putchar('\n');
			fflush(stdout);
		}
		if (verbose) {
			printf("IM Request [%s][%u], %u bytes\n", msg->source,
				   msg->sequence_number, msg->len);
			if (verbose > 1)
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

/* Received message handler (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	lbm_uint64_t sqn = 0; /* large enough to hold a regular sequence number or any HF sequence number */
	if (msg->flags & LBM_MSG_FLAG_HF_32) {
		sqn = (lbm_uint64_t)msg->hf_sequence_number.u32;
	}
	else if (msg->flags & LBM_MSG_FLAG_HF_64) {
		sqn = msg->hf_sequence_number.u64;
	}
	else {
		/* message does not contain a HF sequence number */
		sqn = (lbm_uint64_t)msg->sequence_number;
	}

	switch (msg->type) {
	case LBM_MSG_DATA:
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

		if (ascii) {
			int n = msg->len;
			const char *p = msg->data;
			while (n--)
			{
				putchar(*p++);
			}
			if (ascii > 1) putchar('\n');
			fflush(stdout);
		}
		if (verbose)
		{
			fprintf(stdout, "[%s][%s][%"PRIu64"]%s%s%s%s%s%s%s, %u bytes\n", msg->topic_name, msg->source, sqn,
				   ((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX" : ""),
				   ((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR" : ""),
				   ((msg->flags & LBM_MSG_FLAG_HF_64) ? "-HF64" : ""),
				   ((msg->flags & LBM_MSG_FLAG_HF_32) ? "-HF32" : ""),
				   ((msg->flags & LBM_MSG_FLAG_HF_DUPLICATE) ? "-HFDUP" : ""),
				   ((msg->flags & LBM_MSG_FLAG_HF_PASS_THROUGH) ? "-PASS" : ""),
				   ((msg->flags & LBM_MSG_FLAG_HF_OPTIONAL) ? "-HFOPT" : ""),
				   msg->len);
			if (verbose > 1)
				dump(msg->data, msg->len);
			
			fflush(stdout);
		}
		if (verify_msgs)
		{
			int rc = verify_msg(msg->data, msg->len, verbose);
			if (rc == 0)
			{
				printf("Message sqn %"PRIu64" does not verify!\n", sqn);
			}
			else if (rc == -1)
			{
				fprintf(stderr, "Message sqn %"PRIu64" is not a verifiable message.\n", sqn);
				fprintf(stderr, "Use -V option on source and restart receiver.\n");
				exit(1);
			}
			else
			{
				if (verbose)
				{
					printf("Message sqn %"PRIu64" verifies\n", sqn);
				}
			}
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		unrec_count++;
		total_unrec_count++;
		if (verbose) {
			printf("[%s][%s][%"PRIu64"], LOST\n", msg->topic_name, msg->source, sqn);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		burst_loss++;
		if (verbose) {
			printf("[%s][%s][%"PRIu64"], LOST BURST\n", msg->topic_name, msg->source, sqn);
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
		   the verification to fail as we don't track the numbers on a per source basis.
		*/
		if (end_on_end)
			close_recv = 1;
		break;
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		printf("[%s], no sources found for topic\n", msg->topic_name);
		break;
	case LBM_MSG_HF_RESET:
		if (verbose) {
			fprintf(stdout, "[%s][%s][%"PRIu64"]%s%s%s%s-RESET\n", msg->topic_name, msg->source, sqn,
					((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX" : ""),
					((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR" : ""),
					((msg->flags & LBM_MSG_FLAG_HF_64) ? "-HF64" : ""),
					((msg->flags & LBM_MSG_FLAG_HF_32) ? "-HF32" : ""));
			fflush(stdout);
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
#endif

int main(int argc, char **argv)
{
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_hf_rcv_t *rcv;
	int c, errflag = 0;
	lbm_context_attr_t * ctx_attr;
	unsigned short int request_port;
	int request_port_bound;
	size_t optlen;
	lbm_ipv4_address_mask_t unicast_target_iface;
	struct in_addr inaddr;

	double total_time = 0.0;
	double total_mps = 0.0;
	double total_bps = 0.0;

	int have_stats = 0, set_nstats;

	/* Allocate array for statistics */
	stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
	if (stats == NULL)
	{
		fprintf(stderr, "can't allocate statistics array\n");
		exit(1);
	}

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

	while ((c = getopt(argc, argv, "Ac:Ehr:sSvV")) != EOF) {
		switch (c) {
		case 'A':
			ascii++;
			break;
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'E':
			end_on_end++;
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'r':
			reap_msgs = atoi(optarg);
			summary++;
			break;
		case 's':
			pstats++;
			break;
		case 'S':
			summary++;
			end_on_end++;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			verify_msgs++;
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
	if (opmode_seq) {
		/*
                 * Set operational mode to "sequential" meaning that a separate
		 * thread will not be used to handle LBM processing. All LBM
		 * processng will be done on this thread (see below).
		 */
		if (lbm_context_attr_str_setopt(ctx_attr, "operational_mode", "sequential") == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_str_setopt - sequential: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/* Create LBM context */
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
	if(lbm_context_getopt(ctx,
			      "request_tcp_bind_request_port",
			      &request_port_bound,
			      &optlen) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_getopt(request_tcp_bind_request_port): %s\n",
			      lbm_errmsg());
		exit(1);
	}
	if(request_port_bound == 1) {
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
	}
	else {
		printf("Request port binding disabled, no immediate messaging target.\n");
	}

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/* Initialize immediate message handler (for topicless immediate sends */
	if (lbm_context_rcv_immediate_msgs(ctx, rcv_handle_immediate_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_rcv_immediate_msgs: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Lookup desired topic */
	if (lbm_rcv_topic_lookup(&topic, ctx, argv[optind], NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}
	/*
	 * Create HF receiver passing in the looked up topic info and the message
	 * handler callback.
	 */
	if (lbm_hf_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_hf_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}

	while (1) {
		struct timeval starttv, endtv;

		current_tv(&starttv);
		/*
		 * Just sleep for 1 second. LBM processing is
		 * done in its own thread.
		 */
		SLEEP_SEC(1);
		current_tv(&endtv);
		endtv.tv_sec -= starttv.tv_sec;
		endtv.tv_usec -= starttv.tv_usec;
		normalize_tv(&endtv);

		if (stotal_msg_count > 0) {
			int count = 0;
			lbm_ulong_t lost_tmp;
			have_stats = 0;
			while (!have_stats){
				set_nstats = nstats;
				if (lbm_rcv_retrieve_all_transport_stats(lbm_rcv_from_hf_rcv(rcv), &set_nstats, stats) == LBM_FAILURE){
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

			lost = 0;
			for (count = 0; count < set_nstats; count++)
			{
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
		}
		if (!ascii)
			print_bw(stdout, &endtv, msg_count, byte_count, unrec_count, lost, rx_msg_count, otr_msg_count);
 		if (pstats && set_nstats > 0 && stotal_msg_count > 0) {
			int count = 0;

			for (count = 0; count < set_nstats; count++) {
				fprintf(stdout, "stats %u/%u:", count+1, set_nstats);
				print_stats(stdout, stats[count]);
			}
		}

		if ((reap_msgs > 0 && total_msg_count >= reap_msgs) || close_recv) {
			/*
			 * Get rid of receiver if we've received all we we
			 * wanted or if the sender has already gone away.
			 */
			lbm_hf_rcv_delete(rcv);
			rcv = NULL;
		}
		msg_count = 0;
		rx_msg_count = 0;
		otr_msg_count = 0;
		byte_count = 0;
		unrec_count = 0;
		if (rcv == NULL)
			break;
	}

	if (summary) {
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
		printf("Quitting.... received %u messages\n", total_msg_count);

	SLEEP_SEC(5);

	/* Delete LBM context (not strictly necessary in this example) */
	lbm_context_delete(ctx);
	return 0;
}

