/*
"lbmspike.c: application that generates & receives message spikes for
" performance testing.

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
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <sys/timeb.h>
	#define snprintf _snprintf
	#define vsnprintf _vsnprintf
	#define WIN32_HIGHRES_TIME
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/time.h>
	#include <signal.h>
	#if defined(__TANDEM)
		#include <netdb.h>
	#endif
#endif
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "lbm-example-util.h"

static const char *rcsid_example_lbmspike = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmspike.c#2 $";

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

#if !defined(HOST_NAME_MAX)
#define	HOST_NAME_MAX	255	/* sys/syslimits.h doesn't define this yet */
#endif

#define	CNTL_MSG_MAX	40	/* CoNTroL MeSsaGe MAXimum length (bytes) */

/* Control topic messages */
#define	MAGIC_SETUP	19570913
#define MAGIC_DONE	19601005

/* Data topic messages */
#define	MAGIC_BGHUM	19911208
#define	MAGIC_SPIKE	19950806

/*
 * ToDo XXX
 * Is there an assert macro I should be using? (test for assert.h on Win)
 */
char purpose[] = "Purpose: Source & receive message spikes for performance testing.";
char usage[] =
"Usage: -R [-dhq] [-c filename] [-o ord] [-u bufsiz] [topic]\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -d = dump message time stamps to a file\n"
"       -h = help\n"
"       -o ord = set receiver ordered delivery to ord\n"
"       -q = processess received messages on an event queue\n"
"       -R = role is receiver (default role is source)\n"
"       -u bufsiz = UDP buffer size for LBT-RM\n"
"Usage: [-dhLn] [-B bghumms] [-c filename] [-l len] [-M msgs] [-r rate/pct] [-v recovms] [topic]\n"
"       -B bghumms = milliseconds between \"background hum\" messages\n"
"       -c filename = read config file filename\n"
"       -d = dump message time stamps to a file\n"
"       -h = help\n"
"       -l len = use len length messages\n"
"       -L = use TCP-LB\n"
"       -M msgs = stop after receiving msgs messages\n"
"       -n = use non-blocking writes\n"
"       -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
"                          DATA bits per second, and set retransmit rate limit to\n"
"                          RETR bits per second.  For both limits, the optional\n"
"                          k, m, and g suffixes may be used.  For example,\n"
"                          '-r 1m/500k' is the same as '-r 1000000/500000'\n"
"       -v recovms = milliseconds after spike to allow for recovery\n"
;


int roleRcv = 0;	/* ROLE is ReCeiVer (default role is source */
int ordered_delivery = -1;
unsigned long int udpbuf = 0;
lbm_uint_t msgs = 1000;
size_t msglen = 1000;
struct timeval starttv, endtv;
int received = 0, lost = 0, blost = 0, bghum = 0, pastgap = 0;
double totlate= 0.0;	/* TOTal delivery LATEncy for all messages != first */
double baselate;	/* BASE LATEncy (rcv clock - src clock) */
lbm_rcv_transport_stats_t startstats;
lbm_rcv_transport_stats_t endstats;

/*
 * Printf for and buffer for result.
 */
char result[2048];
int restail = 0;
void resprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	restail += vsnprintf(&result[restail], sizeof(result)-restail, fmt, ap);
	va_end(ap);
}

/*
 * For the elapsed time, calculate and print the msgs/sec and bits/sec
 */
void print_bw(double sec, unsigned int msgs, unsigned int byts, int tot)
{	
	char scale[] = {'\0', 'K', 'M', 'G'};
	int msg_scale_index = 0, bit_scale_index = 0, byte_scale_index = 0;
	double KBscale = 1024.0, kscale = 1000.0;
	double mps = 0.0, bps = 0.0, pct = 0.0;
	double bytes = byts, total = tot;
	
	if (sec == 0.0) return;	/* avoid div by zero */
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
	
	pct = 100.0*bytes/total;
	
	/* Minimum scale for bytes is K - always divide at least once */
	do {
		bytes /= KBscale;
		byte_scale_index++;
	} while (bytes >= KBscale);
	
	resprintf("%5.4g %cB (%3.0f%%) %5.4g %cmsgs/s %5.4g %cbps",
		bytes, scale[byte_scale_index], pct, mps, scale[msg_scale_index], bps, scale[bit_scale_index]);
}

/*
 * Return string with our role (Src or Rcv) with partially-qualified
 * hostname.
 */
char * rolehost()
{
	char hnbuf[HOST_NAME_MAX];		/* HostName BUFfer */
	static char rhbuf[HOST_NAME_MAX+5];	/* Role Host BUFfer */
	static char *retval = NULL;		/* RETurn VALue */
	char *cp;

	if (retval == NULL) {
		gethostname(hnbuf, sizeof(hnbuf));
		/* clobber domain, if any */
		for (cp=hnbuf; *cp!='\0'&&*cp!='.'; cp++) {
		}
		*cp = '\0';
		snprintf(rhbuf, sizeof(rhbuf), "%s.%s",
			roleRcv?"Rcv":"Src", hnbuf);
		retval = rhbuf;
	}
	return(retval);
}

/*
 * Important concepts
 * Background hum
 * Spike
 * Train
 * Gap, nogapnext, pastgap
 */

lbm_event_queue_t *evq = NULL;
lbm_event_queue_t *evqd = NULL;	/* EVent Queue used for data receiver (if any) */
lbm_context_t *ctxcr = NULL;	/* lbm ConTeXt for Control and Results */
lbm_context_t *ctxd = NULL;	/* lbm ConTeXt for Data */
lbm_src_t *srcc = NULL;		/* lbm SouRCe used to send Control */
lbm_src_t *srcd = NULL;		/* lbm SouRCe used to send Data */
lbm_src_t *srcr = NULL;		/* lbm SouRCe used to send Results */
lbm_rcv_t *rcvc = NULL;		/* lbm ReCeiVer used to receive Control */
lbm_rcv_t *rcvd = NULL;		/* lbm ReCeiVer used to receive Data */
lbm_rcv_t *rcvr = NULL;		/* lbm ReCeiVer used to receive Results */
lbm_topic_t *rcvd_topic;
char topicd[LBM_MSG_MAX_TOPIC_LEN] = "Data.";
char *message;
struct timeval *mts = NULL;	/* Message Time Stamps */
int result_timer_id = -1;
int recovery_timer_id = -1;
int bghumms = 250;
int setupms = 1250;	/* time to allow receivers to SETUP for spike in MS */
unsigned long int recovms  = 3000;	/* RECOVery time in MilliSeconds */
int resultms = 2000;	/* RESULT   time in MilliSeconds */
char srcstr[200];	/* SouRCe STRing returned by LBM for srcd */
int nogapnext;		/* NO GAP if this sequence number arrives NEXT */
lbm_uint_t seqno0;	/* LBM SEQuence NO. of first message in the spike */
int dump = 0;		/* DUMP time stamps to a file */

void
dumpts(struct timeval * mts, struct timeval *starttv, int msgs)
{
	char fnbuf[HOST_NAME_MAX];	/* FileName BUFfer */
	int i;
	FILE *fp;
	long unsigned usec;	/* Bursts must fit in 71.5827 minutes */
#if (defined(_WIN32))

	snprintf(fnbuf, sizeof(fnbuf), "%s.XXXXX.txt", rolehost());
	_mktemp(fnbuf);
#else
	time_t now;
	struct tm *tm;

	time(&now);
	tm = localtime(&now);
	snprintf(fnbuf, sizeof(fnbuf), "%s.%d%02d%02d.%02d%02d%02d.txt",
		rolehost(), tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
#endif

	if ((fp=fopen(fnbuf, "w")) == NULL) {
		fprintf(stderr, "%s: couldn't open for writing\n", fnbuf);
		return;
	}
	for (i=0; i<msgs; i++) {
		/* Don't write records for messages never received */
		if (mts[i].tv_sec == 0)
			continue;
		/* Subtract out the start time */
		mts[i].tv_sec  -= starttv->tv_sec;
		mts[i].tv_usec -= starttv->tv_usec;
		normalize_tv(&mts[i]);
		usec = mts[i].tv_sec*1000000 + mts[i].tv_usec;
		fprintf(fp, "%lu %d\n", usec, i);
	}
	fclose(fp);
}

/*
 * Timer callback triggered to unblock source waiting on event queue.
 */
int handle_unblock_timer(lbm_context_t *ctx, const void *clientd)
{
	if (lbm_event_dispatch_unblock(evq) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_dispatch_unblock: %s\n", lbm_errmsg());
		exit(1);
	}
	return 0;
}

/* Timer callback triggered at end of recovery interval to send result */
int handle_recovery_timer(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *srcr = (lbm_src_t *)clientd;
	int incomplete = 0;
	double sec;
	double Kscale = 1024.0, Mscale = 1048576.0;
	char bytscale;
	lbm_ulong_t tbytes;		/* Transport BYTES */
	lbm_ulong_t tmsgs;		/* Transport MeSsaGeS */
	lbm_ulong_t lost_tmsgs;		/* LOST Transport MeSsaGeS */
	lbm_ulong_t lost_tbytes = 0;	/* LOST Transport BYTES */

	if (roleRcv && starttv.tv_sec!=0) {
		resprintf("%-14s ", rolehost());
		if (endtv.tv_sec == 0) {
			incomplete = 1;
			current_tv(&endtv);
		}
		endtv.tv_sec  -= starttv.tv_sec;
		endtv.tv_usec -= starttv.tv_usec;
		normalize_tv(&endtv);
		sec = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;
		print_bw(sec, received, received*msglen, (received+lost+blost)*msglen);
		resprintf(" received in %d ms\n", (int)(1000.0*sec));
		resprintf("%-14s ", "");
		if (lost+blost) {
			print_bw(sec, lost+blost, lost*msglen, (received+lost+blost)*msglen);
			resprintf(" unrecovered");
			if (incomplete) {
				resprintf(" (I)");
			}
		} else {
			if (lbm_rcv_retrieve_transport_stats(rcvd, 
				srcstr, &endstats) == LBM_FAILURE) {
				fprintf(stderr, "lbm_rcv_retrieve_transport_stats, source %s end: %s\n", srcstr, lbm_errmsg());
				exit(1);
			}
			if (endstats.type == LBM_TRANSPORT_STAT_LBTRM) {
				/*
				 * Approximate actual lost transport
				 * bytes as product of average transport
				 * message size and lost transport
				 * messages.  N.B. These are transport
				 * messages, not topic messages.
				 */
				tbytes = endstats.transport.lbtrm.bytes_rcved
				  - startstats.transport.lbtrm.bytes_rcved;
				tmsgs = endstats.transport.lbtrm.msgs_rcved
				  - startstats.transport.lbtrm.msgs_rcved;
				lost_tmsgs = endstats.transport.lbtrm.lost
				  - startstats.transport.lbtrm.lost;
				lost_tbytes = lost_tmsgs*(tbytes/tmsgs);
				if (lost_tbytes <= Mscale) {
					bytscale = 'K';
					lost_tbytes /= Kscale;
				} else {
					bytscale = 'M';
					lost_tbytes /= Mscale;
				}
				if (lost_tbytes) {
					resprintf("~%4d %cB transpt loss",
						lost_tbytes, bytscale);
				}
			}
			if (pastgap) {
				lbm_ulong_t pgbytes = pastgap * msglen;
				/* XXX gotta write a function to do this */
				if (pgbytes <= Mscale) {
					bytscale = 'K';
					pgbytes /= Kscale;
				} else {
					bytscale = 'M';
					pgbytes /= Mscale;
				}
				resprintf(", %d %cB past gap",
					pgbytes, bytscale);
			}
			resprintf(" avg. deliv. latency %d ms",
				(int)((totlate/(msgs-1))*1000.0));
		}
		resprintf("\n");
		starttv.tv_sec = 0;
	}

	if (dump)
		dumpts(mts, &starttv, msgs);

	if (lbm_src_send(srcr, result, restail+1, LBM_MSG_FLUSH) == LBM_FAILURE) {
		fprintf(stderr, "result lbm_src_send: %s\n", lbm_errmsg());
	}
	restail = 0;
	result[restail] = '\0';
	return 0;
}

/* Control source event handler (passed into lbm_src_create()) */
int handle_srcc_event(lbm_src_t *src, int event, void *ed, void *cd)
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
	case LBM_SRC_EVENT_WAKEUP:
		break;
	default:
		printf("Unknown source event %d\n", event);
		break;
	}
	return 0;
}

/*
 * Handle data receiver message.
 * Maintain statistics on received data.
 */
int handle_rcvd_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	int magic, i;
	long arg1, arg2;
	lbm_msg_fragment_info_t fraginfo;

	switch (msg->type) {
	case LBM_MSG_DATA:
		sscanf(msg->data, "%d %ld %ld", &magic, &arg1, &arg2);
		switch (magic) {
		case MAGIC_BGHUM:	/* Ignore background hum */
			bghum++;
			break;
		case MAGIC_SPIKE:
			/* if 1st rcvd msg from the spike */
			/* N.b. 1st rcvd may not be first seqno in spike */
			if (starttv.tv_sec == 0) {
				struct timeval srcclock, delivlate;

				current_tv(&starttv);
				srcclock.tv_sec  = arg1;
				srcclock.tv_usec = arg2;
				delivlate.tv_sec  = starttv.tv_sec
						  - srcclock.tv_sec;
				delivlate.tv_usec = starttv.tv_usec
						  - srcclock.tv_usec;
				normalize_tv(&delivlate);
				baselate = (double)delivlate.tv_sec
					 + (double)delivlate.tv_usec/1000000.0;

				strncpy(srcstr, msg->source, sizeof(srcstr));
				if (lbm_rcv_retrieve_transport_stats(rcvd, 
					srcstr, &startstats) == LBM_FAILURE) {
					fprintf(stderr, "lbm_rcv_retrieve_transport_stats (start): %s\n", lbm_errmsg());
					exit(1);
				}
				/*
				 * msg->sequence_number won't track messages
				 * sent if LBM fragments the message.
				 * lbm_msg_retrieve_fragment_info will succeed
				 * iff this message was fragmented.  Hence
				 * we assert that it must fail.
				 */
				if (lbm_msg_retrieve_fragment_info(msg, &fraginfo) != LBM_FAILURE) {
					fprintf(stderr, "Assertion failed: message arrived fragmented.  Use smaller messages.\n");
					exit(1);
				}
			} else {	/* Not 1st rcvd from the spike */
				struct timeval rcvclock, srcclock, delivlate;

				current_tv(&rcvclock);
				srcclock.tv_sec  = arg1;
				srcclock.tv_usec = arg2;
				delivlate.tv_sec  = rcvclock.tv_sec
						  - srcclock.tv_sec;
				delivlate.tv_usec = rcvclock.tv_usec
						  - srcclock.tv_usec;
				normalize_tv(&delivlate);
				totlate += (double)delivlate.tv_sec
					+  (double)delivlate.tv_usec/1000000.0;
				totlate -= baselate;
			}
			received++;
			i = msg->sequence_number-seqno0;
			if (0<=i && i<msgs) {
				current_tv(&mts[i++]);
				if (msg->sequence_number == nogapnext) {
					nogapnext++;
				} else {
					pastgap++;
				}
				while (mts[nogapnext-seqno0].tv_sec!=0 && i<msgs) {
					nogapnext++;
					i++;
				}
			}
			break;
		default:
			fprintf(stderr, "Assertion failed: unknown data topic magic number: %d\n", magic);
			break;
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		lost++;
		i = msg->sequence_number-seqno0;
		if (0<=i && i<msgs) {
			current_tv(&mts[i++]);
			if (nogapnext == msg->sequence_number) {
				nogapnext++;
			}
			while (mts[nogapnext-seqno0].tv_sec!=0 && i<msgs) {
				nogapnext++;
				i++;
			}
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		i = msg->sequence_number-seqno0;
		while (nogapnext <= msg->sequence_number) {
			nogapnext++;
			blost++;
			if (0<=i && i<msgs) {
				current_tv(&mts[i++]);
			}
		}
		break;
	case LBM_MSG_REQUEST:
	case LBM_MSG_BOS:
	case LBM_MSG_EOS:
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	if (endtv.tv_sec==0 && received+lost+blost==msgs) {
		current_tv(&endtv);
	}
	return 0;
}

/*
 * Handle control receiver message.
 * Print control.  Everything else is an error or ignored.
 */
int handle_rcvc_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	int magic, arg1, arg2, arg3, arg4;
	lbm_rcv_topic_attr_t * rattr;
	lbm_context_attr_t * ctx_attr;

	switch (msg->type) {
	case LBM_MSG_DATA:
		sscanf(msg->data, "%d %d %d %d %d", &magic, &arg1, &arg2, &arg3, &arg4);
		switch (magic) {
		case MAGIC_SETUP:
			msglen  = arg1;
			msgs    = arg2;
			recovms = arg3;
			seqno0  = arg4;
			nogapnext = seqno0;
			printf("Expecting %d messages of length %ld\n",
				msgs, msglen);
			if (starttv.tv_sec != 0) {
				starttv.tv_sec = 0;
				starttv.tv_usec = 0;
			}
			if (endtv.tv_sec != 0) {
				endtv.tv_sec = 0;
				endtv.tv_usec = 0;
			}
			received = 0;
			lost = 0;
			blost = 0;
			pastgap = 0;
			totlate = 0;

			if (mts != NULL) {
				free(mts);
			}
			mts = (struct timeval *) malloc(sizeof(struct timeval)*msgs);
			memset((char *)mts, 0, sizeof(struct timeval)*msgs);

			if (rcvd != NULL) lbm_rcv_delete(rcvd);
			if (ctxd != NULL) lbm_context_delete(ctxd);

			/* Retrieve current context settings */
			if (lbm_context_attr_create(&ctx_attr) == LBM_FAILURE) {
				fprintf(stderr, "lbm_context_attr_create: %s\n", lbm_errmsg());
				exit(1);
			}
			/* Set UDP buffer size option */
			if (udpbuf) {
				if (lbm_context_attr_setopt(ctx_attr, "transport_lbtrm_receiver_socket_buffer", &udpbuf, sizeof(udpbuf)) != 0) {
					fprintf(stderr, "lbm_context_str_setopt:transport_lbtrm_receiver_socket_buffer: %s\n", lbm_errmsg());
					exit(1);
				}
			}
			/* Create data context */
			if (lbm_context_create(&ctxd, ctx_attr, NULL, NULL) == LBM_FAILURE) {
				fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
				exit(1);
			}
			lbm_context_attr_delete(ctx_attr);

			/* Get ready to set receiver options we want */
			if (lbm_rcv_topic_attr_create(&rattr) == LBM_FAILURE) {
				fprintf(stderr, "lbm_rcv_topic_attr_create: %s\n", lbm_errmsg());
				exit(1);
			}
			/* Set NAK generation interval from recovms */
			if (lbm_rcv_topic_attr_setopt(rattr, "transport_lbtrm_nak_generation_interval", &recovms, sizeof(recovms)) == LBM_FAILURE) {
				fprintf(stderr, "lbm_rcv_setopt: %s\n", lbm_errmsg());
				exit(1);
			}
			/* Set arrival-order delivery if requested */
			if (ordered_delivery != -1) {
				if (lbm_rcv_topic_attr_setopt(rattr, "ordered_delivery", &ordered_delivery, sizeof(ordered_delivery)) == LBM_FAILURE) {
					fprintf(stderr, "lbm_rcv_setopt: %s\n", lbm_errmsg());
					exit(1);
				}
			}
			/* Lookup data topic */
			if (lbm_rcv_topic_lookup(&rcvd_topic, ctxd, topicd, rattr) == LBM_FAILURE) {
				fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
				exit(1);
			}
			lbm_rcv_topic_attr_delete(rattr);

			/* Create data receiver. */
			if (lbm_rcv_create(&rcvd, ctxd, rcvd_topic, handle_rcvd_msg, NULL, evqd) == LBM_FAILURE) {
				fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case MAGIC_DONE:
			if (starttv.tv_sec != 0) {
				printf("Allowing %ld ms for recovery before sending result\n", recovms);
				/*
				 * Schedule timer for end of recovery interval
				 * to trigger sending of result.
				 */
				if ((recovery_timer_id = lbm_schedule_timer(ctxcr, handle_recovery_timer, srcr, evq, recovms)) == -1) {
					fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
					exit(1);
				}
			}
			break;
		default:
			printf("Unknown control message %d\n", magic);
			break;
		}
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
	case LBM_MSG_REQUEST:
	case LBM_MSG_EOS:
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	return 0;
}

/*
 * Handle result receiver message.
 * Print result.  Everything else is an error or ignored.
 */
int handle_rcvr_msg(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	switch (msg->type) {
	case LBM_MSG_DATA:
		printf("%s", msg->data);
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS:
		printf("handle_rcvr_msg: unrecoverable loss\n");
		break;
	case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
		printf("handle_rcvr_msg: unrecoverable burst loss\n");
		break;
	case LBM_MSG_REQUEST:
	case LBM_MSG_EOS:
		break;
	case LBM_MSG_NO_SOURCE_NOTIFICATION:
		/* This "should never happen" since we created a source too */
		printf("handle_rcvr_msg: no source\n");
		break;
	default:
		printf("Unknown lbm_msg_t type %x [%s][%s]\n", msg->type, msg->topic_name, msg->source);
		break;
	}
	return 0;
}

int main(int argc, char **argv)
{
	char *topicsfx = "lbmspike";
	lbm_topic_t *srcc_topic, *srcd_topic, *srcr_topic;
	lbm_topic_t *rcvc_topic, *rcvr_topic;
	char topicc[LBM_MSG_MAX_TOPIC_LEN] = "Control.";
	char topicr[LBM_MSG_MAX_TOPIC_LEN] = "Result.";
	int c, i, sendflag = 0, errflag = 0, tcplb = 0;
	int eventq = 0;
	lbm_context_attr_t * ctx_attr;
	lbm_src_topic_attr_t * tattr;
	lbm_uint64_t rm_rate = 0, rm_retrans = 0;
	char rm_protocol = 'M';
	unsigned long sent = 0, blocked = 0;
	double sec = 0.0;
	char cmsg[CNTL_MSG_MAX];
	char dmsg[200];
printf("A");

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

	while ((c = getopt(argc, argv, "bB:c:dhLl:M:o:qr:Ru:v:")) != EOF) {
		switch (c) {
		case 'B':
			bghumms = atoi(optarg);
			break;
		case 'c':
			/* Initialize configuration parameters from a file. */
			if (lbm_config(optarg) == LBM_FAILURE) {
				fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
				exit(1);
			}
			break;
		case 'd':
			dump = 1;
			break;
		case 'h':
			fprintf(stderr, "%s\n%s\n%s\n%s",
				argv[0], lbm_version(), purpose, usage);
			exit(0);
		case 'L':
			tcplb = 1;
			break;
		case 'l':
			msglen = atoi(optarg);
			break;
		case 'M':
			msgs = atoi(optarg);
			break;
		case 'n':
			sendflag |= LBM_SRC_NONBLOCK;
			break;
		case 'o':
			ordered_delivery = atoi(optarg);
			break;
		case 'q':
			eventq = 1;
			break;
		case 'r':
			errflag += parse_rate(optarg, &rm_protocol, &rm_rate, &rm_retrans);
 			break;
		case 'R':
			roleRcv = 1;
			break;
		case 'u':
			/* XXX would be nice to allow K, M suffix here */
			udpbuf = atoi(optarg);
			break;
		case 'v':
			recovms = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Unrecognized option: %c\n", c);
			errflag++;
			break;
		}
	}
	if (optind == argc-1) {
		topicsfx = argv[optind];
	}

	/*
	 * Syntax checks.
	 */
	if (optind <  argc-1) {
 		errflag++;
	}
	if (roleRcv && ((sendflag&LBM_SRC_NONBLOCK)!=0 || tcplb)) {
		fprintf(stderr, "Source options present in receiver mode\n");
 		errflag++;
	}
	if (!roleRcv && (ordered_delivery==0 || eventq)) {
		fprintf(stderr, "Receiver options present in source mode\n");
 		errflag++;
	}
	if (errflag) {
 		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	/*
	 * Sensability checks.
	 */
	if (setupms/bghumms < 4) {
		fprintf(stderr, "At least 4 background hum messages should be sent in the setup interval.\n");
		exit(1);
	}
	if (recovms/bghumms < 10) {
		fprintf(stderr, "At least 10 background hum messages should be sent in the recovery interval.\n");
		exit(1);
	}
	strncat(topicc, topicsfx, sizeof(topicc));
	strncat(topicd, topicsfx, sizeof(topicd));
	strncat(topicr, topicsfx, sizeof(topicr));
	
	/* Create control and result context */
	if (lbm_context_create(&ctxcr, NULL, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Create an event queue. */
	if (lbm_event_queue_create(&evq, NULL, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_event_queue_create: %s\n", lbm_errmsg());
		exit(1);
	}
	evqd = (eventq) ? evq : NULL;

	/* XXX sure could use a source & receiver creation function here */
	/* Allocate result source topic */
	if (lbm_src_topic_alloc(&srcr_topic, ctxcr, topicr, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Create result source using topic allocated above */
	if (lbm_src_create(&srcr, ctxcr, srcr_topic, NULL, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
		exit(1);
	}

	/* Lookup result topic */
	if (lbm_rcv_topic_lookup(&rcvr_topic, ctxcr, topicr, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
		exit(1);
	}
	/* Create result receiver. */
	if (lbm_rcv_create(&rcvr, ctxcr, rcvr_topic, handle_rcvr_msg, NULL, NULL) == LBM_FAILURE) {
		fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
		exit(1);
	}

	if (roleRcv) {	/* Spike receiver role */
		/* Lookup control topic */
		if (lbm_rcv_topic_lookup(&rcvc_topic, ctxcr, topicc, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_topic_lookup: %s\n", lbm_errmsg());
			exit(1);
		}
		/* Create control receiver. */
		if (lbm_rcv_create(&rcvc, ctxcr, rcvc_topic, handle_rcvc_msg, NULL, evq) == LBM_FAILURE) {
			fprintf(stderr, "lbm_rcv_create: %s\n", lbm_errmsg());
			exit(1);
		}
		while (1) {
			if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
				fprintf(stderr, "lbm_event_dispatch returned error.\n");
				exit(1);
			}
		}
		/* NOTREACHED */

	} else {	/* Spike source role */
		/* Allocate control source topic */
		if (lbm_src_topic_alloc(&srcc_topic, ctxcr, topicc, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		/* Create control source using topic allocated above */
		if (lbm_src_create(&srcc, ctxcr, srcc_topic, handle_srcc_event, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
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
		/* Create data context (passing in context attributes) */
		if (lbm_context_create(&ctxd, ctx_attr, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_context_create: %s\n", lbm_errmsg());
			exit(1);
		}
		lbm_context_attr_delete(ctx_attr);

		if (tcplb) {	/* Set TCP-LB behavior */
			if (lbm_src_topic_attr_str_setopt(tattr, "transport_tcp_multiple_receiver_behavior", "Bounded_Latency") != 0) {
				fprintf(stderr, "lbm_src_topic_str_setopt:transport: %s\n", lbm_errmsg());
				exit(1);
			}
		}
		/* Allocate data source topic */
		if (lbm_src_topic_alloc(&srcd_topic, ctxd, topicd, tattr) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_topic_alloc: %s\n", lbm_errmsg());
			exit(1);
		}
		lbm_src_topic_attr_delete(tattr);

		/* Create data source using topic allocated above */
		if (lbm_src_create(&srcd, ctxd, srcd_topic, NULL, NULL, NULL) == LBM_FAILURE) {
			fprintf(stderr, "lbm_src_create: %s\n", lbm_errmsg());
			exit(1);
		}

		SLEEP_SEC(1);	/* Wait for source discovery */

		snprintf(cmsg, sizeof(cmsg), "%d %lu %d %lu %d",
			MAGIC_SETUP, msglen, msgs, recovms, setupms/bghumms);
		if (lbm_src_send(srcc, cmsg, sizeof(cmsg), LBM_MSG_FLUSH) == LBM_FAILURE) {
			fprintf(stderr, "control setup lbm_src_send: %s\n", lbm_errmsg());
			exit(1);
		}

		message = malloc(msglen);
		if (message == NULL) {
			fprintf(stderr, "can't allocate %lu byte message buffer.\n", msglen);
			exit(1);
		}
		memset(message, 0, msglen);

		/* Send pre-spike bghum */
		snprintf(dmsg, sizeof(dmsg), "%d 0 0", MAGIC_BGHUM);
		if (strlen(dmsg)+1 > msglen) {
			fprintf(stderr, "message length too small for bghum payload\n");
			exit(1);
		}
		strncpy(message, dmsg, msglen);
		for (i=0; i<setupms/bghumms; i++) {
			if (lbm_src_send(srcd, message, strlen(message)+1, LBM_MSG_FLUSH) == LBM_FAILURE) {
				fprintf(stderr, "control setup lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
			SLEEP_MSEC(bghumms);
		}

		mts = (struct timeval *) malloc(sizeof(struct timeval)*msgs);
		memset((char *)mts, 0, sizeof(struct timeval)*msgs);

		/* Send message spike */
		current_tv(&starttv);
		for (i=0; i<msgs; i++) {
			if (i == msgs-1)
				sendflag |= LBM_MSG_FLUSH;
			current_tv(&mts[i]);
			snprintf(dmsg, sizeof(dmsg), "%d %ld %ld",
				MAGIC_SPIKE, mts[i].tv_sec, mts[i].tv_usec);
			if (strlen(dmsg)+1 > msglen) {
				fprintf(stderr, "message length too small for payload\n");
				exit(1);
			}
			strncpy(message, dmsg, msglen);
			if (lbm_src_send(srcd, message, msglen, sendflag) == LBM_FAILURE) {
				if (lbm_errnum() == LBM_EWOULDBLOCK) {
					blocked++;
				} else {
					fprintf(stderr, "lbm_src_send: %s\n", lbm_errmsg());
					exit(1);
				}
			} else {
				sent++;
			}
		}
		
		current_tv(&endtv);
		endtv.tv_sec  -= starttv.tv_sec;
		endtv.tv_usec -= starttv.tv_usec;
		normalize_tv(&endtv);
		sec = (double)endtv.tv_sec + (double)endtv.tv_usec / 1000000.0;

		snprintf(cmsg, sizeof(cmsg), "%d %d %d %d",
			MAGIC_DONE, 0, 0, 0);
		if (lbm_src_send(srcc, cmsg, sizeof(cmsg), LBM_MSG_FLUSH) == LBM_FAILURE) {
			fprintf(stderr, "control done lbm_src_send: %s\n", lbm_errmsg());
		}

		resprintf("%-14s ", rolehost());
		print_bw(sec, sent,       sent*msglen, (sent+blocked)*msglen);
		resprintf(" sent     in %d ms\n", (int)(1000.0*sec));
		if ((sendflag&LBM_SRC_NONBLOCK) != 0) {
			resprintf("%-14s ", "");
			print_bw(sec, blocked, blocked*msglen, (sent+blocked)*msglen);
			resprintf(" blocked\n");
		}

		/*
		 * Schedule timer for end of recovery interval to
		 * send result.
		 */
		if ((recovery_timer_id = lbm_schedule_timer(ctxcr, handle_recovery_timer, srcr, evq, recovms)) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}

		/* Schedule timer for end of result interval to trigger exit. */
		if ((result_timer_id = lbm_schedule_timer(ctxcr, handle_unblock_timer, NULL, evq, recovms+resultms)) == -1) {
			fprintf(stderr, "lbm_schedule_timer: %s\n", lbm_errmsg());
			exit(1);
		}

		printf("Done sending spike.  Waiting for recovery and results . . .\n");

		/* Send post-spike bghum for recovery interval */
		snprintf(dmsg, sizeof(dmsg), "%d 0 0", MAGIC_BGHUM);
		if (strlen(dmsg)+1 > msglen) {
			fprintf(stderr, "message length too small for bghum payload\n");
			exit(1);
		}
		strncpy(message, dmsg, msglen);
		for (i=0; i<recovms/bghumms; i++) {
			if (lbm_src_send(srcd, message, strlen(message)+1, LBM_MSG_FLUSH) == LBM_FAILURE) {
				fprintf(stderr, "control setup lbm_src_send: %s\n", lbm_errmsg());
				exit(1);
			}
			SLEEP_MSEC(bghumms);
		}

		/* Will unblock at end of result interval */
		if (lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK) == LBM_FAILURE) {
			fprintf(stderr, "lbm_event_dispatch returned error.\n");
			exit(1);
		}
		/* XXX this is end point of spike repeat loop */
	}

	/* Deallocate sources, receivers, and LBM context */
	if (srcc  != NULL) lbm_src_delete(srcc);
	if (srcd  != NULL) lbm_src_delete(srcd);
	if (srcr  != NULL) lbm_src_delete(srcr);
	if (rcvc  != NULL) lbm_rcv_delete(rcvc);
	if (rcvd  != NULL) lbm_rcv_delete(rcvd);
	if (rcvr  != NULL) lbm_rcv_delete(rcvr);
	if (ctxcr != NULL) lbm_context_delete(ctxcr);
	if (ctxd  != NULL) lbm_context_delete(ctxd);
	if (evq   != NULL) lbm_event_queue_delete(evq);
	if (mts   != NULL) free(mts);

	return 0;
}

