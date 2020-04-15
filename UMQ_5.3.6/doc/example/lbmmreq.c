/*
"lbmmreq.c: application that sends requests to a given topic (single
"  source) and processes responses.

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

static const char *rcsid_example_lbmmreq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmreq.c#2 $";

#define MAX_NUM_REQUESTS 100000
#define DEFAULT_NUM_REQUESTS 100000
#define MIN_ALLOC_MSGLEN 25

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

char purpose[] = "Purpose: Send request messages from a single source.";
char usage[] =
"Usage: lbmmreq [options] topic\n"
"Available options:\n"
"  -c filename = Use LBM configuration file filename.\n"
"                Multiple config files are allowed.\n"
"                Example:  '-c file1.cfg -c file2.cfg topicname'\n"
"  -d delay = delay sending for delay seconds after source creation\n"
"  -h = help\n"
"  -l len = send messages of len bytes\n"
"  -r rate/pct = send with LBT-RM at rate and retransmission pct%\n"
"  -R requests = send requests number of requests\n"
"  -v = be verbose (-v -v = be even more verbose)\n"
;

/* Utility function to output contents of buffer in hex/ASCII format */
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

/* Source event handler (passed into lbm_src_create()) */
int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
	switch (event) {
	case LBM_SRC_EVENT_CONNECT:
		/*
		 * Indicates that a receiver has connected to the
		 * source (topic)
		 */
		{
			const char *clientname = (const char *)ed;
			
			printf("Receiver connect [%s]\n",clientname);
		}
		break;
	case LBM_SRC_EVENT_DISCONNECT:
		/*
		 * Indicates that a receiver has disconnected from the
		 * source (topic)
		 */
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

int response_count = 0;
int verbose = 0;

/* Response callback (passed into lbm_send_request()) */
int handle_response(lbm_request_t *req, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_RESPONSE:
		response_count++;
		if (verbose) {
			printf("Response [%s], %u bytes\n", msg->source, msg->len);
			if (verbose > 1) 
				dump(msg->data, msg->len);
		}
		if ((response_count % 1000) == 0)
			printf("Received %u responses\n", response_count);
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s]\n", msg->type, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

int main(int argc, char **argv)
{
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src;
	lbm_src_topic_attr_t * tattr;
	lbm_request_t **reqs = NULL;
	lbm_context_attr_t * ctx_attr;
	int requests = DEFAULT_NUM_REQUESTS, count = 0;
	char *message;
	char tmpstr[256] = "";
	int c, errflag = 0, delay = 1;
	size_t msglen = MIN_ALLOC_MSGLEN, tmpstrsz = sizeof(tmpstr);
	char rate[50], mult[2], pct[50];
	unsigned long int rm_rate = 0, rm_retrans;
	
#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;
		
		/* Windows socket starup code */
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

	while ((c = getopt(argc, argv, "c:d:hl:R:v")) != EOF) {
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
		case 'r':
 			if (sscanf(optarg, "%[^/]/%s", rate, pct) != 2) {
 				fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
 				exit(1);
 			}
 			switch (sscanf(rate, "%lu%[kKmMgG]", &rm_rate, mult)) {
 			case 1:
 				break;
 			case 2:
 				switch(mult[0]) {
 				case 'k': case 'K':
 					rm_rate *= 1000;
 					break;
 				case 'm': case 'M':
 					rm_rate *= 1000000;
 					break;
 				case 'g': case 'G':
 					rm_rate *= 1000000000;
 					break;
 				}
 				break;
 			default:
 				fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
 				exit(1);
 			}
 			switch (sscanf(pct, "%lu%[kKmMgG%]", &rm_retrans, mult)) {
 			case 1:
 				break;
 			case 2:
 				switch(mult[0]) {
 				case 'k': case 'K':
 					rm_retrans *= 1000;
 					break;
 				case 'm': case 'M':
 					rm_retrans *= 1000000;
 					break;
 				case 'g': case 'G':
 					rm_retrans *= 1000000000;
 					break;
 				case '%':
 					rm_retrans = rm_rate/100*rm_retrans;
 					break;
 				}
 				break;
 			default:
 				fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
 				exit(1);
 			}
 			break;
		case 'R':
			requests = atoi(optarg);
			if (requests > MAX_NUM_REQUESTS) {
				fprintf(stderr, "Too many requests. Max number of requests is %u.\n",MAX_NUM_REQUESTS);
				errflag++;
			}
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
	if ((reqs = malloc(sizeof(lbm_request_t *) * MAX_NUM_REQUESTS)) == NULL) {
		fprintf(stderr, "could not allocate requests array\n");
		exit(1);
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
 		printf("Sending with LBT-RM data rate limit %lu, retransmission rate limit %lu\n", rm_rate, rm_retrans);
		/* Set transport attribute to LBT-RM */
 		if (lbm_src_topic_attr_str_setopt(tattr, "transport", "LBTRM") != 0) {
 			fprintf(stderr, "lbm_src_topic_str_setopt:transport: %s\n", lbm_errmsg());
 			exit(1);
 		}
		/* Set LBT-RM data rate attribute */
 		if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
		/* Set LBM-RM retransmission rate attribute */
 		if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_retransmit_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
 	}
	/* Create LBM context (passing in context attributes) */
	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	tmpstrsz = sizeof(tmpstr);
	/* Retrieve TCP port used for responses */
	if (lbm_context_str_getopt(ctx, "request_tcp_port", tmpstr, &tmpstrsz) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_str_getopt(request_tcp_port): %s\n", lbm_errmsg());
		exit(1);
	}
	printf("Using TCP port %s for responses\n", tmpstr);

	/* Allocate a source topic */
	if (lbm_src_topic_alloc(&topic, ctx, argv[optind], tattr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_src_topic_attr_delete(tattr);

	/*
	 * Create source with associated topic (allocated above)
	 * and event handler
	 */
	if (lbm_src_create(&src, ctx, topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
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
	
	if (delay > 0) {
		printf("Delaying requests for %d second%s...\n", delay, ((delay > 1) ? "s" : ""));
		SLEEP_SEC(delay);
	}
	
	printf("Sending requests...\n");
	
	/* Start sending requests */
	for (count = 0; count < requests; count++) {
		sprintf(message, "request data %u", count);
		/*
		 * Send the request with data payload message.
		 * The callback for the response is specified here as
		 * handle_response().  The request data payload is message.
		 * The request object pointer is returned at &(reqs[count]).
		 */
		if (lbm_send_request(&(reqs[count]), src, message,
			 msglen, handle_response, NULL, NULL, 0) == LBM_FAILURE) {
			fprintf(stderr, "lbm_send_request: %s\n", lbm_errmsg());
			exit(1);
		}
		if (verbose)
			printf("Sent request %d\n", count);
		if ((count % 1000) == 0) {
			/*
			 * The sleep here rate controls how fast we send.
			 * With 100 msec every 1000, we will be maxing our
			 * pace at slightly less than 10K requests/sec.
			 */
			SLEEP_MSEC(100);
		}
	}
	/*
	 * Linger allows transport to complete data transfer and recovery before
	 * context is deleted and socket is torn down.
	 */
	printf("Lingering...");
	SLEEP_SEC(10);
	printf("\n");
	/*
	 * The following (although not strictly necessary in this example)
	 * demonstrates how to delete requests along with the source and LBM
	 * context.
	 */
	printf("Deleting requests...\n");
	for (count = 0; count < requests; count++) {
		lbm_request_delete(reqs[count]);
		if (count > 0 && (count % 1000) == 0)
			printf("Deleted %u requests\n", count);
	}
	printf("Quitting...\n");
	SLEEP_SEC(5);
	lbm_src_delete(src);
	src = NULL;
	SLEEP_SEC(5);
	lbm_context_delete(ctx);
	ctx = NULL;
	free(reqs);
	return 0;
}

