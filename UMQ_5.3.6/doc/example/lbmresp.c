/*
"lbmresp.c: application that waits for requests and sends responses back
"  on a given topic (single receiver).

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
#include <lbm/lbmmon.h>

static const char *rcsid_example_lbmresp = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmresp.c#2 $";

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

char purpose[] = "Purpose: Respond to request messages on a single topic.";
char usage[] =
"Usage: [-Ehsv] [-c filename] [-l len] [-r responses] [-f topic] topic\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -E = end after end-of-stream\n"
"       -h = help\n"
"       -l len = use len bytes for the length of each response\n"
"       -r responses = send responses messages for each request\n"
"       -s = be silent about incoming messages\n"
"       -v = be verbose (-v -v = be even more verbose)\n"
"       -f = forward request to responders listening on given topic\n"
;

/* Utility to print contents of buffer in hex/ASCII format */
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

#define DEFAULT_RESPONSE_LEN 25

int request_count = 0;
int verbose = 0;
int close_recv = 0;
int responses = 1;
size_t response_len = DEFAULT_RESPONSE_LEN;
lbm_msg_t *response_msg = NULL;
lbm_response_t *response = NULL;
lbm_serialized_response_t *serialized_response = NULL;
char *response_buffer = NULL;
int end_on_eos = 0;
lbm_context_t *ctx;

/*
 * Received immediate message handler (for null-topic immediate sends)
 * (passed into lbm_context_rcv_immediate_msgs())
 */
int rcv_handle_immediate_msg(lbm_context_t *ctx, lbm_msg_t *msg, void *clientd)
{
	int skipped;

	switch (msg->type) {
	case LBM_MSG_DATA:
		if (verbose) {
			printf("IM [%s][%u], %u bytes\n",
				   msg->source, msg->sequence_number, msg->len);
			if (verbose > 1) 
				dump(msg->data, msg->len);
		}
		break;
	case LBM_MSG_REQUEST:
		request_count++;
		/* Skip any requests without any response information */
		skipped = response != NULL;
		if (verbose) {
			printf("IM Request [%s][%u], %u bytes%s\n",
				   msg->source, msg->sequence_number, msg->len,
				   skipped ? " (ignored)"  : "");
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		if (!skipped)
		{
			/*
			 * Retain the lbm_msg_t object to keep the response
			 * object around. 
			 */
			lbm_msg_retain(msg); 
			response_msg = msg;
			response = msg->response;
		}
		break;
	default:
		printf("Unknown immediate message lbm_msg_t type %x [%s]\n", msg->type, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

/* Received message handler callback */
int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	int skipped;

	switch (msg->type) {
	case LBM_MSG_DATA:
		/* Check to see if this is a forwarded request that we should
		 * respond to. */
		if (strncmp(msg->data, "FORWARDTO:", 10) == 0) {
			if (verbose) {
				printf("Received forwarded request: %s\n", msg->data + 10 + sizeof(lbm_serialized_response_t) + 1);
			}
			lbm_msg_retain(msg);
			response_msg = msg;
			response = lbm_deserialize_response(ctx, (lbm_serialized_response_t*)(strchr(msg->data, ':') + 1));	
		}
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
		if (end_on_eos)
			close_recv = 1;
		break;
	case LBM_MSG_REQUEST:
		request_count++;
		/* Skip any requests without any response information */
		skipped = response != NULL;
		if (verbose) {
			printf("Request [%s][%s][%u], %u bytes%s\n",
				   msg->topic_name, msg->source, msg->sequence_number,
				   msg->len, skipped ? " (ignored)" : "");
			if (verbose > 1)
				dump(msg->data, msg->len);
		}
		if (!skipped)
		{
			/*
			 * Retain the lbm_msg_t object to keep the response
			 * object around.
			 */
			lbm_msg_retain(msg);
			response_msg = msg;
			response = msg->response;
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		if (verbose) {
			printf("[%s][%s][%u], LOST\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		if (verbose) {
			printf("[%s][%s][%u], LOST BURST\n",
				   msg->topic_name, msg->source, msg->sequence_number);
		}
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	/* LBM automatically deletes the lbm_msg_t object unless we retain it. */
	return 0;
}

int main(int argc, char **argv)
{
	lbm_topic_t *topic;
	lbm_topic_t *forwarding_topic;
	lbm_rcv_t *rcv;
	lbm_src_t *src; /* Regular source to send forwarded requests. */
	int c, i = 0, errflag = 0;
	char forward_topic[256] = "";
	int forward_requests = 0;
	lbm_context_attr_t * ctx_attr;
	unsigned short int request_port;
	int request_port_bound;
	size_t optlen;
	lbm_ipv4_address_mask_t unicast_target_iface;
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
#endif /* _WIN32 */

	while ((c = getopt(argc, argv, "c:Ehl:r:vf:")) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'E':
			end_on_eos++;
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'l':
			response_len = atoi(optarg);
			break;
		case 'r':
			responses = atoi(optarg);
			if (responses <= 0){
				/*Negative # of responses not allowed*/
				fprintf(stderr, "%s\n%s\n%s\n%s", argv[0], lbm_version(), purpose, usage);
				exit(1);
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'f':
			strncpy(forward_topic, optarg, sizeof(forward_topic));
			response_len += sizeof(lbm_serialized_response_t) + 12;
			forward_requests = 1;
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

	if ((response_buffer = malloc(response_len + sizeof(lbm_serialized_response_t) + 11)) == NULL) {
		fprintf(stderr, "could not allocate response buffer of size %u bytes\n",response_len);
		exit(1);
	}
	sprintf(response_buffer,"response message");

	/* Retrieve current context settings */
	if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Create LBM context (passing in context attributes) */
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
	if (lbm_context_getopt(ctx,
				"request_tcp_bind_request_port",
				&request_port_bound,
				&optlen) == LBM_FAILURE)
	{
		fprintf(stderr, "lbm_context_getopt(request_tcp_bind_request_port): %s\n",
				lbm_errmsg());
		exit(1);
	}
	if (request_port_bound == 1) {
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
	} else {
	
		printf("Request port binding disabled, no immediate messaging target.\n");
	}
	/*
	 * Create an LBM immediate message receiver passing in
	 * a received message handler.  For *topicless* immediate sends.
	 */
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
	 * Create an LBM receiver associating a message handler and the looked up
	 * topic info.
	 */
	if (lbm_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}

	/* If we're forwarding, lookup the forwarding topic and create a source
	 * to send forwarding messages from.
	 */
	if (forward_requests) {
		if (lbm_src_topic_alloc(&forwarding_topic, ctx, forward_topic, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_src_create(&src, ctx, forwarding_topic, NULL, NULL, NULL) == LBM_FAILURE) {
		 	fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
			exit(1);
		}
	}
	 
	/* clear the buffer so valgrind is happy */
	memset(response_buffer, 0, response_len);
	
	/* Loop waiting for requests */
	while (1) {
		SLEEP_MSEC(100);
		if (response != NULL) {
			if (verbose) {
				if (!forward_requests) {
					printf("Sending response. %u response%s of %u bytes%s (%u total bytes).\n",
					 	   responses, (responses == 1 ? "" : "s"), response_len, (responses == 1 ? "" : " each"), 
						   (responses * response_len));
				}
				else {
					printf("Forwarding request to topic \"%s\".\n", forward_topic);
				}
					   
			}
			if (!forward_requests) {
				for (i = 0; i < responses; i++) {
					sprintf(response_buffer, "response %u", i);
					/* Send response(s) */
					if (lbm_send_response(response, response_buffer, response_len, 0) == LBM_FAILURE) {
						fprintf(stderr, "lbm_send_response: %s\n", lbm_errmsg());
						break;
					}
				}
				printf("Done sending responses. Deleting response.\n\n");
			}
			else {  /* Forward this request to other responders. */
				for (i = 0; i < responses; i++) {
					serialized_response = lbm_serialize_response(response);
					memcpy(response_buffer, "FORWARDTO:", 10);
					memcpy(response_buffer + 10, serialized_response, sizeof(lbm_serialized_response_t));
					sprintf(response_buffer + 10 + sizeof(lbm_serialized_response_t), ":response %u", i);
					if (lbm_src_send(src, response_buffer, response_len + sizeof(lbm_serialized_response_t) + 11, LBM_MSG_FLUSH) == LBM_FAILURE) {
						fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
						break;
					}
					lbm_serialized_response_delete(serialized_response);
				}
				printf("Done forwarding requests.  Deleting response.\n");
			}
			/*
			 * Delete the lbm_msg_t containing the response info
			 * which was retained in the received message callback.
			 */
			lbm_msg_delete(response_msg);
			response_msg = NULL;
			response = NULL;
		}
		if (close_recv) {
			/* Delete the receiver if the source has gone away */
			lbm_rcv_delete(rcv);
			rcv = NULL;
		}
		if (rcv == NULL)
			break;
	}
	printf("Quitting\n");
	/* SLEEP_SEC(5); */
	
	/* Delete the forwarding source if we were forwarding. */
	if (forward_requests) {
		lbm_src_delete(src);
	}
	
	/* Delete the LBM context */
	lbm_context_delete(ctx);
	ctx = NULL;
	free(response_buffer);
	return 0;
}

