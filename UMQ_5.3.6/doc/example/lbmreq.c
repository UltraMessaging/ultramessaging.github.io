/*
"lbmreq.c: application that sends requests on a given topic (single
"  source) and waits for responses.

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

static const char *rcsid_example_lbmreq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmreq.c#2 $";

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_NUM_REQUESTS 10000000
#define DEFAULT_PAUSE_SEC 5

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

char purpose[] =
"Purpose: Send request messages from  a single source with settable interval\n"
"         between messages.";
char usage[] =
"Usage: lbmreq [options] topic\n"
"Available options:\n"
"  -c filename = Use LBM configuration file filename.\n"
"                Multiple config files are allowed.\n"
"                Example:  '-c file1.cfg -c file2.cfg'\n"
"  -d sec = delay sending for delay seconds after source creation\n"
"  -h = help\n"
"  -l len = send messages of len bytes\n"
"  -L linger = linger for linger seconds before closing context\n"
"  -P sec = pause sec seconds after sending request for responses to arrive\n"
"  -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                     DATA bits per second, and set retransmit rate limit to\n"
"                     RETR bits per second.  For both limits, the optional\n"
"                     k, m, and g suffixes may be used.  For example,\n"
"                     '-r 1m/500k' is the same as '-r 1000000/500000'\n"
"  -R requests = send requests number of requests\n"
"  -q = Use Event Queue\n"
"  -v = be verbose (-v -v = be even more verbose)\n"
;

/* Utility to print out contents of buffer in hex/ASCII format */
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

/* Logging callback function (passed into lbm_log()) */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

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

int evq_timer_id = -1;
size_t response_byte_count = 0;
int response_count = 0;
int verbose = 0;
lbm_event_queue_t *evq = NULL;

/* Response handler callback (passed into lbm_send_request()) */
int handle_response(lbm_request_t *req, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_RESPONSE:
		response_count++;
		response_byte_count += msg->len;
		if (verbose) {
			printf("Response [%s][%u], %u bytes\n", msg->source, msg->sequence_number,msg->len);
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s]\n", msg->type, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* Timer callback to handle termination of event pump */
int handle_evq_timer(lbm_context_t *ctx, const void *clientd)
{
	lbm_event_queue_t *evq = (lbm_event_queue_t *)clientd;
	if ( verbose )
		printf ( "Timer fired, unblocking queue\n" );
	if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
		exit(1);
	}
	return 0;
}


int main(int argc, char **argv)
{
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src;
	lbm_src_topic_attr_t * tattr;
	lbm_request_t *req;
	lbm_context_attr_t * ctx_attr;
	int requests = DEFAULT_NUM_REQUESTS, count = 0;
	int linger = 5;
	char portstr[256] = "14392";
	char tmpstr[256] = "";
	char *message = NULL;
	int c, errflag = 0, pause_sec = DEFAULT_PAUSE_SEC,
		delay = 1;
	size_t msglen = MIN_ALLOC_MSGLEN, tmpstrsz = sizeof(tmpstr);
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	int eventq = 0;
	
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

	while ((c = getopt(argc, argv, "c:d:hl:L:P:p:r:R:vq")) != EOF) {
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
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'L':
			linger = atoi(optarg);
			break;
		case 'P':
			pause_sec = atoi(optarg);
			break;
		case 'p':
			strncpy(portstr, optarg, sizeof(portstr));
			break;
		case 'q':
			eventq++;
			break;
		case 'r':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 'R':
			requests = atoi(optarg);
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
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
	/* Establish logging callback */
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
		fprintf(stderr, "could not allocate message buffer of size %u bytes\n",msglen);
		exit(1);
	}
	
	memset(message, 0, msglen);

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
	}
	tmpstrsz = sizeof(tmpstr);
	/*
	 * Retrieve TCP request port setting (this isn't strictly necessary here
	 * since we just set this attribute above).
	 */
	if (lbm_context_str_getopt(ctx, "request_tcp_port", tmpstr, &tmpstrsz) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_str_getopt(request_tcp_port): %s\n", lbm_errmsg());
		exit(1);
	}
	printf("Using TCP port %s for responses\n", tmpstr);


	/* Allocate desired topic */
	if (lbm_src_topic_alloc(&topic, ctx, argv[optind], tattr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_src_topic_attr_delete(tattr);
	/*
	 * Create source object with associated topic info (from above) and
	 * source event handler callback.
	 * Note that even when using an event queue for the receiving responses, the event queue does 
	 * have to get specified here.  Specifying it here only forces the events that are caught by handle_src_event
	 * to get handled via the event queue.
	 */
	if (lbm_src_create(&src, ctx, topic, handle_src_event, NULL, evq ) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
		exit(1);
	}
	
	if (delay > 0) {
		printf("Delaying requests for %d second%s...\n", delay, (delay > 1 ? "s" : ""));
		SLEEP_SEC(delay);
	}
	
	if (requests > 0) 
		printf("Will send %d request%s\n", requests, (requests == 1 ? "" : "s"));

	for (count = 0; count < requests; count++) {
		sprintf(message, "request data %u", count);
		printf("Sending request %u\n", count);
		/* Send request noting the callback to handle the response. */
		if (lbm_send_request(&req, src, message, msglen, handle_response, NULL, evq, 0) == LBM_FAILURE) {
			fprintf(stderr, "lbm_send_request: %s\n", lbm_errmsg());
			exit(1);
		}
		if (verbose)
			printf("Sent request %d.  ", count ); 

		/* Wait some time for responses to come in. Then delete the request. */
		if (!eventq)
		{
			if ( verbose )
				fprintf ( stderr, "Pausing %d seconds.\n", pause_sec);
			SLEEP_SEC(pause_sec);
		}
		else
		{
			fprintf ( stderr, "Starting event pump for %d seconds.\n", pause_sec);
			/* Set a timer to guarantee the queue will not block indefinitely */
			if ((evq_timer_id = lbm_schedule_timer(ctx, handle_evq_timer, evq, evq, pause_sec * 1000 )) == -1) 
				fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			/* Start the event pump */
			if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) 
				fprintf(stderr, "lbm_event_dispatch returned error.\n");
		}
		/* Delete request */
		lbm_request_delete(req);
		printf("Done waiting for responses. %d response%s (%u total bytes) received. Deleting request.\n\n",
			   response_count, (response_count == 1 ? "" : "s"), response_byte_count);
		response_count = 0;
		response_byte_count = 0;
	}
	
	if (linger > 0) {
		printf("Lingering for %d second%s...\n", linger, (linger > 1 ? "s" : ""));
		SLEEP_SEC(linger);
	}
	
	printf("Quitting...\n");
	
	/* Delete allocated source and LBM context */
	lbm_src_delete(src);
	src = NULL;
	
	lbm_context_delete(ctx);
	ctx = NULL;
	free(message);
	if (eventq)
		lbm_event_queue_delete(evq);
	return 0;
}

