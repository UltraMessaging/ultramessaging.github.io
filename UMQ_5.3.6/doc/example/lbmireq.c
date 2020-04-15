/*
"lbmireq.c: application that sends immediate requests to a given topic
"  (single source) and waits for responses.

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

static const char *rcsid_example_lbmireq = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmireq.c#2 $";

#define DEFAULT_NUM_REQUESTS 1
#define DEFAULT_PAUSE_SEC 5
#define DEFAULT_INITIAL_DELAY_MSEC 1000
#define DEFAULT_DELAY_B4CLOSE 5
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

char purpose[] = "Purpose: Send immediate request messages on a single topic.";
char usage[] =
"Usage: [-hv] [-c filename] [-l len] [-L linger] [-P sec] [-r rate/pct]\n"
"       [-R requests] [-T target] [topic]\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -h = help\n"
"       -l len = send messages of len bytes\n"
"       -L linger = linger for linger seconds before closing context\n"
"       -P sec = pause sec seconds after sending request for responses to arrive\n"
"       -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                          DATA bits per second, and set retransmit rate limit to\n"
"                          RETR bits per second.  For both limits, the optional\n"
"                          k, m, and g suffixes may be used.  For example,\n"
"                          '-r 1m/500k' is the same as '-r 1000000/500000'\n"
"       -R requests = send requests number of requests\n"
"       -T target = send immediate request to target\n"
"       -v = be verbose (-v -v = be even more verbose)\n"
;

/* Function to dump buffer contents in hex/ASCII format. */ 
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

/* Logging callback function (passed to lbm_log()) */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

size_t response_byte_count = 0;
int response_count = 0;
int verbose = 0;

/* Callback handler for responses (passed to lbm_*_immediate_request) */
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

int main(int argc, char **argv)
{
	lbm_context_t *ctx;
	lbm_request_t *req;
	lbm_context_attr_t * ctx_attr;
	int requests = DEFAULT_NUM_REQUESTS, count = 0;
	char *message, portstr[256] = "14392";
	char tmpstr[256] = "";
	char topicname[256] = "", targetname[256] = "";
	char *topic = NULL, *target = NULL;
	int c, errflag = 0, pause_sec = DEFAULT_PAUSE_SEC, result = 0;
	int linger = DEFAULT_DELAY_B4CLOSE;
	size_t msglen = MIN_ALLOC_MSGLEN, tmpstrsz = sizeof(tmpstr);
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	
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

	while ((c = getopt(argc, argv, "c:hL:l:P:p:r:R:T:v")) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'L':
			linger = atoi(optarg);
			break;
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'P':
			pause_sec = atoi(optarg);
			break;
		case 'p':
			strncpy(portstr, optarg, sizeof(portstr));
			break;
		case 'r':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 'R':
			requests = atoi(optarg);
			break;
		case 'T':
			strncpy(targetname, optarg, sizeof(targetname));
			target = targetname;
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
	if (errflag) {
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}
	if (argc > optind) {
		strncpy(topicname, argv[optind], sizeof(topicname));
		topic = topicname;
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
	 * Bind to a specific port for requests so that we know where things are
	 * coming into.
	 */
	if (lbm_context_attr_str_setopt(ctx_attr, "request_tcp_port", portstr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_attr_str_setopt - request_tcp_port: %s\n", lbm_errmsg());
		exit(1);
	}
 	if (rm_rate != 0) {
 		printf("Sending with LBT-RM data rate limit %" PRIu64 ", retransmission rate limit %" PRIu64 "\n", rm_rate, rm_retrans);
		/* Set LBT-RM data rate */
 		if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
		/* Set LBT-RM retransmission rate */
 		if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_retransmit_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
 	}
	/* Create LBM context passing in any new context level attributes */
	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	tmpstrsz = sizeof(tmpstr);
	/*
	 * Retrieve TCP response port from context. This is not strictly
	 * necessary, since we just set this when we initialized the context
	 * above.
	 */
	if (lbm_context_str_getopt(ctx, "request_tcp_port", tmpstr, &tmpstrsz) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_str_getopt(request_tcp_port): %s\n", lbm_errmsg());
		exit(1);
	}
	printf("Using TCP port %s for responses\n", tmpstr);
		
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
	
	printf("Sending %u requests of size %u bytes to target <%s> topic <%s>\n",
		   requests, msglen, ((target == NULL) ? "" : target),
		   ((topic == NULL) ? "" : topic));

	for (count = 0; count < requests; count++) {
		sprintf(message, "request data %u", count);
		printf("Sending request %u\n", count);

		/*
		 * If no target was specified, send immediate request using
		 * LBT-RM; otherwise, use unicast.  Response callback function
		 * is passed in here.
		 */
		if (target == NULL) {
			result = lbm_multicast_immediate_request(&req, ctx, topic, message, msglen,
													 handle_response, NULL, NULL, 0);
		} else {
			result = lbm_unicast_immediate_request(&req, ctx, target, topic, message, msglen,
												   handle_response, NULL, NULL, 0);
		}
		if (result == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
			exit(1);
		}		
		if (verbose)
			printf("Sent request %d. Pausing %d seconds.\n", count, pause_sec);
		/*
		 * Wait some time for responses to come in, then delete the
		 * request.
		 */
		SLEEP_SEC(pause_sec);
		lbm_request_delete(req);
		printf("Done waiting for responses. %d responses (%u bytes) received. Deleting request.\n",
			   response_count, response_byte_count);
		response_count = 0;
		response_byte_count = 0;
	}
	printf("Quitting...\n");
	/*
	 * Linger allows transport to complete data transfer and recovery before
	 * context is deleted and socket is torn down.
	 */
	if (linger > 0) {
		printf("Lingering for %d seconds...", linger);
		SLEEP_SEC(linger);
		printf("\n");
	}
	lbm_context_delete(ctx);
	ctx = NULL;
	free(message);
	return 0;
}

