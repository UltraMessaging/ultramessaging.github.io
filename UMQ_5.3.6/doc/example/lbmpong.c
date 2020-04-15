/*
"lbmpong.c: application that measures message round trip time to
"  give a good measure of latency.

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
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_MSC_VER)
	/* Windows-only includes */
	#include <windows.h>
	#include <winsock2.h>
	typedef unsigned long socklen_t;
	#define ERRNO GetLastError()
	#define CLOSESOCKET closesocket
	#define TLONGLONG signed __int64
#else
	/* Unix-only includes */
	#define HAVE_PTHREAD_H
	#include <signal.h>
	#include <unistd.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <errno.h>
	#if defined(__TANDEM)
		#if defined(HAVE_TANDEM_SPT)
			#include <spthread.h>
		#else
			#include <pthread.h>
		#endif
	#else
		#include <pthread.h>
	#endif
	#include <sys/time.h>
	#define CLOSESOCKET close
	#define ERRNO errno
	#define SOCKET int
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR -1
	#define TLONGLONG signed long long
#endif

#include "lbm/lbm.h"

static const char *rcsid_example_lbmpong = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmpong.c#2 $";

#if defined(_WIN32)
#include <sys/timeb.h>
extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
#define WIN32_HIGHRES_TIME
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

#include "lbm-example-util.h"

char purpose[] = "Purpose: Message round trip processor.";
const char * OptionString = "Cc:hi:o:Il:M:P:qr:Rs:t:T:v";
char usage[] =
"Usage: [-ChIqRv] [-c filename] [-i msgs] [-l len] [-M msgs] [-P msec] [-r rate/pct] [-s seed] [-t secs] [-T topic] id\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -C = collect RTT data\n"
"       -h = help\n"
"       -i msgs = send and ignore msgs messages to warm up\n"
"       -o offset = use offset to calculate Registration ID\n"
"                   (as source registration ID + offset)\n"
"                   offset of 0 forces creation of regid by store\n"
"       -I = Use MIM\n"
"       -l len = use len length messages\n"
"       -M msgs = stop after receiving msgs messages\n"
"       -P msec = pause after each send msec milliseconds\n"
"       -q = use an LBM event queue\n"
"       -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                          DATA bits per second, and set retransmit rate limit to\n"
"                          RETR bits per second.  For both limits, the optional\n"
"                          k, m, and g suffixes may be used.  For example,\n"
"                          '-r 1m/500k' is the same as '-r 1000000/500000'\n"
"       -R = perform RTT measurement per message\n"
"       -s seed = init randomization of contents of message payload\n"
"       -t secs = run for secs seconds\n"
"       -T topic = topic name prefix (appended with '/' and id) [lbmpong]\n"
"       -v = be verbose about each message (for RTT only)\n"
"       id = either 'ping' or 'pong'\n"
;



#define TOPIC_SUFFIX_PING "ping"
#define TOPIC_SUFFIX_PONG "pong"
#define DEFAULT_TOPIC_PREFIX "lbmpong"

int msg_count = 0;
int msgs = 200;
int ping = 0;
int eventq = 0;
int rtt_measure = 0;
int rtt_collect = 0;
int rtt_ignore = -1;
int rtts = 0;
int rtt_min_idx = -1;
int rtt_max_idx = -1;
int seed = 0;
int verbose = 0;
size_t msglen = 20;
struct timeval starttv, endtv, msgstarttv, msgendtv;
lbm_context_t *ctx = NULL;
lbm_event_queue_t *evq = NULL;
lbm_src_t *src = NULL;
char *message;
double rtt_total = 0.0, rtt_min = 100.0, rtt_max = 0.0, rtt_median = 0.0, rtt_stddev = 0.0,rtt_avg = 0.0;
int run_secs = 300;
int msecpause = 0;
char *topic_prefix = DEFAULT_TOPIC_PREFIX;
char src_topic_name[250];
char rcv_topic_name[250];
double *rtt_data = NULL;
int use_mim = 0;
int regid_offset = -1;   /* Offset for calculating registration IDs */

/* callback for setting the RegID based on extended info */
lbm_uint_t ume_rcv_regid_ex(lbm_ume_rcv_regid_ex_func_info_t *info, void *clientd)
{
	lbm_uint_t regid = info->src_registration_id + regid_offset;

	if (verbose)
		printf("Store %u: %s [%s][%u] Flags %x. Requesting regid: %u (CD %p)\n", info->store_index, info->store, info->source,
			info->src_registration_id, info->flags, regid, info->source_clientd);
	return regid;
}

/*
 * For the elapsed time, calculate and print the latency per message (RTT),
 * then divide by 2
 */
void print_latency(FILE *fp, struct timeval *tv, size_t count)
{
	double sec = 0.0, rtt = 0.0, latency = 0.0;
	
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	rtt = (sec/(double)count) * 1000.0;
	latency = rtt / 2.0;
	fprintf(fp, "Elapsed time %.04g secs. %u messages (RTTs). %.04g msec RTT, %.04g msec latency\n", sec,
			count, rtt, latency);
	fflush(fp);
}

int print_stats()
{
	int nstats = 1;
	lbm_rcv_transport_stats_t rcvstats;
	lbm_src_transport_stats_t srcstats;			
			
	/* Retreive transport statistics */
	if (lbm_context_retrieve_rcv_transport_stats(ctx, &nstats, &rcvstats) == LBM_FAILURE) {
		fprintf(stderr, "Cannot retrieve all context stats (%s)\n", lbm_errmsg());
		fprintf(stderr, "lbm_context_retrieve_rcv_transport_stats: %s\n", lbm_errmsg());
		return -1;
	}
			
	/* Retreive source statistics */
	nstats = 1;
	if (lbm_context_retrieve_src_transport_stats(ctx, &nstats, &srcstats) == LBM_FAILURE) {
		fprintf(stderr, "Cannot retrieve all context stats (%s)\n", lbm_errmsg());
		fprintf(stderr, "lbm_context_retrieve_src_transport_stats: %s\n", lbm_errmsg());
		return -1;
	}
		
	switch (srcstats.type) {
		case LBM_TRANSPORT_STAT_TCP:
			break;
		case LBM_TRANSPORT_STAT_LBTRM:
			if (rcvstats.transport.lbtrm.lost != 0 || srcstats.transport.lbtrm.rxs_sent != 0) {
				fprintf(stdout, "The latency for this LBT-RM session of lbmpong might be skewed by loss\n");
				fprintf(stdout, "Source loss: %lu    Receiver loss: %lu\n", srcstats.transport.lbtrm.rxs_sent, 
																							   rcvstats.transport.lbtrm.lost);
			}
			fflush(stdout);
			break;
		case LBM_TRANSPORT_STAT_LBTRU:
			if (rcvstats.transport.lbtru.lost != 0 || srcstats.transport.lbtru.rxs_sent != 0) {
				fprintf(stdout, "The latency for this LBT-RU session of lbmpong might be skewed by loss\n");
				fprintf(stdout, "Source loss: %lu    Receiver loss: %lu\n", srcstats.transport.lbtru.rxs_sent, 
																							   rcvstats.transport.lbtru.lost);
			}
			fflush(stdout);
			break;
		case LBM_TRANSPORT_STAT_LBTIPC:
			break;
		case LBM_TRANSPORT_STAT_LBTRDMA:
			break;
		default:
			break;
	}		
	return 0;
}

/* Print RTT stats */
void print_rtt(FILE *file, struct timeval *tsp, struct timeval *etv)
{
	double sec = 0.0;
	
	etv->tv_sec -= tsp->tv_sec;
	etv->tv_usec -= tsp->tv_usec;
	normalize_tv(etv);
	sec = (double)etv->tv_sec + (double)etv->tv_usec / 1000000.0;
	if(rtt_ignore <= 0) {
		rtt_total += sec;
		if (sec < rtt_min) {
			rtt_min = sec;
			rtt_min_idx = rtts;
		}
		if (sec >= rtt_max) {
			rtt_max = sec;
			rtt_max_idx = rtts;
		}
		if(rtt_data)
			rtt_data[rtts] = sec;
		rtts++;
	}
	/* Only dump data if not collecting */
	if (verbose) {
		if( rtt_ignore > 0)
			fprintf(file, "RTT ignore (%d) %.04g msec\n",rtt_ignore,sec * 1000.0);
		else if( rtt_data == NULL )
			fprintf(file, "RTT %.04g msec\n",sec * 1000.0);
		fflush(file);
	}
}

/* Print RTT summary */
void print_rtt_results(FILE *file)
{
	if(rtt_data) {
		fprintf(file, "min/max msg = %d/%d median/stddev %.04g/%.04g msec\n",
			rtt_min_idx,rtt_max_idx,rtt_median,rtt_stddev);
	}
	fprintf(file, "%u RTT measurements. ",rtts);
	fprintf(file, "RTT min/avg/max = %.04g/%.04g/%.04g ms\n", rtt_min*1000.0, rtt_avg*1000.0, rtt_max*1000.0);
	if (rtt_max > 10) {	/* Reasonableness test  */
		fprintf(file, "Large RTT detected--perhaps you forgot the '-R' in 'lbmpong -R pong'?\n");
	}
	fflush(file);
}

double calc_med() {
	int r,changed;
	double t;

	/* sort the result set */
	do {
		changed = 0;
		
		for(r = 0;r < msgs - 1;r++) {
			if(rtt_data[r] > rtt_data[r + 1]) {
				t = rtt_data[r];
				rtt_data[r] = rtt_data[r + 1];
				rtt_data[r + 1] = t;
				changed = 1;
			}
		}
	} while(changed);

	if(msgs & 1) {
		/* Odd number of data elements - take middle */
		return rtt_data[(msgs / 2) + 1];
	} else {
		/* Even number of data element avg the two middle ones */
		return (rtt_data[(msgs / 2)] + rtt_data[(msgs / 2) + 1]) / 2;
	}
}

double calc_stddev(double mean) {
	int r;
	double sum;

	/* Subtract the mean from the data points, square them and sum them */
	sum = 0.0;
	for(r = 0;r < msgs;r++) {
		rtt_data[r] -= mean;
		rtt_data[r] *= rtt_data[r];
		sum += rtt_data[r];
	}

	sum /= (msgs - 1);

	return sqrt(sum);
}

void print_rtt_data(FILE *file)
{

	/* If data was collected during the run, dump all the data and the idx of the min and max */
	if(rtt_data)
	{
		int r;

		for(r = 0;r < msgs;r++)
			fprintf(file, "RTT %.04g msec, msg %d\n",rtt_data[r] * 1000.0,r);

		/* Calculate median and stddev */
		rtt_median = calc_med() * 1000.0;
		rtt_stddev = calc_stddev(rtt_avg) * 1000.0;

		print_rtt_results(file);
	}
}

/* Message handling callback (passed into lbm_rcv_create()) */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Data message received */
		if(ping && msg_count == 0 && rtt_ignore == 0) {
			current_tv(&starttv);
		}
		if(rtt_ignore <= 0) {
			msg_count++;
			if (ping && msg_count == msgs && !rtt_measure) {
				/*
			 	 * pinger:
			 	 *	received all the messages` now - print summary
			 	 *	and exit.
			 	 */
				current_tv(&endtv);
				endtv.tv_sec -= starttv.tv_sec;
				endtv.tv_usec -= starttv.tv_usec;
				normalize_tv(&endtv);
				print_latency(stdout, &endtv, msg_count);
				
				/* print transport stats */
				print_stats();
				
				exit(0);
				/* return 0;*/
			}
		}
		if (ping && rtt_measure) {
			/*
			 * pinger:
			 *	print RTT (and summary if all messages received)
			 */
			current_tv(&msgendtv);
			memcpy(&msgstarttv, msg->data, sizeof(msgstarttv));
			print_rtt(stdout, &msgstarttv, &msgendtv);
			if (rtt_ignore <= 0 && msg_count == msgs) {
				rtt_avg = (rtt_total/(double)rtts);

				/* Send data to stderr so it can be redirected separately */
				print_rtt_data(stderr);
				print_rtt_results(stdout);
				
				/* print transport stats */
				print_stats();
				
				exit(0);
				/* return 0; */
			}

			if (msecpause > 0) {
				SLEEP_MSEC(msecpause);
			}
			current_tv(&msgstarttv);
			memcpy(message, &(msgstarttv.tv_sec), sizeof(msgstarttv.tv_sec));
			memcpy(message + sizeof(msgstarttv.tv_sec), &(msgstarttv.tv_usec), sizeof(msgstarttv.tv_usec));
		} else if (rtt_measure) {
			/* ponger: return timestamp message */
			memcpy(message, msg->data, msglen);
		}
		/* send message back to peer */
		if (use_mim) {
			if (lbm_multicast_immediate_message(ctx, src_topic_name, message, msglen, LBM_MSG_FLUSH | LBM_SRC_NONBLOCK) == LBM_FAILURE) {
				fprintf(stderr, "lbm_multicast_immediate_message: %s\n", lbm_errmsg());
				exit(1);
			}
		} else {
			if (lbm_src_send(src, message, msglen, LBM_MSG_FLUSH | LBM_SRC_NONBLOCK) == LBM_FAILURE) {
				fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
		}
		if(rtt_ignore > 0) rtt_ignore--;
		break;
	case LBM_MSG_BOS:
		printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
		printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		printf("[%s][%s][%u], LOST\n", msg->topic_name, msg->source, msg->sequence_number);
		/* Any kind of loss makes this test invalid */	
		fprintf(stderr, "Unrecoverable loss occurred.  Quitting...\n");
		exit(1);
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		printf("[%s][%s][%u], LOST BURST\n", msg->topic_name, msg->source, msg->sequence_number);
		/* Any kind of loss makes this test invalid */	
		fprintf(stderr, "Unrecoverable loss occurred.  Quitting...\n");
		exit(1);	
		break;
	case LBM_MSG_UME_REGISTRATION_SUCCESS_EX:
	case LBM_MSG_UME_REGISTRATION_COMPLETE_EX:
	case LBM_MSG_UMQ_REGISTRATION_COMPLETE_EX:
		/* Provided to enable quiet usage of lbmstrm with UME */
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* Source event handler (passed into lbm_src_create()) */
int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
	const char *clientname = (const char *)ed;
	
	switch (event) {
	case LBM_SRC_EVENT_CONNECT:
		/*
		 * Indicates that a receiver has connected to the
		 * source (topic)
		 */
		printf("Receiver connect [%s]\n", clientname);
		break;
	case LBM_SRC_EVENT_DISCONNECT:
		/*
		 * Indicates that a receiver has disconnected from the
		 * source (topic)
		 */
		printf("Receiver disconnect [%s]\n", clientname);
		break;
	case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
	case LBM_SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
	case LBM_SRC_EVENT_UME_MESSAGE_STABLE_EX:
	case LBM_SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX:
		/* Provided to enable quiet usage of lbmpong with UME */
		break;
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

int main(int argc, char **argv)
{
	lbm_topic_t *rcv_topic;
	lbm_topic_t *src_topic;
	lbm_rcv_topic_attr_t * rcv_attr;
	lbm_rcv_t *rcv;
	int c, errflag = 0;
	lbm_context_attr_t * ctx_attr;
	lbm_src_topic_attr_t * tattr;
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	
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

	while ((c = getopt(argc, argv, OptionString)) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'C':
			rtt_collect++;
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'i':
			rtt_ignore = atoi(optarg);
			break;
		case 'o':
			regid_offset = atoi(optarg);
			break;
		case 'I':
			use_mim++;
			break;
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'M':
			msgs = atoi(optarg);
			break;
		case 'P':
			msecpause = atoi(optarg);
			break;
		case 'q':
			eventq++;
			break;
		case 'r':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 'R':
			rtt_measure++;
			break;
		case 's':
			seed = atoi(optarg);
			break;
		case 't':
			run_secs = atoi(optarg);
			break;
		case 'T':
			if (strlen(optarg) > (sizeof(src_topic_name) - 10)) {
				fprintf(stderr, "-T value longer than %d chars\n",
						sizeof(src_topic_name) - 10);
				errflag++;
			}
			topic_prefix = strdup(optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			errflag++;
			break;
		}
	}
	if (optind == argc) {
 		fprintf(stderr, "Missing id\n");
 		errflag++;
	}
	if (errflag) {
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}
	if (strcmp(argv[optind], "ping") == 0) {
		ping = 1;
		sprintf(src_topic_name, "%s/%s", topic_prefix, TOPIC_SUFFIX_PING);
		sprintf(rcv_topic_name, "%s/%s", topic_prefix, TOPIC_SUFFIX_PONG);
	} else if (strcmp(argv[optind], "pong") == 0) {
		ping = 0;
		sprintf(src_topic_name, "%s/%s", topic_prefix, TOPIC_SUFFIX_PONG);
		sprintf(rcv_topic_name, "%s/%s", topic_prefix, TOPIC_SUFFIX_PING);
	} else {
 		fprintf(stderr, "malformed id: '%s'\n", argv[optind]);
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}	
	if (!(msglen >= sizeof(struct timeval))) {
		fprintf(stderr, "Message length (%d) must be >= sizeof(struct timeval) (%d)\n", msglen, sizeof(struct timeval));
		exit(1);
	}
	if (use_mim && eventq == 0) {
		fprintf(stderr, "Using mim requires event queue to send from receive callback - forcing use\n");
		eventq++;
	}
	if (msecpause > 0 && eventq == 0) {
		fprintf(stderr, "Setting pause value requires event queue - forcing use\n");
		eventq++;
	}

	if(rtt_measure && rtt_collect) {
		/* Allocate buffer to store RTT data */
		rtt_data = malloc(msgs * sizeof(rtt_data[0]));
	}

	/* Retrieve current context settings */
	if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
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
 			if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RM retransmission rate attribute */
 			if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
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
 			if (lbm_context_attr_setopt(ctx_attr, "transport_lbtru_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtru_data_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			/* Set LBT-RU retransmission rate attribute */
 			if (lbm_context_attr_setopt(ctx_attr, "transport_lbtru_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
 				fprintf(stderr, "lbm_context_attr_setopt:transport_lbtru_retransmit_rate_limit: %s\n", lbm_errmsg());
 				exit(1);
 			}
			break;
		}
 	}

	/* Create LBM context (passing in context attributes) */
	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	if (eventq) {
		printf("Event queue in use\n");
		/* Create event queue if requested */
		if (lbm_event_queue_create(&evq, NULL, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
			exit(1);
		}
	} else {
		printf("No event queue\n");
	}

	/* init rcv topic attributes */
	if (lbm_rcv_topic_attr_create(&rcv_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if(regid_offset >= 0)
	{
		/* This is relevant for UME only, but enables this example to be used with persistent streams.
		 * There is no effect by doing this on non persistent streams or if an LBM only license is used
		 */
		lbm_ume_rcv_regid_ex_func_t id;

		id.func = ume_rcv_regid_ex;
		id.clientd = NULL;

		if (lbm_rcv_topic_attr_setopt(rcv_attr, "ume_registration_extended_function", &id, sizeof(id)) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_attr_setopt:ume_registration_extended_function: %s\n", lbm_errmsg());
			exit(1);
		}
		printf("Will use RegID offset %u.\n", regid_offset);
	}

	if (ping) {
		/* Pinger: */
		fprintf(stderr, "Sending %d %d byte messages, pausing %d msec between\n", msgs, msglen, msecpause);
		/* Look up ponger topic */
		if (lbm_rcv_topic_lookup(&rcv_topic, ctx, rcv_topic_name, rcv_attr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_lookup - pong: %s\n", lbm_errmsg());
			exit(1);
		}
		/* Allocate pinger source topic */
		if (use_mim == 0 && lbm_src_topic_alloc(&src_topic, ctx, src_topic_name, tattr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc - ping: %s\n", lbm_errmsg());
			exit(1);
		}
	} else {
		/* Ponger: */
		/* Look up pinger topic */
		if (lbm_rcv_topic_lookup(&rcv_topic, ctx, rcv_topic_name, rcv_attr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_lookup - ping: %s\n", lbm_errmsg());
			exit(1);
		}
		/* Allocate ponger source topic */
		if (use_mim == 0 && lbm_src_topic_alloc(&src_topic, ctx, src_topic_name, tattr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc - pong: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	lbm_src_topic_attr_delete(tattr);

	message = malloc(msglen);
	if (message == NULL) {
		fprintf(stderr, "can't allocate %d byte message buffer.\n", msglen);
		exit(1);
	}
	if (seed == 0)
		memset(message, 0, msglen);
	else {
		srand(seed);
		for (c = 0; c < (int)msglen; ++c) {
			/* According to `man 3 random`, "... the low dozen bits generated
			   by rand go through a cyclic pattern." */
			message[c] = (rand() >> 12) & 0xff;
		}
	}
	/* Create receiver using looked up topic (above) and message handler */
	if (lbm_rcv_create(&rcv, ctx, rcv_topic, rcv_handle_msg, NULL, evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Create source using topic allocated above */
	if (use_mim == 0 && lbm_src_create(&src, ctx, src_topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
		exit(1);
	}
	SLEEP_SEC(5);
	if (ping) {
		current_tv(&starttv);
		if (rtt_measure) {
			memcpy(&msgstarttv,&starttv,sizeof(msgstarttv));
			memcpy(message, &(msgstarttv.tv_sec), sizeof(msgstarttv.tv_sec));
			memcpy(message + sizeof(msgstarttv.tv_sec), &(msgstarttv.tv_usec), sizeof(msgstarttv.tv_usec));
		}
		/* pinger sends message with timestamp to start ping-pong */
		if (use_mim) {
			if (lbm_multicast_immediate_message(ctx, src_topic_name, message, msglen, LBM_MSG_FLUSH) == LBM_FAILURE) {
				fprintf(stderr, "lbm_multicast_immediate_message: %s\n", lbm_errmsg());
				exit(1);
			}
		} else {
			if (lbm_src_send(src, message, msglen, LBM_MSG_FLUSH) == LBM_FAILURE) {
				fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
		}
	}
	if (eventq) {
		/*
		 * If using event queue, start event queue dispatch (runs on
		 * current thread
		 */
		if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch returned error.\n");
		}
	} else {
		/* Otherwise, sleep for pre-defined period of time */
		SLEEP_SEC(run_secs);
	}
	
	printf("Quitting....\n");
	SLEEP_SEC(5);
	/*
	 * Deallocate source, receiver, LBM context, and event queue
	 *
	 * Event queue runs outside of LBM context, so it can be deallocated
	 * outside of LBM context.
	 */
	lbm_src_delete(src);
	lbm_rcv_delete(rcv);
	lbm_context_delete(ctx);
	if (eventq)
		lbm_event_queue_delete(evq);
	return 0;
}

