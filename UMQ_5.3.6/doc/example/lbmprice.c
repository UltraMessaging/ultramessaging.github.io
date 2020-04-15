/*
"lbmprice.c: Simulated price source and receiver for demonstration.

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
/*
 * XXX price always moves toward low limit
 * XXX should this use lbm_cancel_timer_ex()?
 *
 * Purpose
 *  Binary
 *	The pre-compiled binary of lbmprice is meant to be a functional
 *	demonstration of how a price source and receiver might
 *	be built with LBM.  Simulated prices are generated at a
 *	rate that makes it possible to observe the operation of
 *	various LBM features that are useful in distributed
 *	applications.  Command line flags allow key LBM operating
 *	parameters to be changed without using configuration files.
 *	Output is generated for all key events to illustrate the
 *	operation of LBM and a price distribution application.
 *	Hot failover can be easily demonstrated.
 *	It doubles as a "multicast beacon" that is handy for
 *	troubleshooting multicast connectivity problems.
 *	Unlike most of the rest of the example programs, the focus
 *	is much more on functional testing than performance testing.
 *  Source
 *	Programmers can use this source to get ideas
 *	for how various LBM features could be used in coding
 *	distributed applications.
 *
 * Application Scenario & Terminology:
 *
 *	A large set of receivers need information from a source.  The
 *	source needs to know when new receivers are discovered.  It
 *	also needs to know when receivers die or if communication
 *	with a receiver is lost for any reason.
 *
 *	Some information from the source changes rapidly, perhaps
 *	sub-second.  We'll call this the "last price" or just
 *	"price."  Each new price (or "update") obsoletes the previous
 *	last price.
 *
 *	The last price is part of a larger data structure containing
 *	information that updates more slowly.  We'll call this the
 *	pricebook or just the "book."  (In some applications it's
 *	called the "image.")
 *
 *	The master copy of the book is maintained on the source,
 *	yet the receivers require a local copy of the book to do
 *	their work.
 *
 *	An updated price may cause parts of the book to change,
 *	according to a simple set of rules.  For example, a new
 *	price above the current record high sets a new record high.
 *	Mathematically, high = max(high, last).
 *
 *	The book is significantly larger than a price update, hence
 *	it is desirable to send the book only when necessary.
 *
 *	If a receiver knows the source's rules for updating the
 *	book as new prices are received, then a copy of the current
 *	book may be maintained at a receiver as long as it has
 *	received a book and an unbroken stream of price updates.
 *	Hence it is only necessary to send the book when a new
 *	receiver joins the group or when an existing receiver has
 *	had a break in the price stream.
 *
 *	Similar problems occur with database updates, maintaining
 *	cache coherence, and in many other distributed applications.
 *	Related applications include market data feed handlers and
 *	streaming caches.
 *
 *	When applied to a list of prices, the min() and max()
 *	functions used in maintaining the low and high records in
 *	the book have an interesting property:  their value is
 *	independent of the order of the prices in the list.  Hence
 *	the high and low records can be maintained accurately even
 *	if the price updates arrive in an order different from the
 *	order in which they were sent.
 *
 *	If ordered delivery is required, LBM must add latency to
 *	reorder messages whenever they arrive out of order.  This
 *	latency can be eliminated by selecting arrival-order delivery.
 *	Correct low and high values will be maintained whenever all
 *	messages sent by the source have been received.  However,
 *	low and high records will pass through periods of uncertainty
 *	while there are gaps in the sequence of messages that have
 *	been received.
 *
 *  Programs
 *   *	There are 4 related programs in this source file: a
 *	price source, a price receiver, a hot failover price relay,
 *	and a hot failover price receiver.  Command line flags are
 *	used to select among the programs.  These may be combined
 *	in many interesting ways.  (Lines below show data flow.)
 *   *	Simple operation with a price source talking to a price receiver:
 *	lbmprice -s  --->  lbmprice
 *   *	A price source talking through 2 hot failover price
 *	relays to a hot failover price receiver:
 *	                 /---  lbmprice -H -s  ---\
 *	lbmprice -s  ---                            --->  lbmprice -H
 *	                 \---  lbmprice -H -s  ---/
 *	Note that in the hot failover case, book requests go back to
 *	the source, not to the hot failover relays.
 *
 * Things to Do and See with the Pre-Compiled Binary:
 *
 *  Basic Operation
 *   *	Run lbmprice -s and lbmprice.  Note that the latest price is
 *	always printed as it arrives, even if the current book state
 *	is invalid ("--.---" for low and high prices).  After the
 *	initial book has been received, low and high are updated as
 *	price updates are received.  Note the sequence numbers in the
 *	leftmost column monotonically increasing without gaps.
 *  Session Management (Who's "Connected")
 *   *	Run lbmprice without lbmprice -s running on the network.
 *	Note warning messages that no price sources are present.
 *   *  Run lbmprice -s and lbmprice.  Kill lbmprice and note how
 *	lbmprice -s notices the death after a short delay.
 *   *	Run lbmprice -s and lbmprice.  Disconnect the machine running
 *	lbmprice from the network.  Lbmprice -s still notices.
 *  Book Request Management
 *   *  Run lbmprice -s and lbmprice.  Start a 2nd lbmprice after source
 *	has returned to state SRC_READY.  Note that initial book request
 *	is promptly served.
 *   *  Run lbmprice -s and lbmprice.  Start a 2nd lbmprice while source
 *	is in state RATE_LIMIT.  Note that service of 2nd book request
 *	delayed till source exits RATE_LIMIT state.
 *   *  Run lbmprice -s and lbmprice.  Start a 2nd lbmprice while source
 *	is in state SQUELCH.  Note that 1st book request is serviced,
 *	2nd is squelched, but is serviced after price receiver repeats
 *	the request.
 *   *	Run 2 copies of lbmprice, then run lbmprice -s.  Note that both
 *	copies of lbmprice will discover the new source at about the
 *	same time, but one is likely to send its book request before
 *	the other due to an intentional random delay.  The 2nd lbmprice
 *	receives a book without even having to send a request.
 *  Latency and Loss Testing
 *   *	Run lbmprice -s and lbmprice in two windows on the same machine.
 *	Note the latency.
 *   *	Run lbmprice -s and lbmprice on two different machines.  Note the
 *	reported latency.  It contains any clock offset between machines.
 *	(You may need to set the ttl with -t if crossing IP network boundaries.)
 *   *	Note: Following tests show the impact of loss on latency.  It will be
 *	easiest to see the effect of loss if latency is consistent and small.
 *	So it may be easiest to understand loss test results if a single
 *	machine (and hence a single clock) is used for both source and receiver.
 *   *	Run lbmprice -l 5 to simulate loss.  Note occasional latency caused
 *	by loss repair.  Average and maximum latencies are printed.
 *   *	Run lbmprice -l 20 to simulate even more loss.  Note how latency
 *	may accumulate across several prices awaiting delivery following
 *	repair.
 *   *	Run lbmprice -l 20 -n 300 to simulate loss with a small latency
 *	budget and note how unrecoverable loss invalidates the book.
 *      Note that a book request is logged at the source.  Note that
 *	high and low aren't printed while book is invalid, but price
 *	continues to update.  Also note that a source won't send the book
 *	too frequently even with receivers having severe loss ("crybaby
 *	receivers").
 *  Arrival Order, Latency, and Uncertainty
 *   *	Run lbmprice -l 20 -o 0 to simulate loss with arrival-order
 *	delivery.  Note gaps in sequence numbers.  Note that some
 *	prices will be completely ignored since they arrive after
 *	even newer prices have already arrived.  Note that book
 *	state can now be UNCERTAIN (high and low marked with "*"), but
 *	returns to VALID after missing messages have been delivered.
 *   *  Run lbmprice -l 20 -o 1 and lbmprice -l 20 -o 0 in two windows
 *	on the same machine.  Note much lower average and maximum latency
 *	possible with arrival-order delivery.
 * Hot Failover
 *   *	Run lbmprice -s, 2x lbmprice -H -s, and lbmprice -H to set up a
 *	price source, 2 HF relays, and an HF receiver.  Kill either
 *	relay and note that there is no delay in received prices.
 *   *	Run lbmprice -s, lbmprice -H -s -l 20 -o 0, and lbmprice -H to
 *	set up a lossy HF relay forwarding in arrival order.  Add a 2nd
 *	lbmprice -H -s -l 20 -o 0 and note the sharp reduction in latency
 *	because the receiver picks the lowest latency source for each message.
 *
 * 
 * Things to See in the Source:
 *  Session Management (Who's "Connected")
 *   *	Note how LBM's no source notification events are used to provide
 *	notice when no source is present.  (Search for "MSG_NO_SOURCE".)
 *   *	Note how new source notification is used to find new receivers.
 *	(Search for "new_source_action" and "src_notify".)
 *   *	Note how LBM's end of session events are used to provide notice
 *	to the source when a price receiver goes away for any reason.
 *	(Search for "MSG_EOS".)
 *   *	Note how session message timers and session activity timeouts have
 *	been decreased from their defaults to provide more prompt EOS
 *	notification.  (Search for "sm_maximum" and "activity_timeout".)
 *  Book Request Management
 *   *	Note price source book state machine that governs when a book
 *	is sent.  (Search "src_book_sm".)
 *   *  Note price receiver book request state machine that governs when
 *	a book request is sent.  (Search "rcv_req_sm".)
 *  Latency and Loss Testing
 *   *	Note how random induced receiver loss and a decreased NAK generation
 *	interval can be used to cause unrecoverable loss (very handy for
 *	testing error recovery).  (Search for "LBTRM_LOSS_RATE" and
 *	"nak_generation".)
 *  Arrival Order Delivery
 *   *	Note how message delivery can be "ordered" or "arrival order."
 *	(Search for "ordered_delivery".)  Most of the logic for dealing
 *	out-of-order arrival is in data_msg_action().
 *  Hot Failover
 *   *	Note how a hot failover receiver may be created in hf_rcv_main().
 *	Note creation of hot failover source.  (Search for "hf_relay_main"
 *	and "data_relay_event_action".)
 *
 *
 * Design Choices and Rationale:
 *   Overall Tradeoffs
 *	Scalability to a large number of receivers and stable operation of
 *	the source under any book request load are generally the top
 *	priorities.  The core belief is that receivers that're operating
 *	normally must be serviced before receivers that're having trouble.
 *	Receivers having trouble should not be able to cause trouble for other
 *	receivers.
 *   Transport Protocol
 *	The scenario calls for a large number of receivers and source
 *	notification when communication is lost with a receiver.
 *	The multicast addressing model of LBT-RM allows efficient
 *	price delivery to a large number of receivers.  LBT-RM is
 *	also used by price receivers to send book requests toward
 *	the source.  The session message and activity timer features of
 *	LBT-RM allow a source to know when communication with a receiver
 *	is lost.  The arrival order delivery option of LBT-RM allows
 *	for the lowest-possible latency under adverse conditions.
 *	Note that, unlike TCP, LBT-RM is connectionless.  The
 *	session messages are used as a heartbeat, the absence of which
 *	indicates a loss of communication.  LBT-RU would be the choice
 *	when multicast connectivity between price sources and receivers
 *	is not possible.  TCP was rejected because it does not scale
 *	well to large numbers of receivers, it does not allow
 *	arrival-order delivery, and does not consistently allow detection
 *	of disconnected receivers.
 *  Topics for Prices and Books
 *	LBM sequences messages within a topic but not across topics.
 *	If prices and books were sent on separate topics, then the
 *	receiver would have to impose ordering across topics to
 *	maintain the consistency of its local copy of the book.
 *	For the sake of receiver simplicity, we've chosen a single
 *	topic for both prices and books.  This may also be the right
 *	design for production if the size of the book could be kept
 *	small or perhaps if it could be delivered in pieces so as not
 *	to introduce too much latency by its transmission.  For larger
 *	books, production systems may want a separate topic for
 *	sending the book while adding the receiver complexity needed
 *	to order it within the stream of prices updates.
 *  Request/Response (Why Not Use It?)
 *	Books may benefit more than one receiver.  Responses uses TCP
 *	so they can benefit one receiver at most.  What if a group of
 *	receivers are suddenly reconnected after a network break is restored?
 *	Ideally, the book would be sent once to service them all.
 *  Event Queues
 *	LBM event queues are used because they allow application callbacks
 *	to block while still allowing processing to continue on the LBM
 *	context thread.  Since this is an example, we often call printf()
 *	and do other things from callbacks that might block.  A real-world
 *	price source and receiver could perhaps be coded to avoid blocking
 *	behavior on callbacks thereby allowing event queues to be avoided
 *	for even lower latency.
 *
 *
 * Ways different from real price source
 *   *	Higher rate, not random, better error handling, tune timers, multiple
 *	symbols
 *
 * Unsupported
 *   *	More than one price source at a time.  Of course, many HF sources
 *	are ok.
 *
 * Under Consideration
 *   *	Measure source startup latency
 *	Time between when a source starts and when a long-running receiver
 *	receives the first message it sends.
 *   *	Measure receiver startup latency
 *	Time between when a receiver starts and when it receives the first
 *	packet from a long-running source.
 *
 *   *	Price receiver startup with long-running price source
 *	Source will be advertising, but receivers cache will be empty
 *	since it just started.  Receiver generates a topic query
 *	XXX finish this when you know more.
 */

#ifdef __VOS__
#define _POSIX_C_SOURCE 200112L 
#include <sys/time.h>
#endif

char purpose[] = "Purpose: Simulated price source and receiver for demonstration.";
char usage[] =
"Usage: -s [-h] [-c filename]\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -h = help\n"
"       -H = act has Hot Failover relay for a price source\n"
"       -l pct = induce random receiver loss of pct percent\n"
"       -n ms = set receiver NAK generation interval to ms milliseconds\n"
"       -s = act as a price source (acts as a receiver by default)\n"
"       -t ttl = set resolver (and multicast source) ttl to ttl\n"
"       -v = be verbose\n"
"Usage: [-h] [-c filename]\n"
"       -c filename = read config file\n"
"       -h = help\n"
"       -H = use Hot Failover receiver\n"
"       -l pct = induce random receiver loss of pct percent, print max latency\n"
"       -n ms = set receiver NAK generation interval to ms milliseconds\n"
"       -o mode = set ordered delivery mode (1=ordered, 0=arrival order)\n"
"       -t ttl = set resolver (and multicast source) ttl to ttl\n"
"       -v = be verbose\n"
;

static const char *rcsid_example_lbmprice = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmprice.c#2 $";

#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
	#include <sys/time.h>
#endif

#include <stdarg.h>

#if defined(_MSC_VER)
	#include <winsock2.h>
	#include <stdlib.h>
	#include <sys/timeb.h>
	#define WIN32_HIGHRES_TIME
#else
	#include <stdlib.h>
	#include <unistd.h>
	#include <signal.h>
#endif

#include <lbm/lbm.h>
#include <float.h>
#include <lbm/lbmsdm.h>
#include "lbm-example-util.h"

#if defined(_WIN32)
extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
#endif /* _WIN32 */


/* --- High-precision timestamp manipulation --- */


/* we need to identify the host OS/timer resolution for latency calculations */
/* *nix will always use wall time, as will XP when not using the high        */
/* resolution timer; the XP high resolution timer will report relative       */
/* time                                                                      */
#if defined(_WIN32) && defined(WIN32_HIGHRES_TIME)
const uint8_t wall_time = '0' ;
#else
const uint8_t wall_time = '1' ;
#endif /* _WIN32 && WIN32_HIGHRES_TIME */

/* if functioning as a receiver, we need to know whether our source's time is*/
/* relative or not                                                           */
uint8_t remote_wall_time;


/* --- LBM Auxiliary functions --- */

/*
 * Create an LBM context with conditional options set by binary value.
 */
int
lbmaux_context_cond_setopt_create(lbm_context_t **ctxp,
								  lbm_daemon_event_cb_proc proc, void *clientd, ...)
{
	va_list ap;
	lbm_context_attr_t * ctx_attr;	/* context attributes */
	int cond;
	const char *optname;
	void *optval;
	size_t optlen;
	int err;			/* return status of lbm functions */

	va_start(ap, clientd);
	/* XXX reword comments about _attr_ to reflect new config terminology */
	/* Retrieve current context settings */
	err = lbm_context_attr_create(&ctx_attr);
	if (err == LBM_FAILURE)	return (err);
	while ((cond=va_arg(ap, int)) != -1)
	{
		optname = va_arg(ap, char *);
		optval  = va_arg(ap, void *);
		optlen  = va_arg(ap, size_t);
		if (cond)
		{
			err= lbm_context_attr_setopt(ctx_attr, optname, optval, optlen);
			if (err == LBM_FAILURE)	return (err);
		}
	}
	va_end(ap);
	err = lbm_context_create(ctxp, ctx_attr, proc, clientd);
	lbm_context_attr_delete(ctx_attr);
	return (err);
}

/*
 * Create an LBM source with options set by string.
 */
int
lbmaux_src_str_setopt_create(lbm_src_t **srcp, lbm_context_t *ctx,
							 const char *symbol, 
							 lbm_src_cb_proc proc, void *clientd,
							 lbm_event_queue_t *evq, ...)
{
	va_list ap;
	lbm_src_topic_attr_t * tattr;	/* Attributes for source */
	lbm_topic_t *topic;		/* Topic for source */
	const char *optname;
	const char *optval;
	int err;			/* return status of lbm functions */

	va_start(ap, evq);
	/* XXX reword comments about _attr_ to reflect new config terminology */
	/* Retrieve current context settings */
	err = lbm_src_topic_attr_create(&tattr);
	if (err == LBM_FAILURE)	return (err);
	while ((optname=va_arg(ap, const char *)) != NULL)
	{
		optval  = va_arg(ap, const char *);
		err= lbm_src_topic_attr_str_setopt(tattr, optname, optval);
		if (err == LBM_FAILURE)
		{
			lbm_src_topic_attr_delete(tattr);
			return (err);
		}
	}
	va_end(ap);
	err = lbm_src_topic_alloc(&topic, ctx, symbol, tattr);
	if (err == LBM_FAILURE)
	{
		lbm_src_topic_attr_delete(tattr);
		return (err);
	}
	err = lbm_src_create(srcp, ctx, topic, proc, clientd, evq);
	lbm_src_topic_attr_delete(tattr);
	return (err);
}

/*
 * Create and LBM receiver with conditional options set by string.
 */
int
lbmaux_rcv_cond_str_setopt_create(lbm_rcv_t **rcvp, lbm_context_t *ctx,
								  const char *symbol, 
								  lbm_rcv_cb_proc proc, void *clientd,
								  lbm_event_queue_t *evq, ...)
{
	va_list ap;
	lbm_rcv_topic_attr_t * tattr;	/* Attributes for receiver */
	lbm_topic_t *topic;		/* Topic for receiver */
	int cond;
	const char *optname;
	const char *optval;
	int err;			/* Return status of lbm functions */

	va_start(ap, evq);
	/* XXX reword comments about _attr_ to reflect new config terminology */
	/* Retrieve current context settings */
	err = lbm_rcv_topic_attr_create(&tattr);
	if (err == LBM_FAILURE)	return (err);
	while ((cond=va_arg(ap, int)) != -1)
	{
		optname = va_arg(ap, const char *);
		optval  = va_arg(ap, const char *);
		if (cond)
		{
			err= lbm_rcv_topic_attr_str_setopt(tattr, optname, optval);
			if (err == LBM_FAILURE)
			{
				lbm_rcv_topic_attr_delete(tattr);
				return (err);
			}
		}
	}
	va_end(ap);
	err = lbm_rcv_topic_lookup(&topic, ctx, symbol, tattr);
	if (err == LBM_FAILURE)
	{
		lbm_rcv_topic_attr_delete(tattr);
		return (err);
	}
	err = lbm_rcv_create(rcvp, ctx, topic, proc, clientd, evq);
	lbm_rcv_topic_attr_delete(tattr);
	return (err);
}

/* --- Price, book, and other global variables & types --- */

int price_timer_id = -1;
#define	PRICE_MS	500	/* milliseconds between prices generated */
int verbose = 0;

/* we're going slow, don't need big buffer, this suppresses warnings */
unsigned long int udpbuf = 128*1024;

typedef double price_t;
typedef struct
{
	price_t     low;   /* Lowest  trade price since the open */
	price_t     high;  /* Highest trade price since the open */
	price_t     last;  /* Last trade price */
	struct timeval  ts;	   /* Source's time stamp when last price was sent */
	/* Following fields are used only on receiver to trace book state */
	enum        book_states
	{
		VALID, INVALID, UNCERTAIN
	} state;
	lbm_uint_t      newest_book; /* Sequence number for newest book received */
	lbm_uint_t      leading;	 /* Leading  edge of uncertainty window */
	/* Trailing is valid iff state == UNCERTAIN */
	lbm_uint_t      trailing;	 /* Trailing edge of uncertainty window */
	/* Following fields are used only on receiver to keep statistics */
	int         updates;	/* Number of price & book updates received */
	double      total;	/* Total latency */
	double      max;	/* Maximum latency */
} book_t;
book_t  rcvd_book;	/* received book */
book_t  src_book;	/* source book */

#define	DATA_TOPIC	"PriceBook.Data"	/* Prices & books */
#define	DATA_HF_TOPIC	"PriceBook.HF.Data"	/* Hot Failover prices&books */
#define	REQ_TOPIC	"PriceBook.Request"	/* Book requests */

#define	PR_OPEN		50.0		/* opening price */
#define	PR_LIMIT_HI	99.0		/* high limit on random price gen */
#define	PR_LIMIT_LO	 1.0		/* low  limit on random price gen */

/* --- Price book manipulation shared between price source and receiver */

int	/* Return true if new record high or low was set */
book_hi_low_set(book_t *bookp, price_t pr)
{
	int new_record = 0;

	if (pr > bookp->high)
	{
		bookp->high = pr;
		new_record = 1;
	}
	if (pr < bookp->low)
	{
		bookp->low  = pr;
		new_record = 1;
	}
	return (new_record);
}

/* --- Source price book manipulation */

void
src_book_init(book_t *bookp)
{
	bookp->low  = PR_OPEN*0.9;
	bookp->last = PR_OPEN;
	bookp->high = PR_OPEN*1.1;
}

void
price_update(book_t *bookp)
{
	/* random price change on the interval [2,-2] by eighths */
#if defined(_WIN32)
	bookp->last += ((price_t)(rand()%31))/8.0-2.0;
#else
	bookp->last += ((price_t)(random()%31))/8.0-2.0;
#endif

	if (bookp->last > PR_LIMIT_HI)
		bookp->last = PR_LIMIT_HI;
	if (bookp->last < PR_LIMIT_LO)
		bookp->last = PR_LIMIT_LO;
	(void) book_hi_low_set(bookp, bookp->last);
}

lbmsdm_msg_t *
book_to_msg(book_t *bookp)
{
	lbmsdm_msg_t * msg;
	struct timeval tv;
	char msgtype;

	current_tv(&tv);
	if (lbmsdm_msg_create(&msg) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_create (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		return (NULL);
	}

	msgtype = 'B';
	if (lbmsdm_msg_add_uint8(msg, "msgtype", (uint8_t) 'B') == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_uint8 (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_double(msg, "low", bookp->low) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_double (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_double(msg, "last", bookp->last) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_double (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_double(msg, "high", bookp->high) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_double (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_timestamp(msg, "timestamp", &tv) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_timestamp (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_uint8(msg, "wall_time", wall_time) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_uint8 (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	return (msg);
}

lbmsdm_msg_t *
price_to_msg(price_t price)
{
	lbmsdm_msg_t * msg;
	struct timeval tv;

	current_tv(&tv);
	if (lbmsdm_msg_create(&msg) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_create (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		return (NULL);
	}

	if (lbmsdm_msg_add_uint8(msg, "msgtype", (uint8_t) 'P') == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_uint8 (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_double(msg, "price", price) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_double (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_timestamp(msg, "timestamp", &tv) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_timestamp (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	if (lbmsdm_msg_add_uint8(msg, "wall_time", wall_time) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_add_uint8 (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		lbmsdm_msg_destroy(msg);
		return (NULL);
	}
	return (msg);
}

/* --- Price source book state machine --- */

/*
 * This state machine governs the sending of price books.  Its
 * primary function is to strike a balance between promptly serving
 * book requests and spending too much time (and bandwidth) serving
 * book requests.  It allows requests and books to "pass in the mail"
 * without needless retransmission.
 *
 * Requests that arrive very soon after a book has been sent are
 * "squelched" (i.e. they're ignored).  Later requests are
 * "rate limited" (i.e., they're accumulated so that a single book will
 * be sent in the future after a delay that limits the maximum book
 * transmission rate).
 *
 * States:
 *  SRC_READY	Haven't sent a book recently
 *		Will send book immediately if a request arrives
 *  SQUELCH	Have sent a book within the last SQUELCH_MS milliseconds
 *		Ignoring requests assuming they passed a book in the mail
 *  RATE_LIMIT	Have sent a book recently, but it's been at least SQUELCH_MS
 *		Accumulating requests to be sent after RATE_LIMT_MS ms
 */
struct
{
	enum       src_states
	{
		SRC_READY, SQUELCH, RATE_LIMIT
	} state;
	int        timer_id;
	lbm_context_t *ctx;
	lbm_event_queue_t *evq;
	int        received;
	int        sent;
	int        squelched;
	int        rate_limited;
	int        requests_pending;
} src_book_sm = {SRC_READY, -1, NULL, 0, 0, 0, 0, 0};
#define	SQUELCH_MS	2000	/* Time to ignore requests after book send */
#define	RATE_LIMIT_MS	3000	/* Time to batch requests for service later */

/*
 * Take action when price timer expires:  send a new price.
 */
int
price_timer_action(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;
	int err;			   /* return status of lbm functions */
	lbmsdm_msg_t * msg;	/* pointer to message */

	price_update(&src_book);
	msg = price_to_msg(src_book.last);
	if (msg == NULL)
	{
		exit(1);
	}
	err = lbm_src_send(src, lbmsdm_msg_get_data(msg), lbmsdm_msg_get_datalen(msg), LBM_MSG_FLUSH);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	if (lbmsdm_msg_destroy(msg) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_destroy (line %d): %s\n", __LINE__, lbmsdm_errmsg());
	}
	price_timer_id = lbm_schedule_timer(ctx, price_timer_action, src,
										src_book_sm.evq, PRICE_MS);
	if (price_timer_id == -1)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg());
		exit(1);
	}
	return (0);
}

/* Forward declaration of function to follow */
int
book_timer_action(lbm_context_t *ctx, const void *clientd);

void
book_send(lbm_src_t *src)
{
	int err;			/* return status of lbm functions */
	lbmsdm_msg_t * msg;

	printf("Sending book.\n");
	msg = book_to_msg(&src_book);
	if (msg == NULL)
	{
		exit(1);
	}
	err = lbm_src_send(src, lbmsdm_msg_get_data(msg), lbmsdm_msg_get_datalen(msg), LBM_MSG_FLUSH);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	if (lbmsdm_msg_destroy(msg) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_destroy (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	src_book_sm.sent++;

	printf("Entering SQUELCH state\n");
	src_book_sm.state = SQUELCH;
	src_book_sm.timer_id = lbm_schedule_timer(src_book_sm.ctx,
											  book_timer_action, src, src_book_sm.evq, SQUELCH_MS);
	if (src_book_sm.timer_id == -1)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg());
		exit(1);
	}
}

/*
 * Take action when source book timer expires.
 */
int
book_timer_action(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;

	switch (src_book_sm.state)
	{
		case SQUELCH:
			printf("Entering RATE_LIMIT state\n");
			src_book_sm.state = RATE_LIMIT;
			src_book_sm.requests_pending = 0;
			src_book_sm.timer_id = lbm_schedule_timer(ctx, book_timer_action, src,
													  src_book_sm.evq, RATE_LIMIT_MS);
			if (src_book_sm.timer_id == -1)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg());
				exit(1);
			}
			break;

		case RATE_LIMIT:
			if (src_book_sm.requests_pending)
			{
				printf("Exiting RATE_LIMIT state with %d requests pending\n",
					   src_book_sm.requests_pending);
				book_send(src);	/* This also advances state to SQUELCH */
			}
			else
			{
				printf("Entering SRC_READY state\n");
				src_book_sm.state = SRC_READY;
				printf("Requests:  received %d, sent %d, squelched %d, rate limited %d\n",
					   src_book_sm.received, src_book_sm.sent,
					   src_book_sm.squelched, src_book_sm.rate_limited);
			}
			break;

		default:
		case SRC_READY:
			printf("book_timer_action: In state %d.  Should not happen!\n",
				   src_book_sm.state);
			exit(1);
	}
	return (0);
}

void
book_req_action(lbm_src_t *src, lbm_msg_t *msg)
{
	src_book_sm.received++;
	switch (src_book_sm.state)
	{
		case SRC_READY:
			book_send(src);
			break;

		case SQUELCH:
			src_book_sm.squelched++;
			printf("Squelched request from %s\n", msg->source);
			break;

		case RATE_LIMIT:
			src_book_sm.rate_limited++;
			src_book_sm.requests_pending++;
			printf("Rate limited request from %s\n", msg->source);
			break;

		default:
			printf("book_req_action: In state %d.  Should not happen!\n",
				   src_book_sm.state);
			exit(1);
	}
}

/* --- Book request receiver action and price source mainline --- */

int
req_rcv_action(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;

	/* There are several different events that can cause the receiver callback
	 * to be called.  Decode the event that caused this.  */
	switch (msg->type)
	{
		case LBM_MSG_DATA:	  /* a received message */
			if (verbose)
				printf("Received %zu bytes on topic %s: '%.*s'\n",
					   msg->len, msg->topic_name, (int)msg->len, msg->data);

			switch (msg->data[0])
			{
				case 'I':
					printf("Initial book request from %s\n", msg->source);
					book_req_action(src, msg);
					break;
				case 'R':
					printf("Book refresh request from %s\n", msg->source);
					book_req_action(src, msg);
					break;
			}
			break;

		case LBM_MSG_NO_SOURCE_NOTIFICATION:
			printf("No price receivers found.  Try running lbmprice\n");
			break;

			/*
			 * Simply log unrecoverable loss here and count on price receiver
			 * request state machine to repeat request if services is still
			 * needed.
			 */
		case LBM_MSG_UNRECOVERABLE_LOSS:
			printf("Unrecoverable loss from %s\n", msg->source);
			break;

		case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
			printf("Unrecoverable burst loss from %s\n", msg->source);
			break;

		case LBM_MSG_BOS:
			printf("Beginning of stream from %s\n", msg->source);
			break;

		case LBM_MSG_EOS:
			printf("End of stream from %s\n", msg->source);
			break;

		default:	/* unexpected receiver event */
			printf("line %d: unexpected event type %d\n", __LINE__, msg->type);
			exit(1);
	}  /* switch msg->type */

	return (0);
}  /* req_rcv_action */

/*
 * N.B. This callback must run on the LBM context thread whereas the
 * others are configured to run on the main thread via an event queue.
 * It wouldn't be good practice for a production application to be
 * calling something like printf() that might block from here.
 */
int
src_new_source_action(const char *topic, const char *src, void *clientd)
{
	if (strcmp(topic, REQ_TOPIC) == 0)
	{
		printf("New price receiver discovered: %s\n", src);
	}
	return (0);
}

int
src_main(int argc, char *argv[])
{
	lbm_context_t *ctx;						/* pointer to context object */
	lbm_src_t *src;							/* pointer to source (sender) object */
	lbm_rcv_t *rcv;							/* pointer to receiver object */
	lbm_event_queue_t *evq = NULL;
	int err;										/* return status of lbm functions */
	lbm_src_notify_func_t src_notify;
	const char *nak_gen_ivl = NULL;		/* how long (in ms) to generate NAKs */
	unsigned char multicast_ttl = 16;
	int c, errflag = 0;

	static char loss_str[256] = "";

	while ((c = getopt(argc, argv, "c:l:n:st:v")) != EOF)
	{
		switch (c)
		{
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'l':
				sprintf (loss_str, "%s=%s", "LBTRM_LOSS_RATE", optarg);
				putenv (loss_str);
				break;
			case 'n':
				nak_gen_ivl = optarg;
				break;
			case 's':
				/* Couldn't have gotten here without it */
				break;
			case 't':
				multicast_ttl = atoi(optarg);
				break;
			case 'v':
				verbose++;
				break;
			default:
				errflag++;
				break;
		}
	}

	if (errflag || (optind != argc))
	{
		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	/* Create an event queue. */
	err = lbm_event_queue_create(&evq, NULL, NULL, NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Save event queue pointer so it can be used by callbacks */
	src_book_sm.evq = evq;

	/* Set the callback for new source notification */
	src_notify.clientd = NULL;
	src_notify.notifyfunc = src_new_source_action;

	/*
	 * Create context with:
	 *  Multicast ttl if different from default
	 *  Modest UDP buffer size
	 *  New source notification callback pointer
	 */
	err = lbmaux_context_cond_setopt_create(&ctx, NULL, NULL,
											(multicast_ttl != 16),
											"resolver_multicast_ttl", &multicast_ttl, sizeof(multicast_ttl),
											(1),
											"transport_lbtrm_receiver_socket_buffer", &udpbuf, sizeof(udpbuf),
											(1),
											"resolver_source_notification_function", &src_notify, sizeof(src_notify),
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/*
	 * Save context pointer in state machine for use in
	 * setting book timers from callbacks.
	 */
	src_book_sm.ctx = ctx;

	/*
	 * Create LBT-RM source with short (2s) activity timer.
	 */
	err = lbmaux_src_str_setopt_create(&src, ctx, DATA_TOPIC, NULL, NULL, evq,
									   "transport", "LBTRM",
									   "transport_lbtrm_sm_maximum_interval", "2000",
									   NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/*
	 * Create receiver with:
	 *  Activity timeout to 6 seconds to discover dead sources sooner
	 *  NAK generation interval from command line
	 *  Arrival-order delivery (ordering of requests is unimportant)
	 *  No source notification every 50 queries * 10 queries/sec = 5 seconds
	 */
	err = lbmaux_rcv_cond_str_setopt_create(&rcv, ctx, REQ_TOPIC, 
											req_rcv_action, src, evq,
											(1),
											"transport_lbtrm_activity_timeout", "6000",
											(nak_gen_ivl != NULL),
											"transport_lbtrm_nak_generation_interval", nak_gen_ivl,
											(1),
											"ordered_delivery", "0",
											(1),
											"resolution_no_source_notification_threshold", "50",
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	src_book_init(&src_book);
	/*
	 * Schedule price timer
	 */
	price_timer_id = lbm_schedule_timer(ctx, price_timer_action, src, evq,
										PRICE_MS);
	if (price_timer_id == -1)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg());
		exit(1);
	}
	/* the real work will be done on callbacks */
	while (1)
	{
		err = lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK);
		if (err == -1)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
	}

	/*
	 * This main() runs till interrupted, hence no need for the usual
	 * cleanup (i.e. lbm_*_delete()) here.
	 */
	/*NOTREACHED*/

	return (0);
}  /* src_main */

/* --- Receiver price book manipulation --- */

void
rcv_book_init(book_t *bookp)
{
	bookp->state = INVALID;
	bookp->newest_book = 0;
	bookp->leading     = 0;
	bookp->trailing    = 0;
	bookp->updates     = 0;
	bookp->total       = 0.0;
	bookp->max         = -DBL_MAX;
}

void
rcv_book_invalidate(book_t *bookp)
{
	bookp->state = INVALID;
}

int
book_valid(book_t *bookp)
{
	return (bookp->state == VALID);
}

void
book_print(book_t *bookp)
{
	double  ms = (double) 0.0;
	struct timeval now, latency;

	switch (bookp->state)
	{
		case VALID:
			printf("%4d %7.3f  %7.3f  %7.3f ",
				   bookp->leading, bookp->low, bookp->last, bookp->high);
			break;
		case UNCERTAIN:
			printf("%4d %7.3f* %7.3f  %7.3f*",
				   bookp->leading, bookp->low, bookp->last, bookp->high);
			break;
		case INVALID:
			printf("%4d %7s  %7.3f  %7s ", bookp->leading,
				   "--.---", bookp->last, "--.---");
			break;
		default:
			printf("book_print: Book state is %d.  Should not happen!\n",
				   bookp->state);
			exit(1);
	}

	if (wall_time != remote_wall_time)
		printf(" (incompatible clocks, latency not calculated");
	else
	{
		current_tv(&now);
		latency.tv_sec  = now.tv_sec  - bookp->ts.tv_sec;
		latency.tv_usec = now.tv_usec - bookp->ts.tv_usec;
		normalize_tv(&latency);
		ms = latency.tv_sec*1000.0 + latency.tv_usec/1000.0;
		bookp->total += ms;
		bookp->updates++;
		if (ms > bookp->max)
			bookp->max = ms;

		printf(" (%7.6f s", ms/1000.0);
		if (getenv("LBTRM_LOSS_RATE") != NULL)
		{
			printf(", %7.6f avg., %7.6f max.",
				   bookp->total/bookp->updates/1000.0, bookp->max/1000.0);
		}
	}

	printf(") ");
	if ( (ms >= 5000.0) || (ms <= -5000.0) )
		printf ("unusually high latency, possible clock skew\n");
	else
		printf ("\n");

	if (bookp->state != INVALID)
	{
		if (bookp->low > bookp->last)
		{
			printf("Low is higher than last!\n");
			exit(1);
		}
		if (bookp->high < bookp->last)
		{
			printf("High is less than last!\n");
			exit(1);
		}
	}
}

/* --- Price receiver book request state machine --- */
/*
 * This state machine governs the sending of book requests.  Its
 * primary function is to prevent a price receiver from repeating book
 * requests too often.  If a source has indeed missed a book request, a
 * receiver might actually make things worse by repeating the request
 * too soon.  The state machine allows requests and books to "pass
 * in the mail" yet leaves a safety net for cases where a request
 * is completely lost.  It also governs the sending of the initial book
 * request so that it is delayed by a random interval.  Otherwise, a newly
 * discovered price source could be bombarded with requests from a waiting
 * hoard of price receivers.
 *
 * States:
 *  INIT      We HAVE NOT sent our initial request after source discovery
 *  RCV_READY We HAVE NOT sent a request within the last REQ_WAIT_MS ms
 *  WAITING   We HAVE     sent a request within the last REQ_WAIT_MS ms
 */
struct
{
	enum       rcv_states
	{
		INIT, RCV_READY, WAITING
	} state;
	int        timer_id;
	lbm_context_t *ctx;
	int        src_count;
	lbm_event_queue_t *evq;
	struct timeval start_tv;	/* When source or receiver started */
	struct timeval req_tv;
} rcv_req_sm = {RCV_READY, -1, NULL, 0};
#define	REQ_WAIT_MS	6000	/* Wait time between requests sent (ms) */
/* 110 below is resolver_query_interval+fudge */
#define	INIT_FLOOR_MS	 125	/* Minimum time before initial book request */
#define	INIT_INTVL_MS	 375	/* Random interval added to above (ms) */

void
book_rcv_action(lbmsdm_msg_t * msg, book_t *bookp)
{
	book_t src_book;

	if (rcv_req_sm.state == WAITING)
	{
		struct timeval now, latency;
		double  ms;

		current_tv(&now);
		latency.tv_sec  = now.tv_sec  - rcv_req_sm.req_tv.tv_sec;
		latency.tv_usec = now.tv_usec - rcv_req_sm.req_tv.tv_usec;
		normalize_tv(&latency);
		ms = latency.tv_sec*1000.0 + latency.tv_usec/1000.0;
		/*
		 * Note that latency could be negative (even without clock skew)
		 * if source sent book before we sent request.  Likely if we
		 * were delayed in reading messages.
		 */
		printf("Book request was serviced in %7.3f ms.\n", ms);
	}
	/*
	 * Having just received a book, we are in a state ready to request
	 * another when needed and we cancel any timer we had that would've
	 * given us permission to nag the source with another request.
	 */
	if (rcv_req_sm.state != RCV_READY)
	{
		printf("Entering state RCV_READY.  ");
	}
	printf("Book %d received.\n", bookp->leading);
	rcv_req_sm.state = RCV_READY;
	if (rcv_req_sm.timer_id != -1)
	{
		printf("Canceling pending request timer.\n");
		lbm_cancel_timer(rcv_req_sm.ctx, rcv_req_sm.timer_id, NULL);
		rcv_req_sm.timer_id = -1;
	}

	if (lbmsdm_msg_get_double_name(msg, "low", &(src_book.low)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_double_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	if (lbmsdm_msg_get_double_name(msg, "last", &(src_book.last)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_double_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	if (lbmsdm_msg_get_double_name(msg, "high", &(src_book.high)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_double_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}

	if (book_valid(bookp))
	{
		/* Scan source's book and consistency check with ours */
		if (src_book.low!=src_book.last && src_book.low!=bookp->low)
		{
			printf("Book consistency error: expecting low %7.3f, got %7.3f\n",
				   bookp->low, src_book.low);
		}
		if (src_book.last!=src_book.high && src_book.high!=bookp->high)
		{
			printf("Book consistency error: expecting high %7.3f, got %7.3f\n",
				   bookp->high, src_book.high);
		}
	}

	if (lbmsdm_msg_get_timestamp_name(msg, "timestamp", &(bookp->ts)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_timestamp_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	bookp->state = VALID;

	book_print(bookp);
}

void
price_rcv_action(lbmsdm_msg_t * msg, book_t *bookp)
{
	if (lbmsdm_msg_get_double_name(msg, "price", &(bookp->last)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_double_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	if (lbmsdm_msg_get_timestamp_name(msg, "timestamp", &(bookp->ts)) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_timestamp_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	(void) book_hi_low_set(&rcvd_book, bookp->last);
	book_print(bookp);
}

/* Forward declaration of function to follow */
int
req_timer_action(lbm_context_t *ctx, const void *clientd);

void
req_send(lbm_src_t *src, int c)
{
	int err;			/* return status of lbm functions */
	char msg[100];

	if (rcv_req_sm.state != WAITING)
	{
		printf("Entering state WAITING.  ");
		rcv_req_sm.state = WAITING;
	}

	printf("Sending book request.");

#if defined(_WIN32)
	msg[0] = (char) c;
	msg[1] = '\0';  
#else
	snprintf(msg, sizeof(msg), "%c", c);
#endif

	err = lbm_src_send(src, msg, strlen(msg)+1, LBM_MSG_FLUSH);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	if (rcv_req_sm.timer_id == -1)
	{
		printf("  Setting request timer.");
		rcv_req_sm.timer_id = lbm_schedule_timer(rcv_req_sm.ctx,
												 req_timer_action, src, rcv_req_sm.evq, REQ_WAIT_MS);
		if (rcv_req_sm.timer_id == -1)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg());
			exit(1);
		}
		current_tv(&rcv_req_sm.req_tv);
	}
	printf("\n");
}

int
req_timer_action(lbm_context_t *ctx, const void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;

	rcv_req_sm.timer_id = -1;
	switch (rcv_req_sm.state)
	{
		case INIT:
			printf("Request timer expired in state INIT.\n");
			req_send(src, 'I');
			break;
		case WAITING:
			printf("Request timer expired in state WAITING.\n");
			req_send(src, 'R');
			break;
		case RCV_READY:
		default:
			printf("req_timer_action: In state %d.  Should not happen!\n",
				   rcv_req_sm.state);
			exit(1);
	}
	return (0);
}

void
book_needed(lbm_src_t *src, int reason)
{
	if (rcv_req_sm.state == RCV_READY)
	{
		req_send(src, 'R');
	}
}

/*
 * Data topic receiver message action
 *
 * Most of the conditional logic in this function detects cases where
 * messages arrive out of order, tracks progress toward eventual
 * delivery of all messages, and sets state variables for others.
 *
 * If ordered_delivery is 1 (the default), then this whole function
 * just boils down to calling book_rcv_action() or price_rcv_action().
 *
 * Note that although a linked list of messages is maintained when they're
 * arriving out-of-order, the messages are still acted upon as they arrive.
 * They're queued only as a simple means to detect when all have arrived.
 * A simple bitmap would be adequate for this purpose, but not as clear.
 */
void
data_msg_action(lbm_msg_t *msg)
{
	struct msg_list_t
	{
		lbm_msg_t       *msg;
		struct msg_list_t   *next;
	};
	static struct msg_list_t *head = NULL;
	struct msg_list_t *p, *np;

	lbmsdm_msg_t * sdmmsg;
	uint8_t msgtype;

	if (verbose)
		printf("Received %zu bytes on topic %s: '%.*s'\n",
			   msg->len, msg->topic_name, (int)msg->len, msg->data);

	/*
	 * Detect and deal with uncertainty caused by messages arriving
	 * out of order.
	 */
	if (rcvd_book.state == VALID
		&&  msg->sequence_number > (rcvd_book.leading+1))
	{
		/*
		 * A message arriving out of order when the book is valid opens
		 * the uncertainty window.
		 */
		printf("Book state is now UNCERTAIN due to arrival of msg %d.\n",
			   msg->sequence_number);
		rcvd_book.state = UNCERTAIN;
		rcvd_book.trailing = rcvd_book.leading;
	}

	if (rcvd_book.state == UNCERTAIN)
	{
		/* Keep all messages that arrive while UNCERTAIN */
		if ((np=malloc(sizeof(struct msg_list_t))) == NULL)
		{
			printf("data_msg_action: malloc() failed\n");
			exit(1);
		}
		lbm_msg_retain(msg);	/* We may reference msg later */
		np->msg = msg;
		if (head==NULL || head->msg->sequence_number>=msg->sequence_number)
		{
			/* Special case insert before the head */
			np->next = head;
			head = np;
		}
		else
		{
			/* Search for appropriate insertion point in list */
			p = head;
			while (p->next!=NULL && p->next->msg->sequence_number<msg->sequence_number)
			{
				p = p->next;
			}
			np->next = p->next;
			p->next = np;
		}
	}

	/* Advance leading edge */
	if (msg->sequence_number > rcvd_book.leading)
		rcvd_book.leading = msg->sequence_number;

	if (rcvd_book.state == UNCERTAIN
		&&  msg->sequence_number == (rcvd_book.trailing+1))
	{
		while (head!=NULL && head->msg->sequence_number==rcvd_book.trailing+1)
		{
			rcvd_book.trailing++;
			lbm_msg_delete(head->msg);
			p = head;
			head = head->next;
			free(p);
		}
		if (rcvd_book.trailing == rcvd_book.leading)
		{
			if (head != NULL)
			{
				printf("Head != NULL after uncertainty window closed.\n");
				exit(1);
			}
			printf("Book state is now VALID.\n");
			rcvd_book.state = VALID;
		}
	}

	if (lbmsdm_msg_parse(&sdmmsg, msg->data, msg->len) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_parse (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	if (lbmsdm_msg_get_uint8_name(sdmmsg, "msgtype", &msgtype) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_uint8_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	if (lbmsdm_msg_get_uint8_name(sdmmsg, "wall_time", &remote_wall_time) == LBMSDM_FAILURE)
	{
		printf("lbmsdm_msg_get_uint8_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
		exit(1);
	}
	switch (msgtype)
	{
		case 'B':
			/* Act on new books, ignore others */
			/*
			 * LBM never delivers the same message twice, so the == case
			 * below should never happen.  But it can be true if a book is
			 * the first message received from a source since newest_book
			 * is initialized to 0.  Also act if first message is a book.
			 */
			if (msg->sequence_number >= rcvd_book.newest_book)
			{
				book_rcv_action(sdmmsg, &rcvd_book);
				rcvd_book.newest_book = msg->sequence_number;
			}
			else
			{
				printf("Ignoring book %d since we've already seen %d\n",
					   msg->sequence_number, rcvd_book.newest_book);
			}
			break;

		case 'P':
			/* Act on new prices, merge others into book */
			if (msg->sequence_number >= rcvd_book.leading)
			{
				price_rcv_action(sdmmsg, &rcvd_book);
			}
			else
			{
				price_t pr;
				if (lbmsdm_msg_get_double_name(sdmmsg, "price", &pr) == LBMSDM_FAILURE)
				{
					printf("lbmsdm_msg_get_double_name (line %d): %s\n", __LINE__, lbmsdm_errmsg());
					exit(1);
				}
				/* Print book if merge sets new record */
				if (book_hi_low_set(&rcvd_book, pr))
				{
					printf("New high/low set by price %d received out of order.\n",
						   msg->sequence_number);
					book_print(&rcvd_book);
				}
				else
				{
					printf("Completely ignoring price %d received out of order.\n",
						   msg->sequence_number);
				}
			}
			break;
	}
	lbmsdm_msg_destroy(sdmmsg);
}

/* --- Data topic receiver event action and price receiver main */

int
data_rcv_event_action(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	lbm_src_t *src = (lbm_src_t *)clientd;
	int err;			   /* return status of lbm functions */
	
	/* There are several different events that can cause the receiver callback
	 * to be called.  Decode the event that caused this.  */
	switch (msg->type)
	{		
		case LBM_MSG_DATA:	  /* a received message */
			data_msg_action(msg);
			break;

			/* A strong sign our prices are stale.  A GUI app. might flag this */
		case LBM_MSG_NO_SOURCE_NOTIFICATION:
			printf("No price sources found.  Try running lbmprice -s\n");
			break;

		case LBM_MSG_UNRECOVERABLE_LOSS:
			printf("Unrecoverable loss.  Invalidating book.\n");
			rcv_book_invalidate(&rcvd_book);
			book_needed(src, 'R');
			break;

		case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
			printf("Unrecoverable burst loss.  Invalidating book.\n");
			rcv_book_invalidate(&rcvd_book);
			book_needed(src, 'R');
			break;

		case LBM_MSG_EOS:
			printf("End of transport session from %s\n", msg->source);
			/* Decrement below should be protected with a mutex or atomic */
			if (rcv_req_sm.src_count-- > 1)
			{
				printf("Still have %d source%s left.\n",
					   rcv_req_sm.src_count, (rcv_req_sm.src_count==1) ? "" : "s");
			}
			err = lbm_event_dispatch_unblock(rcv_req_sm.evq);
			if (err)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
			}
			break;
			
		case LBM_MSG_BOS:
			printf("Beginning of stream from %s\n", msg->source);
			break;

		default:	/* unexpected receiver event */
			printf("line %d: unexpected event type %d\n", __LINE__, msg->type);
	}  /* switch msg->type */

	return (0);
}  /* data_rcv_event_action */

/*
 * N.B. This callback must run on the LBM context thread whereas the
 * others are configured to run on the main thread via an event queue.
 * It wouldn't be good practice for a production application to be
 * calling something like printf() that might block from here.
 */
int
rcv_new_source_action(const char *topic, const char *src, void *clientd)
{
	lbm_event_queue_t *evq = (lbm_event_queue_t *)clientd;
	int err;			   /* return status of lbm functions */

	if (strcmp(topic, DATA_TOPIC) == 0)
	{
		struct timeval now, discov;
		double  ms;

		printf("New price source discovered: %s\n", src);
		/* Increment below should be protected with a mutex or atomic */
		rcv_req_sm.src_count++;
		err = lbm_event_dispatch_unblock(evq);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
		current_tv(&now);
		discov.tv_sec  = now.tv_sec  - rcv_req_sm.start_tv.tv_sec;
		discov.tv_usec = now.tv_usec - rcv_req_sm.start_tv.tv_usec;
		normalize_tv(&discov);
		ms = discov.tv_sec*1000.0 + discov.tv_usec/1000.0;
		printf("Source was discovered in %7.3f ms.\n", ms);
	}
	return (0);
}

/*
 * Price receiver main()
 */
int
rcv_main(int argc, char *argv[])
{
	lbm_context_t *ctx;		/* pointer to context object */
	lbm_rcv_t *rcv;		/* pointer to receiver object */
	lbm_src_t *src;		/* pointer to source (sender) object */
	lbm_event_queue_t *evq = NULL;
	lbm_src_notify_func_t src_notify;
	int err;			/* return status of lbm functions */
	const char *ordered_delivery = NULL;/* ask LBM to order before delivery? */
	const char *nak_gen_ivl = "3000"; /* how long (in ms) to generate NAKs */
	unsigned char multicast_ttl = 16;
	int last_src_count;
	int c, errflag = 0;

	static char loss_str[256] = "";

	while ((c = getopt(argc, argv, "c:l:n:o:t:v")) != EOF)
	{
		switch (c)
		{
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'l':
				sprintf (loss_str, "%s=%s", "LBTRM_LOSS_RATE", optarg);
				putenv (loss_str);
				break;
			case 'n':
				nak_gen_ivl = optarg;
				break;
			case 'o':
				ordered_delivery = optarg;
				break;
			case 't':
				multicast_ttl = atoi(optarg);
				break;
			case 'v':
				verbose++;
				break;
			default:
				errflag++;
				break;
		}
	}

	if (errflag || (optind != argc))
	{
		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	rcv_book_init(&rcvd_book);

	/* XXX Windows portability trouble? */
#if defined(_WIN32)
	srand((unsigned int) (getpid()+time(NULL)));
#else
	srandom(getpid()+time(NULL));
#endif

	current_tv(&rcv_req_sm.start_tv);

	/* Create an event queue. */
	err = lbm_event_queue_create(&evq, NULL, NULL, NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/* Save event queue pointer so it can be used by callbacks */
	rcv_req_sm.evq = evq;

	/* Set the callback for new source notification */
	src_notify.clientd = evq;
	src_notify.notifyfunc = rcv_new_source_action;
	/*
	 * Create context with:
	 *  Multicast ttl if different from default
	 *  Modest UDP buffer size
	 *  New source notification callback pointer
	 */
	err = lbmaux_context_cond_setopt_create(&ctx, NULL, NULL,
											(multicast_ttl != 16),
											"resolver_multicast_ttl", &multicast_ttl, sizeof(multicast_ttl),
											(1),
											"transport_lbtrm_receiver_socket_buffer", &udpbuf, sizeof(udpbuf),
											(1),
											"resolver_source_notification_function", &src_notify, sizeof(src_notify),
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/*
	 * Save context pointer in state machine for use in
	 * setting request timers from callbacks.
	 */
	rcv_req_sm.ctx = ctx;

	/*
	 * Create LBT-RM source with short (2s) activity timer.
	 */
	err = lbmaux_src_str_setopt_create(&src, ctx, REQ_TOPIC, NULL, NULL, evq,
									   "transport", "LBTRM",
									   "transport_lbtrm_sm_maximum_interval", "2000",
									   NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/*
	 * Create receiver with:
	 *  Activity timeout to 6 seconds to discover dead sources sooner
	 *  NAK generation interval from command line
	 *  Arrival-order delivery if requested
	 *  No source notification every 50 queries * 10 queries/sec = 5 seconds
	 */
	err = lbmaux_rcv_cond_str_setopt_create(&rcv, ctx, DATA_TOPIC, 
											data_rcv_event_action, src, evq,
											(1),
											"transport_lbtrm_activity_timeout", "6000",
											(nak_gen_ivl != NULL),
											"transport_lbtrm_nak_generation_interval", nak_gen_ivl,
											(ordered_delivery != NULL),
											"ordered_delivery", ordered_delivery,
											(1),
											"resolution_no_source_notification_threshold", "50",
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	last_src_count = 0;
	/* the real work will be done on callbacks */
	while (1)
	{
		err = lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK);
		if (err == -1)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
		/*
		 * We've popped out of lbm_event_dispatch() either because a new
		 * source was discovered or because an old one went away.
		 */
		if (last_src_count==0 && rcv_req_sm.src_count>=1)
		{
			int ms;	/* ms to wait before sending request */

			printf("Found a source.\n");
			if (rcv_req_sm.timer_id != -1)
			{
				printf("Canceling pending request timer.\n");
				lbm_cancel_timer(rcv_req_sm.ctx, rcv_req_sm.timer_id, NULL);
				rcv_req_sm.timer_id = -1;
			}
			rcv_req_sm.state = INIT;

#if defined (_WIN32)
			ms = INIT_FLOOR_MS + ((long)rand()%1000)*INIT_INTVL_MS/1000;
#else
			ms = INIT_FLOOR_MS + (random()%1000)*INIT_INTVL_MS/1000;
#endif

			printf("Delaying initial book request by %d ms\n", ms);
			rcv_req_sm.timer_id = lbm_schedule_timer(rcv_req_sm.ctx,
													 req_timer_action, src, evq, ms);
			if (rcv_req_sm.timer_id == -1)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg());
				exit(1);
			}
		}
		if (last_src_count>=1 && rcv_req_sm.src_count==0)
		{
			printf("Lost a source.\n");
			/*
			 * Get ready for discovery of next source.
			 * Clean out any book state from previous source.
			 * Cancel any pending request timer.
			 */
			rcv_book_init(&rcvd_book);
			if (rcv_req_sm.state != RCV_READY)
			{
				printf("Entering state RCV_READY.\n");
				rcv_req_sm.state = RCV_READY;
			}
			if (rcv_req_sm.timer_id != -1)
			{
				printf("Canceling pending request timer.\n");
				lbm_cancel_timer(rcv_req_sm.ctx, rcv_req_sm.timer_id, NULL);
				rcv_req_sm.timer_id = -1;
			}
		}
		last_src_count = rcv_req_sm.src_count;	/* Remember for next time around */
	}

	/*
	 * This main() runs till interrupted, hence no need for the usual
	 * cleanup (i.e. lbm_*_delete()) here.
	 */
	/*NOTREACHED*/

	return (0);
}  /* rcv_main */

/* --- Hot Failover receiver main */

int
rcv_new_hf_source_action(const char *topic, const char *src, void *clientd)
{
	lbm_event_queue_t *evq = (lbm_event_queue_t *)clientd;
	int err;			   /* return status of lbm functions */

	if (strcmp(topic, DATA_HF_TOPIC) == 0)
	{
		printf("New HF price source discovered: %s\n", src);
		/* Increment below should be protected with a mutex or atomic */
		rcv_req_sm.src_count++;
		err = lbm_event_dispatch_unblock(evq);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
	}
	return (0);
}

/*
 * Hot Failover price receiver main()
 */
int
hf_rcv_main(int argc, char *argv[])
{
	lbm_context_t *ctx;		/* pointer to context object */
	lbm_topic_t *dtopic;	/* pointer to data topic object */
	lbm_hf_rcv_t *hf_rcv;	/* pointer to hot failover receiver object */
	lbm_src_t *src;		/* pointer to source (sender) object */
	lbm_rcv_topic_attr_t * rattr;	/* receiver attributes */
	lbm_event_queue_t *evq = NULL;
	lbm_src_notify_func_t src_notify;
	int err;			/* return status of lbm functions */
	const char *ordered_delivery = NULL;/* ask LBM to order before delivery? */
	const char *nak_gen_ivl = "3000"; /* how long (in ms) to generate NAKs */
	unsigned char multicast_ttl = 16;
	int last_src_count;
	int c, errflag = 0;

	static char loss_str[256] = "";

	while ((c = getopt(argc, argv, "c:Hl:n:o:t:v")) != EOF)
	{
		switch (c)
		{
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'H':
				/* Couldn't've gotten here without it! */
				break;
			case 'l':
				sprintf (loss_str, "%s=%s", "LBTRM_LOSS_RATE", optarg);
				putenv (loss_str);
				break;
			case 'n':
				nak_gen_ivl = optarg;
				break;
			case 'o':
				ordered_delivery = optarg;
				break;
			case 't':
				multicast_ttl = atoi(optarg);
				break;
			case 'v':
				verbose++;
				break;
			default:
				errflag++;
				break;
		}
	}

	if (errflag || (optind != argc))
	{
		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	rcv_book_init(&rcvd_book);

	/* XXX Windows portability trouble? */
#if defined(_WIN32)
	srand((unsigned int) (getpid()+time(NULL)));
#else
	srandom(getpid()+time(NULL));
#endif

	current_tv(&rcv_req_sm.start_tv);

	/* Create an event queue. */
	err = lbm_event_queue_create(&evq, NULL, NULL, NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/* Save event queue pointer so it can be used by callbacks */
	rcv_req_sm.evq = evq;

	/* Set the callback for new source notification */
	src_notify.clientd = evq;
	src_notify.notifyfunc = rcv_new_hf_source_action;
	/*
	 * Create context with:
	 *  Multicast ttl if different from default
	 *  Modest UDP buffer size
	 *  New source notification callback pointer
	 */
	err = lbmaux_context_cond_setopt_create(&ctx, NULL, NULL,
											(multicast_ttl != 16),
											"resolver_multicast_ttl", &multicast_ttl, sizeof(multicast_ttl),
											(1),
											"transport_lbtrm_receiver_socket_buffer", &udpbuf, sizeof(udpbuf),
											(1),
											"resolver_source_notification_function", &src_notify, sizeof(src_notify),
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/*
	 * Save context pointer in state machine for use in
	 * setting request timers from callbacks.
	 */
	rcv_req_sm.ctx = ctx;

	/*
	 * Create LBT-RM source with short (2s) activity timer.
	 */
	err = lbmaux_src_str_setopt_create(&src, ctx, REQ_TOPIC, NULL, NULL, evq,
									   "transport", "LBTRM",
									   "transport_lbtrm_sm_maximum_interval", "2000",
									   NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Get ready to set receiver options we want */
	err = lbm_rcv_topic_attr_create(&rattr);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* set activity timeout to 6 seconds to discover dead sources sooner */
	err = lbm_rcv_topic_attr_str_setopt(rattr,
										"transport_lbtrm_activity_timeout", "6000");
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Set NAK generation interval from nak_gen_ivl if option was present */
	if (nak_gen_ivl != NULL)
	{
		err = lbm_rcv_topic_attr_str_setopt(rattr,
											"transport_lbtrm_nak_generation_interval", nak_gen_ivl);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
	}
	/* Set arrival-order delivery if requested */
	if (ordered_delivery != NULL)
	{
		err = lbm_rcv_topic_attr_str_setopt(rattr, "ordered_delivery",
											ordered_delivery);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
	}

	/* queries every 100 ms by default so this is 5 seconds */
	err = lbm_rcv_topic_attr_str_setopt(rattr, "resolution_no_source_notification_threshold", "50");
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	err = lbm_rcv_topic_lookup(&dtopic, ctx, DATA_HF_TOPIC, rattr);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	lbm_rcv_topic_attr_delete(rattr);

	err = lbm_hf_rcv_create(&hf_rcv, ctx, dtopic, data_rcv_event_action, src, evq);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	last_src_count = 0;
	/* the real work will be done on callbacks */
	while (1)
	{
		err = lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK);
		if (err == -1)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
		/*
		 * We've popped out of lbm_event_dispatch() either because a new
		 * source was discovered or because an old one went away.
		 */
		if (last_src_count==0 && rcv_req_sm.src_count>=1)
		{
			int ms;	/* ms to wait before sending request */

			printf("Found our first source.\n");

			if (rcv_req_sm.timer_id != -1)
			{
				printf("Canceling pending request timer.\n");
				lbm_cancel_timer(rcv_req_sm.ctx, rcv_req_sm.timer_id, NULL);
				rcv_req_sm.timer_id = -1;
			}
			rcv_req_sm.state = INIT;

#if defined(_WIN32)
			ms = INIT_FLOOR_MS + ((long)rand()%1000)*INIT_INTVL_MS/1000;
#else
			ms = INIT_FLOOR_MS + (random()%1000)*INIT_INTVL_MS/1000;
#endif
			printf("Delaying initial book request by %d ms\n", ms);
			rcv_req_sm.timer_id = lbm_schedule_timer(rcv_req_sm.ctx,
													 req_timer_action, src, evq, ms);
			if (rcv_req_sm.timer_id == -1)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg());
				exit(1);
			}
		}
		if (last_src_count>=1 && rcv_req_sm.src_count==0)
		{
			printf("Lost our last source.  Deleting receiver & recreating it.\n");
			err = lbm_hf_rcv_delete(hf_rcv);
			if (err)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
			}
			err = lbm_rcv_topic_lookup(&dtopic, ctx, DATA_HF_TOPIC, rattr);
			if (err)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
			}
			err = lbm_hf_rcv_create(&hf_rcv, ctx, dtopic, data_rcv_event_action, src, evq);
			if (err)
			{
				printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
			}
			/*
			 * Get ready for discovery of next source.
			 * Clean out any book state from previous source.
			 * Cancel any pending request timer.
			 */
			rcv_book_init(&rcvd_book);
			if (rcv_req_sm.state != RCV_READY)
			{
				printf("Entering state RCV_READY.\n");
				rcv_req_sm.state = RCV_READY;
			}
			if (rcv_req_sm.timer_id != -1)
			{
				printf("Canceling pending request timer.\n");
				lbm_cancel_timer(rcv_req_sm.ctx, rcv_req_sm.timer_id, NULL);
				rcv_req_sm.timer_id = -1;
			}
		}
		if (rcv_req_sm.src_count != last_src_count)
		{
			printf("Now know of %d HF source%s.\n",
				   rcv_req_sm.src_count, (rcv_req_sm.src_count==1) ? "" : "s");
		}
		last_src_count = rcv_req_sm.src_count;	/* Remember for next time around */
	}

	/*
	 * This main() runs till interrupted, hence no need for the usual
	 * cleanup (i.e. lbm_*_delete()) here.
	 */
	/*NOTREACHED*/

	return (0);
}  /* hf_rcv_main */

/* --- Hot Failover Relay data receiver event action --- */

lbm_src_t *hf_src = NULL;	/* pointer to hot failover source object */

int
data_relay_event_action(lbm_rcv_t *rcv, lbm_msg_t *msg, void *clientd)
{
	int verbose = *(int *)clientd;
	int err;			   /* return status of lbm functions */

	/* There are several different events that can cause the receiver callback
	 * to be called.  Decode the event that caused this.  */
	switch (msg->type)
	{
		case LBM_MSG_DATA:	  /* a received message */
			if (verbose)
			{
				printf("Relaying %d\n", msg->sequence_number);
			}
			if (hf_src != NULL)
			{
				err = lbm_hf_src_send(hf_src, msg->data, msg->len,
									  msg->sequence_number, LBM_MSG_FLUSH);
				if (err)
				{
					printf("data_relay_event_action: lbm_hf_src_send() failed.  Trudging on.\n");
				}
			}
			else
			{
				printf("No HF source.  Msg %d not relayed\n", msg->sequence_number);
			}
			break;

		case LBM_MSG_NO_SOURCE_NOTIFICATION:
			printf("No price sources found.  Try running lbmprice -s\n");
			break;

		case LBM_MSG_UNRECOVERABLE_LOSS:
			printf("Unrecoverable loss.\n");
			break;

		case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
			printf("Unrecoverable burst loss.\n");
			break;

		case LBM_MSG_EOS:
			printf("End of transport session from %s\n", msg->source);
			/*
			 * Get ready for discovery of next source.
			 * Delete the relay source to signal HF receivers to reset.
			 */
			if (hf_src != NULL)
			{
				printf("Deleting HF source.\n");
				lbm_src_delete(hf_src);
				hf_src = NULL;
			}
			break;

		default:	/* unexpected receiver event */
			printf("line %d: unexpected event type %d\n", __LINE__, msg->type);
			exit(1);
	}  /* switch msg->type */

	return (0);
}  /* data_relay_event_action */

/* --- Hot Failover Relay Main --- */

int
hf_relay_main(int argc, char *argv[])
{
	lbm_context_t *ctx;		/* pointer to context object */
	lbm_topic_t *hftopic;	/* pointer to hf   topic object */
	lbm_rcv_t *rcv;		/* pointer to receiver object */
	lbm_src_topic_attr_t *tattr;	/* source/topic attributes */
	lbm_event_queue_t *evq = NULL;
	lbm_src_notify_func_t src_notify;
	int err;			/* return status of lbm functions */
	const char *ordered_delivery = NULL;/* ask LBM to order before delivery? */
	unsigned char multicast_ttl = 16;
	const char *nak_gen_ivl = NULL;	/* how long (in ms) to generate NAKs */
	int c, errflag = 0;
	static char loss_str[256] = "";


	while ((c = getopt(argc, argv, "c:Hl:n:o:st:v")) != EOF)
	{
		switch (c)
		{
			case 'c':
				/* Initialize configuration parameters from a file. */
				if (lbm_config(optarg) == LBM_FAILURE) {
					fprintf(stderr, "lbm_config: %s\n", lbm_errmsg());
					exit(1);
				}
				break;
			case 'H':
				/* Couldn't've gotten here without it */
				break;
			case 'l':
				sprintf (loss_str, "%s=%s", "LBTRM_LOSS_RATE", optarg);
				putenv (loss_str);
				break;
			case 'n':
				nak_gen_ivl = optarg;
				break;
			case 'o':
				ordered_delivery = optarg;
				break;
			case 's':
				/* Couldn't've gotten here without it */
				break;
			case 't':
				multicast_ttl = atoi(optarg);
				break;
			case 'v':
				verbose++;
				break;
			default:
				errflag++;
				break;
		}
	}

	if (errflag || (optind != argc))
	{
		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), usage);
		exit(1);
	}

	current_tv(&rcv_req_sm.start_tv);

	/* Create an event queue. */
	err = lbm_event_queue_create(&evq, NULL, NULL, NULL);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Setup the callback for new source notification */
	src_notify.clientd = evq;
	src_notify.notifyfunc = rcv_new_source_action;

	/*
	 * Create context with:
	 *  Multicast ttl if different from default
	 *  Modest UDP buffer size
	 *  New source notification callback pointer
	 */
	err = lbmaux_context_cond_setopt_create(&ctx, NULL, NULL,
											(multicast_ttl != 16),
											"resolver_multicast_ttl", &multicast_ttl, sizeof(multicast_ttl),
											(1),
											"transport_lbtrm_receiver_socket_buffer", &udpbuf, sizeof(udpbuf),
											(1),
											"resolver_source_notification_function", &src_notify, sizeof(src_notify),
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Retrieve current source topic settings */
	err = lbm_src_topic_attr_create(&tattr);
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Set transport attribute to LBT-RM */
	err = lbm_src_topic_attr_str_setopt(tattr, "transport", "LBTRM");
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* Set max LBT-RM session message interval to 2 seconds */
	err = lbm_src_topic_attr_str_setopt(tattr,
										"transport_lbtrm_sm_maximum_interval", "2000");
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}
	/* Delay actual HF source creation till a price source is found */

	/*
	 * Create receiver with:
	 *  Activity timeout to 6 seconds to discover dead sources sooner
	 *  NAK generation interval from command line
	 *  Arrival-order delivery if requested
	 *  No source notification every 50 queries * 10 queries/sec = 5 seconds
	 */
	err = lbmaux_rcv_cond_str_setopt_create(&rcv, ctx, DATA_TOPIC, 
											data_relay_event_action, &verbose, evq,
											(1),
											"transport_lbtrm_activity_timeout", "6000",
											(nak_gen_ivl != NULL),
											"transport_lbtrm_nak_generation_interval", nak_gen_ivl,
											(ordered_delivery != NULL),
											"ordered_delivery", ordered_delivery,
											(1),
											"resolution_no_source_notification_threshold", "50",
											(-1));
	if (err)
	{
		printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
	}

	/* the real work will be done on callbacks */
	while (1)
	{
		err = lbm_event_dispatch(evq, LBM_EVENT_QUEUE_BLOCK);
		if (err == -1)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
		/*
		 * We've popped out of lbm_event_dispatch() because the new source
		 * notification called lbm_event_dispatch_unblock().
		 */
		err = lbm_src_topic_alloc(&hftopic, ctx, DATA_HF_TOPIC, tattr);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
		lbm_src_topic_attr_delete(tattr);
		err = lbm_hf_src_create(&hf_src, ctx, hftopic, NULL, NULL, evq);
		if (err)
		{
			printf("line %d: %s\n", __LINE__, lbm_errmsg()); exit(1);
		}
	}

	/*
	 * This main() runs till interrupted, hence no need for the usual
	 * cleanup (i.e. lbm_*_delete()) here.
	 */
	/*NOTREACHED*/

	return (0);
}  /* hf_relay_main */

/* --- Mainline code --- */

int
main(int argc, char *argv[])
{
	int c, be_src = 0, hf = 0;

#if defined(_MSC_VER)
	/* windows-specific code */
	WSADATA wsadata;
	int wsStat = WSAStartup(MAKEWORD(2,2), &wsadata);
	if (wsStat != 0)
	{
		printf("line %d: %d\n", __LINE__, wsStat); exit(1);
	}
#endif

	/* are we a price source, receiver, or hot failover relay? */
	/* also do option processing common to both source and receiver */
	while ((c = getopt(argc, argv, "c:hHl:n:o:st:v")) != EOF)
	{
		switch (c)
		{
			case 'h':
				fprintf(stderr, "%s\n%s\n%s\n%s",
						argv[0], lbm_version(), purpose, usage);
				exit(0);
			case 'H':
				hf++;
				break;
			case 's':
				be_src++;
				break;
		}
	}
	optind = 1;		/* reset getopt() for use by other main()'s */
	if (hf)
		return(be_src) ? hf_relay_main(argc, argv) : hf_rcv_main(argc, argv);
	else
		return(be_src) ? src_main(argc, argv) :	rcv_main(argc, argv);
}

