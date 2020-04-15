/*
"lbmresping.c: Create a source and see if it can be resolved.  Tests
"  operation of lbm resolver.  Handy for troubleshooting problems reaching
"  lbm unicast resolvers.  Reports time taken for topic resolution and
"  connection to the source.

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
	#include <netdb.h>
	#include <errno.h>
#endif
#include <lbm/lbm.h>
#include "lbm-example-util.h"

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

#ifndef HAVE_HERROR
#define herror(str) fprintf(stderr, "gethostbyname: failed looking up host '%s'\n", str);
#endif

char usage[] =
"Usage: [-h] [-c filename] [unicast_resolver_host]\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -h = help\n"
;

int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}


struct timeval starttv, endtv;
long our_seq = 0;

int handle_src_event(lbm_src_t *src, int event, void *ed, void *cd)
{
	double msec;
	switch (event) {
	case LBM_SRC_EVENT_CONNECT:
		{
			current_tv(&endtv);
			endtv.tv_sec -= starttv.tv_sec;
			endtv.tv_usec -= starttv.tv_usec;
			normalize_tv(&endtv);
			msec = (double)endtv.tv_sec  * 1000.0 \
			     + (double)endtv.tv_usec / 1000.0;
			printf("Topic resolution and connection seq=%ld, time=%5.3g ms\n", our_seq, msec);
		}
		break;
	case LBM_SRC_EVENT_DISCONNECT:
		break;
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

int rcv_handle_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	return 0;
}

int main(int argc, char **argv)
{
	/* double secs = 0.0; */
	lbm_context_t *ctx;
	lbm_topic_t *topic;
	lbm_src_t *src = NULL;
	lbm_rcv_t *rcv = NULL;
	int c, errflag = 0;
	lbm_context_attr_t * ctx_attr;
	char topic_str[64];
	long our_id;
	
#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;
		
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

	while ((c = getopt(argc, argv, "c:hl:M:P:s:V")) != EOF) {
		switch (c) {
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'h':
			errflag++;
			break;
		default:
			break;
		}
	}
	if (errflag || argc>optind+1) {
		fprintf(stderr, "%s\n%s %s\n", lbm_version(), argv[0], usage);
		exit(0);
	}
	if (lbm_log(lbm_log_msg, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_log: %s\n", lbm_errmsg());
		exit(0);
	}
	if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
		exit(0);
	}
	if (optind == argc-1) {
		struct hostent *hp;
		char *ip, *inet_ntoa();
		if ((hp=gethostbyname(argv[optind])) == NULL) {
			herror(argv[optind]);
			exit(1);
		}
		ip = inet_ntoa(*((struct in_addr*)hp->h_addr));
		printf("Using %s (%s) for unicast topic resolution\n", argv[optind], ip);
		if (lbm_context_attr_str_setopt(ctx_attr, "resolver_unicast_address", ip) != 0) {
			fprintf(stderr, "lbm_context_attr_str_setopt: %s\n", lbm_errmsg());
			exit(1);
		}
	}

	if (lbm_context_create(&ctx, ctx_attr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(ctx_attr);

	srandom(time(0));
	our_id = random();
	
	while (1) {
		sprintf(topic_str, "%ld %ld", our_id, our_seq);
		if (src != NULL) {
			lbm_src_delete(src);
			src = NULL;
		}
		if (lbm_src_topic_alloc(&topic, ctx, topic_str, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_src_create(&src, ctx, topic, handle_src_event, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
			exit(1);
		}
/*
long id, seq;
sscanf(topic_str, "%ld %ld", &id, &seq);
*/

		if (rcv != NULL) {
			lbm_rcv_delete(rcv);
			rcv = NULL;
		}
		current_tv(&starttv);
		if (lbm_rcv_topic_lookup(&topic, ctx, topic_str, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
			exit(1);
		}
		if (lbm_rcv_create(&rcv, ctx, topic, rcv_handle_msg, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
			exit(1);
		}
		SLEEP_SEC(1);
		our_seq++;
	}
	/* Sleep for a bit so that batching gets out all the queued messages, if any.
	   If we just exit, then some messages may not have been sent by TCP yet.
	 */
	SLEEP_SEC(1);
	lbm_context_delete(ctx);
	ctx = NULL;
	return 0;
}
