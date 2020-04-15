/*
"lbmimsg.c: application that sends immediate messages as fast as it can
"  to a given topic (single source).

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

static const char *rcsid_example_lbmimsg = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmimsg.c#2 $";

#define MIN_ALLOC_MSGLEN 25
#define DEFAULT_MAX_MESSAGES 10000000
#define DEFAULT_DELAY_B4CLOSE 5

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

char purpose[] = "Purpose: Send immediate messages on a single topic or send topic-less messages.";
char usage[] =
"Usage: lbmimsg [options] -o OR topic\n"
"Available options:\n"
"  -c filename = Use LBM configuration file filename.\n"
"                Multiple config files are allowed.\n"
"                Example:  '-c file1.cfg -c file2.cfg'\n"
"  -d delay = delay sending for delay seconds after source creation\n"
"  -h = help\n"
"  -l len = send messages of len bytes\n"
"  -L linger = linger for linger seconds before closing context\n"
"  -M msgs = send msgs number of messages\n"
"  -n num = Append a number between 1 and num to topic\n"
"  -o = send topic-less immediate messages\n"
"  -P msec = pause after each send msec milliseconds\n"
"  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                     DATA bits per second, and set retransmit rate limit to\n"
"                     RETR bits per second.  For both limits, the optional\n"
"                     k, m, and g suffixes may be used.  For example,\n"
"                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
"  -T target = target for unicast immediate messages\n"
;

/* for the elapsed time, calculate and print the msgs/sec and bits/sec */
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

/* Logging callback function (given as an argument to lbm_log()) */
int lbm_log_msg(int level, const char *message, void *clientd)
{
	printf("LOG Level %d: %s\n", level, message);
	return 0;
}

void print_help_exit(char **argv, int exit_value){
	fprintf(stderr, "%s\n%s\n%s", 
					lbm_version(), purpose, usage);
	exit(exit_value);
}

int done_sending = 0;

int main(int argc, char **argv)
{
	double secs = 0.0;
	lbm_context_t *ctx;
	lbm_context_attr_t * cattr;
	struct timeval starttv, endtv;
	int msgs = DEFAULT_MAX_MESSAGES, count = 0, bytes_sent = 0;
	char *message;
	char targetname[256] = "", topicname[256] = "";
	char *target = NULL, *topic = NULL;
	int c, errflag = 0, pause = 0, topic_less = 0, delay = 1;
	int linger = DEFAULT_DELAY_B4CLOSE;
	size_t msglen = MIN_ALLOC_MSGLEN;
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	int isfx = 0; /* evade compiler warning */
	int nsfx = 0;
	int csfx = 1;
	
#if defined(_WIN32)
	{
		WSADATA wsadata;
		int status;
		
		/* Code to initialize socket interface on Windows */
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

	while ((c = getopt(argc, argv, "c:d:hL:l:M:n:oP:R:T:")) != EOF) {
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
		case 'L':
			linger = atoi(optarg);
			break;
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'M':
			msgs = atoi(optarg);
			break;
		case 'n':
			nsfx = atoi(optarg);
			break;
		case 'o':
			topic_less = 1;
			break;
		case 'h':
			print_help_exit(argv, 0);
		case 'P':
			pause = atoi(optarg);
			break;
		case 'R':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 'T':
			strncpy(targetname, optarg, sizeof(targetname));
			target = targetname;
			break;
		default:
			errflag++;
			break;
		}
	}
	
	if ((errflag != 0) || (optind == argc && topic_less != 1)) {
		/* An error occurred processing the command line - print help and exit */
		print_help_exit(argv, 1);
	}
	
	if (argc > optind && topic_less == 1) {
		/* User chose topic-less and yet specified a topic - print help and exit */
		printf("lbmimsg: error--selected topic-less option and still specified topic\n");
		print_help_exit(argv, 1);
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
	if (lbm_context_attr_create(&cattr) == LBM_FAILURE) {
 		fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
 		exit(1);
 	}
 	if (rm_rate != 0) {
 		printf("Sending with LBT-RM data rate limit %" PRIu64 ", retransmission rate limit %" PRIu64 "\n", rm_rate, rm_retrans);
		/* Set LBT-RM data rate */
 		if (lbm_context_attr_setopt(cattr, "transport_lbtrm_data_rate_limit", &rm_rate, sizeof(rm_rate)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_data_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
		/* Set LBT-RM retransmission rate */
 		if (lbm_context_attr_setopt(cattr, "transport_lbtrm_retransmit_rate_limit", &rm_retrans, sizeof(rm_retrans)) != 0) {
 			fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_retransmit_rate_limit: %s\n", lbm_errmsg());
 			exit(1);
 		}
 	}
	/* Create LBM context passing in any new context level attributes */
	if (lbm_context_create(&ctx, cattr, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}
	lbm_context_attr_delete(cattr);
		
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
		printf("Will start sending in %d second%s...\n", delay, ((delay > 1) ? "s" : ""));
		SLEEP_SEC(delay);
	}
	
	printf("Sending %u%s immediate messages of size %u bytes to target <%s> topic <%s>\n",
		   msgs, ((topic_less == 1) ? " topic-less" : ""), msglen, ((target == NULL) ? "" : target),
		   ((topic == NULL) ? "" : topic));
		   
	current_tv(&starttv);
	for (count = 0; count < msgs; count++) {
		int result = 0;
		
		sprintf(message, "message %u", count);

		/*
		 * If no target was specified, send immediate message using
		 * LBT-RM; otherwise, use unicast.
		 */
		if (nsfx > 0 && topic)
		{
			if (count == 0)
				isfx = strlen(topic);
			sprintf(&topic[isfx], "%d", csfx);
			if (++csfx > nsfx)
				csfx = 1;
				
		}
		if (target == NULL) {
			result = lbm_multicast_immediate_message(ctx, topic, message, msglen, 0);
		} else {
			result = lbm_unicast_immediate_message(ctx, target, topic, message, msglen, 0);
		}
		if (result == LBM_FAILURE) {
			if (lbm_errnum() == LBM_EOP && target != NULL) {
				fprintf(stderr, "LBM send error: no connection to target while sending unicast immediate message\n");
			}
			else {
				fprintf(stderr, "LBM send error: %s\n", lbm_errmsg());
			}
			exit(1);
		}
		bytes_sent += msglen;
		if (pause > 0) {
			SLEEP_MSEC(pause);
		}
	}
	current_tv(&endtv);
	endtv.tv_sec -= starttv.tv_sec;
	endtv.tv_usec -= starttv.tv_usec;
	normalize_tv(&endtv);
	secs = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
	printf("Sent %u%s immediate messages of size %u bytes in %.04g seconds.\n",
			count, ((topic_less == 1) ? " topic-less" : ""), msglen, secs);
	print_bw(stdout, &endtv, count, bytes_sent);
	
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

