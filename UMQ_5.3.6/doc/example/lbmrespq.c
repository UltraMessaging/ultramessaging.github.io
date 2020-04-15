/*
"lbmrespq.c: application that waits for requests and sends responses back
"  on a given topic (single receiver), using an event queue.

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
#include "lbm-example-util.h"

static const char *rcsid_example_lbmrespq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmrespq.c#2 $";

#if defined(_WIN32)
extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
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

char purpose[] = "Purpose: Respond to request messages on a single topic using an event queue.";
char usage[] =
"Usage: [-hs] [-c filename] [-r msgs] topic\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -h = help\n"
"       -P msecs = pause msecs milliseconds before sending response\n"
"       -r msgs = delete receiver after msgs request messages\n"
"       -s = be silent about requests/sec rate\n"
"       -v = be verbose (-v -v = be even more verbose)\n"
;

int request_count = 0, requests_in_ivl = 0;
int verbose = 0, silent_rate = 0;
lbm_ulong_t response_interval = 0;
lbm_event_queue_t *evq = NULL;
lbm_rcv_t *rcv = NULL;
lbm_context_t *ctx = NULL;
struct timeval starttv, endtv;
int reap_reqs = 0;
int unrec_count = 0;
int burst_loss = 0;


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

/* For the elapsed time, calculate and print the msgs/sec and bits/sec */
void print_speed(FILE *fp, struct timeval *tv, int reqs)
{
	double sec = 0.0, reqps = 0.0;
	double kscale = 1000.0, mscale = 1000000.0;
	char reqsscale = 'K';
	
	sec = (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
	reqps = (double)reqs/sec;
	if (reqps <= mscale) {
		reqsscale = 'K';
		reqps /= kscale;
	} else {
		reqsscale = 'M';
		reqps /= mscale;
	}
	if (unrec_count > 0 || burst_loss > 0)
		fprintf(fp, "%.04g secs. %.04g%c requests/sec. [%d unrecoverable, %d bursts]\n",
			sec, reqps, reqsscale, unrec_count, burst_loss);
	else
		fprintf(fp, "%.04g secs. %.04g%c requests/sec.\n",
			sec, reqps, reqsscale);
	fflush(fp);
	burst_loss = 0;
	unrec_count = 0;
}

/* Event queue monitor callback (passed into lbm_event_queue_create()) */
int evq_monitor(lbm_event_queue_t *evq, int event, size_t evq_size,
				lbm_ulong_t event_delay_usec, void *clientd)
{
	printf("event queue threshold exceeded - event %x, sz %u, delay %lu\n",
		   event, evq_size, event_delay_usec);
	return 0;
}

/* Logging callback (passed into lbm_log()) */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

/* Timer callback that handles sending delayed reponses */
int rcv_handle_resp_tmo(lbm_context_t *ctx, const void *clientd)
{
	lbm_msg_t *resp_msg = (lbm_msg_t *)clientd;
	lbm_response_t *response = resp_msg->response;
	char response_buffer[256] = "Response message is here!";
	
	if (response != NULL) {
		if (verbose)
			printf("Sending response\n");
		
		if (lbm_send_response(response, response_buffer, strlen(response_buffer), 0) == LBM_FAILURE) {
			fprintf(stderr, "lbm_send_response: %s\n", lbm_errmsg());
			exit(1);
		}
		/*
		 * Delete the request retained in the received message
		 * callback.
		 */
		lbm_msg_delete(resp_msg);
	}
	return 0;
}

/*
 * Timer callback that prints bandwidth utilization info on a 1 second interval
 */
int rcv_handle_tmo(lbm_context_t *ctx, const void *clientd)
{
	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);
	print_speed(stdout, &endtv, requests_in_ivl);
	requests_in_ivl = 0;

	current_tv(&starttv);
	/* Reschedule timer */
	if (lbm_schedule_timer(ctx, rcv_handle_tmo, NULL, evq, 1000) == -1) {
		fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
		exit(1);
	}
	return 0;
}

/* Received message handler callback */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	int close_recv = 0;

	switch (msg->type) {
	case LBM_MSG_DATA:
		if (verbose) {
			printf("[%s][%s][%u], %u bytes\n",
				   msg->topic_name, msg->source, msg->sequence_number, msg->len);
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_BOS:
		printf("[%s][%s], Beginning of Transport Session\n", msg->topic_name, msg->source);
		break;
	case LBM_MSG_EOS:
		printf("[%s][%s], End of Transport Session\n", msg->topic_name, msg->source);
		close_recv = 1;
		break;
	case LBM_MSG_REQUEST:
		request_count++;
		requests_in_ivl++;
		if (verbose) {
			printf("Request [%s][%s][%u], %u bytes\n",
				   msg->topic_name, msg->source, msg->sequence_number,
				   msg->len);
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		if (response_interval > 0) {
			/* Schedule a timer to handle the response later. */
			if (lbm_schedule_timer(ctx, rcv_handle_resp_tmo, msg, evq, response_interval) == -1) {
				fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
				exit(1);
			}
			/*
			 * Retain the lbm_msg_t object to keep the response
			 * object around.
			 */
			lbm_msg_retain(msg);
		} else {
			lbm_response_t *response = msg->response;
			char response_buffer[256] = "Response message is here!";
			
			if (verbose)
				printf("Sending response\n");
			
			/* Send response now */
			if (lbm_send_response(response, response_buffer, strlen(response_buffer), 0) == LBM_FAILURE) {
				fprintf(stderr, "lbm_send_response: %s\n", lbm_errmsg());
			}
		}
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
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	if ((reap_reqs > 0 && request_count >= reap_reqs) || close_recv) {
		/*
		 * Delete the receiver and unblock the event queue dispatcher
		 * once we have received all that was requested or the source
		 * has gone away.
		 */
		lbm_rcv_delete(rcv);
		if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
			exit(1);
		}
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
	lbm_topic_t *topic;
	int c, errflag = 0;
	
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

	while ((c = getopt(argc, argv, "c:hP:r:sv")) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'P':
			response_interval = atoi(optarg);
			break; 
		case 's':
			silent_rate = 1;
			break;
		case 'r':
			reap_reqs = atoi(optarg);
			break;
		case 'v':
			verbose++;
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
	/* Setup logging callback */
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Create LBM context */
	if (lbm_context_create(&ctx, NULL, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}
	
	/* Allocate event queue with associated monitor callback */
	if (lbm_event_queue_create(&evq, evq_monitor, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
		exit(1);
	}
	
	/* Lookup desired topic */
	if (lbm_rcv_topic_lookup(&topic, ctx, argv[optind], NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}

#if !defined(_WIN32)
	signal(SIGHUP, SigHupHandler);
	signal(SIGUSR1, SigUsr1Handler);
	signal(SIGUSR2, SigUsr2Handler);
#endif

	/*
	 * Create an LBM receiver associating a message handler and the looked
	 * up topic info.
	 */
	if (lbm_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}
	if (!silent_rate) {
		current_tv(&starttv);
		/*
		 * Schedule timer to handle display of bandwidth utilization
		 * information.
		 */
		if (lbm_schedule_timer(ctx, rcv_handle_tmo, NULL, evq, 1000) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	/*
	 * Call event queue dispatcher.  This function returns upon failure or
	 * when unblocked by the message handler callback.
	 */
	if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_dispatch returned error.\n");
	}
	printf("Quitting\n");
	SLEEP_SEC(5);
	/* Deallocate LBM context and event queue. */
	lbm_context_delete(ctx);
	ctx = NULL;
	/*
	 * The event queue is external to the LBM context and so may be
	 * deallocated after the context is deleted.
	 */
	lbm_event_queue_delete(evq);
	evq = NULL;
	return 0;
}

