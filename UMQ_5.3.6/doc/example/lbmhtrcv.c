/*
"lbmhtrcv.c: application that receives from a collection of HyperTopic patterns.

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
#include "lbm/lbm.h"
#include "lbm/lbmht.h"
#include "lbm-example-util.h"

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

char Usage[] =
"Usage: %s [options] <patterns\n"
"Available options:\n"
"  -d msec               delete hypertopic receiver every msec milliseconds\n"
"  -h, --help            display this help and exit\n"
"  -p string             set hypertopic prefix to string\n"
"  -q                    use event queue\n"
"  -s, --statistics      print statistics along with bandwidth\n"
"  -v, --verbose         be verbose about incoming messages\n"
"  -x                    exit after all receivers deleted\n"
;

#define DEFAULT_MAX_NUM_SRCS 10000
#define DEFAULT_NUM_SRCS 10

const char *OptionString = "d:hp:qsvx";
const struct option OptionTable[] = 
{
   { "help", no_argument, NULL, 'h' },
   { "verbose", no_argument, NULL, 'v' },
   { "statistics", no_argument, NULL, 's' },
   { NULL, 0, NULL, 0 }
};

int verbose = 0;
int exitflag = 0;
size_t nrmsgs = 0;
size_t nrbytes = 0;
size_t rx_msg_count = 0;
size_t otr_msg_count = 0;
size_t burst_loss = 0;
size_t unrec_count = 0;
lbm_event_queue_t *evq = NULL;
int deleteivl = 0;
char **patterns = NULL;
int pattern_size = 0;
int npat;
int ndel = 0;
lbm_hypertopic_rcv_t *hrcv;
lbm_context_t *ctx;
char *prefix = NULL;
int pstats = 0;
lbm_ulong_t lost = 0, last_lost = 0, lost_tmp = 0;
lbm_rcv_transport_stats_t *stats = NULL;
int nstats = DEFAULT_NUM_SRCS;
int nstat;
int have_stats, set_nstats;
		
int ht_rcv_handler(lbm_hypertopic_rcv_t *hrcv, lbm_msg_t *msg, void *clientd)
{
	char *pat;

	switch(msg->type)
	{
		case LBM_MSG_DATA:
			nrmsgs++;
			nrbytes += msg->len;

			if (msg->flags & LBM_MSG_FLAG_RETRANSMIT)
				rx_msg_count++;
			if (msg->flags & LBM_MSG_FLAG_OTR)
				otr_msg_count++;

			if (verbose > 1)
			{
				pat = (char *)clientd;
				printf("[%s][%s][%u]%s%s, %u bytes (%s)\n",msg->topic_name, msg->source, msg->sequence_number,
										((msg->flags & LBM_MSG_FLAG_RETRANSMIT) ? "-RX-" : ""),
										((msg->flags & LBM_MSG_FLAG_OTR) ? "-OTR-" : ""),
										msg->len, pat);
			}
			break;
		case LBM_MSG_UNRECOVERABLE_LOSS:
			unrec_count++;
			if (verbose > 1){
				pat = (char *)clientd;
				printf("[%s][%s][%u], LOST (%s)\n", msg->topic_name, msg->source, msg->sequence_number, pat);
			}
			break;
		case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
			burst_loss++;
			if (verbose > 1){
				pat = (char *)clientd;
				printf("[%s][%s][%u], LOST BURST (%s)\n", msg->topic_name, msg->source, msg->sequence_number, pat);
			}
			break;
		case LBM_MSG_BOS:
			pat = (char *)clientd;
			printf("[%s][%s], Beginning of Transport Session (%s)\n", msg->topic_name, msg->source, pat);
			break;
		case LBM_MSG_EOS:
			pat = (char *)clientd;
			printf("[%s][%s], End of Transport Session (%s)\n", msg->topic_name, msg->source, pat);
			break;
		default:
			printf("msg type = %d\n", msg->type);
			break;

	}
	return 0;
}


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

	if ((rx_msg_count != 0) || (otr_msg_count != 0))
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps [RX: %d][OTR: %d]",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index], rx_msg_count, otr_msg_count);
	else
		fprintf(fp, "%-5.4g secs.  %-5.4g %cmsgs/sec.  %-5.4g %cbps",
			sec, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);

	if (lost != 0 || unrec_count != 0 || burst_loss != 0){
		fprintf(fp, " [%u pkts lost, %u msgs unrecovered, %d bursts]",
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

void ht_del_complete(int dispatch_thrd, void *clientd)
{
	static int ncomplete;

	ncomplete++;
	if (verbose){
		fprintf(stdout, "%d. %s deletion complete (thread=%d)\n", ncomplete, (char *)clientd, dispatch_thrd);
		fflush(stdout);
	}
}
void load_hypertopics()
{
	int i;
	
	if (lbm_hypertopic_rcv_init(&hrcv, ctx, prefix, evq) == LBM_FAILURE)
	{
		fprintf(stdout, "lbm_hypertopic_rcv_init: %s\n", lbm_errmsg());
		exit(1);
	}
	for (i = 0; i < npat; i++)
	{
		if (lbm_hypertopic_rcv_add(hrcv, patterns[i], ht_rcv_handler, patterns[i]) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_hypertopic_rcv_add: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	fprintf(stdout, "%d patterns loaded\n", npat);
	fflush(stdout);
}

#if defined(_WIN32)
DWORD WINAPI deletion_thread_main(void *arg)
#else
void *deletion_thread_main(void *arg)
#endif
{
	lbm_delete_cb_info_t cbinfo;

	for (;;)
	{
		SLEEP_MSEC(deleteivl);
		if (ndel == npat)
		{
			if (lbm_hypertopic_rcv_destroy(hrcv) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_hypertopic_rcv_destroy: %s\n", lbm_errmsg());
				exit(1);
			}
			fprintf(stderr, "All hypertopics deleted\n");
			if (exitflag)
			{
				fprintf(stderr, "Lingering for 10 seconds...\n");
				SLEEP_SEC(10);
				lbm_context_delete(ctx);
				exit(0);
			}
			load_hypertopics();
			ndel = 0;
#ifdef NOTDEF
#if defined(_WIN32)
			/* The following line is only needed for static Windows library use */
			lbm_win32_static_thread_detach();
			ExitThread(0);
    return 0;
#else
			pthread_exit(NULL);
			return NULL;
#endif /* _WIN32 */
#endif
			continue;
		}
		cbinfo.clientd = patterns[ndel];
		cbinfo.cbproc = ht_del_complete;
		if (lbm_hypertopic_rcv_delete(hrcv, patterns[ndel], ht_rcv_handler, patterns[ndel], &cbinfo) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_hypertopic_rcv_delete(%s): %s\n", patterns[ndel], lbm_errmsg());
			exit(1);
		}
		ndel++;
	}
}

int main(int argc, char **argv)
{
		lbm_context_attr_t * ctx_attr;
		struct timeval starttv, endtv;
		char *pat;
		char line[256];
		size_t n;
		int c;
		int use_evq = 0;
		int errflag = 0;
		lbm_ipv4_address_mask_t unicast_target_iface;
		unsigned short int request_port;
		int request_port_bound;
		struct in_addr inaddr;

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
		pthread_t tid;
#endif /* _WIN32 */
		while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
		{
			switch (c)
			{
			case 'd':
				deleteivl = atoi(optarg);
				break;
			case 'x':
				exitflag = 1;
				break;
			case 'p':
				prefix = optarg;
				break;
			case 'q':
				use_evq = 1;
				break;
			case 's':
				pstats++;
				break;
			case 'v':
				verbose++;
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

		/* Allocate array for statistics */
		stats = (lbm_rcv_transport_stats_t *)malloc(nstats * sizeof(lbm_rcv_transport_stats_t));
		if (stats == NULL)
		{
			fprintf(stderr, "can't allocate statistics array\n");
			exit(1);
		}

		if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
			exit(1);
		}
		lbm_context_attr_delete(ctx_attr);
		n = sizeof(request_port_bound);
		if (lbm_context_getopt(ctx,
					"request_tcp_bind_request_port",
					&request_port_bound,
					&n) == LBM_FAILURE)
		{
			fprintf(stderr, "lbm_context_getopt(request_tcp_bind_request_port): %s\n",
					lbm_errmsg());
			exit(1);
		}
	      
		if (request_port_bound == 1) {
			
			n = sizeof(request_port);
			if (lbm_context_getopt(ctx,
						"request_tcp_port",
						&request_port,
						&n) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_context_getopt(request_tcp_port): %s\n",
						lbm_errmsg());
				exit(1);
			}
			n = sizeof(unicast_target_iface);
			if (lbm_context_getopt(ctx,
						"request_tcp_interface",
						&unicast_target_iface,
						&n) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_context_getopt(request_tcp_interface): %s\n",
						lbm_errmsg());
				exit(1);
			}
			/* if the request_tcp_interface is INADDR_ANY, get one we know is good. */
			if(unicast_target_iface.addr == INADDR_ANY) {
				if (lbm_context_getopt(ctx,
						       "resolver_multicast_interface",
						       &unicast_target_iface,
						       &n) == LBM_FAILURE) {
					fprintf(stderr, "lbm_context_getopt(resolver_multicast_interface): %s\n",
							lbm_errmsg());
					exit(1);
				}
			}
			inaddr.s_addr = unicast_target_iface.addr;
			printf("Immediate messaging target: TCP:%s:%d\n", inet_ntoa(inaddr), ntohs(request_port));
		}
		else { 
			printf("Request port binding disabled, no immediate messaging target.\n");
		}
		if (use_evq)
		{
			if (lbm_event_queue_create(&evq, NULL, NULL, NULL) == LBM_FAILURE)
			{
				fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
				exit(1);
			}
		}
		pattern_size = 128;
		patterns = (char **)malloc(sizeof(char *) * pattern_size);
		if (patterns == NULL)
		{
			fprintf(stderr, "can't allocate array");
			exit(1);
		}
		npat = 0;
		while (fgets(line, sizeof(line), stdin) != NULL)
		{
			n = strlen(line);
			pat = malloc(n);
			memcpy(pat, line, n);
			pat[n-1] = '\0';
			if (npat >= pattern_size)
			{
				pattern_size *= 2;
				patterns = (char **)realloc(patterns, sizeof(char *) * pattern_size);
				if (patterns == NULL)
				{
					fprintf(stderr, "can't reallocate array");
					exit(1);
				}
			}
			patterns[npat] = pat;
			npat++;
		}
		load_hypertopics();
		if (deleteivl != 0)
		{
#if defined(_WIN32)
			if (CreateThread(NULL, 0, deletion_thread_main, NULL, 0, NULL) == NULL)
			
#else
			if (pthread_create(&tid, NULL, deletion_thread_main, NULL) != 0)
#endif
			{
				fprintf(stderr, "could not create deletion thread\n");
				exit(1);
			}
		}

		for (;;)
		{
			current_tv(&starttv);
			if (use_evq)
			{
				 if (lbm_event_dispatch(evq, 1000) == LBM_FAILURE)
				 {
					fprintf(stderr, "lbm_event_dispatch returned error.\n");
					exit(2);
				}
			}
			else
				SLEEP_SEC(1);

			/* Retrieve receiver stats */
			have_stats = 0;
			while (!have_stats){
				set_nstats = nstats;
				if (lbm_context_retrieve_rcv_transport_stats(ctx, &set_nstats, stats) == LBM_FAILURE){
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

			/* Get transport level loss */
			lost = 0;
			for (nstat = 0; nstat < set_nstats; nstat++)
			{
				switch (stats[nstat].type) {
				case LBM_TRANSPORT_STAT_LBTRM:
					lost += stats[nstat].transport.lbtrm.lost;
					break;
				case LBM_TRANSPORT_STAT_LBTRU:
					lost += stats[nstat].transport.lbtru.lost;
					break;
				}
			}
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
			endtv.tv_usec -= endtv.tv_usec;
			normalize_tv(&endtv);
			
			print_bw(stdout, &endtv, nrmsgs, nrbytes, lost);

			if (pstats){
				/* Display transport level statistics */
				for (nstat = 0; nstat < set_nstats; nstat++){
					fprintf(stdout, "stats %u/%u:", nstat+1, set_nstats);
					print_stats(stdout, stats[nstat]);
				}
			}
			
			nrmsgs = 0;
			rx_msg_count = 0;
			otr_msg_count = 0;
			nrbytes = 0;
			unrec_count = 0;
			burst_loss = 0;
		}
}
