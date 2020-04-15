/*
  lbmmondata.c: example LBM monitoring application.

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

static const char *rcsid_example_lbmmondata = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmondata.c#2 $";

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

const char Purpose[] = "Purpose: Example LBM statistics monitoring application.";
const char OptionString[] = "c:t:";
const char Usage[] =
"Usage: [-c filename] [-t topic]\n"
"       -c filename = Use LBM configuration file filename.\n"
"                     Multiple config files are allowed.\n"
"                     Example:  '-c file1.cfg -c file2.cfg'\n"
"       -t topicname = use topic topicname to receive statistics\n"
;

unsigned int Terminate = 0;
FILE * RcvTCPFile;
FILE * SrcTCPFile;
FILE * RcvLBTRUFile;
FILE * SrcLBTRUFile;
FILE * RcvLBTRMFile;
FILE * SrcLBTRMFile;
FILE * RcvLBTIPCFile;
FILE * SrcLBTIPCFile;
FILE * RcvLBTRDMAFile;
FILE * SrcLBTRDMAFile;
FILE * EvqFile;
FILE * CtxFile;
FILE * CtxIMSrcLBTRMFile;
FILE * CtxIMRcvLBTRMFile;
unsigned int RcvTCPCount = 0;
unsigned int SrcTCPCount = 0;
unsigned int RcvLBTRUCount = 0;
unsigned int SrcLBTRUCount = 0;
unsigned int RcvLBTRMCount = 0;
unsigned int SrcLBTRMCount = 0;
unsigned int RcvLBTIPCCount = 0;
unsigned int SrcLBTIPCCount = 0;
unsigned int RcvLBTRDMACount = 0;
unsigned int SrcLBTRDMACount = 0;
unsigned int EvqCount = 0;
unsigned int CtxCount = 0;
unsigned int CtxIMRcvLBTRMCount = 0;
unsigned int CtxIMSrcLBTRMCount = 0;

void SignalHandler(int signo)
{
	Terminate = 1;
}

void
write_common_data(FILE * Outfile, const void * AttributeBlock, const char * Source)
{
	time_t timestamp = 0;
	struct in_addr addr;
	char appid[256];

	lbmmon_attr_get_timestamp(AttributeBlock, &timestamp);
	memset(appid, 0, sizeof(appid));
	lbmmon_attr_get_appsourceid(AttributeBlock, appid, sizeof(appid));
	lbmmon_attr_get_ipv4sender(AttributeBlock, (lbm_uint_t *) &(addr.s_addr));
	fprintf(Outfile,
			"%d,\"%s\",\"%s\",\"%s\",",
			(int) timestamp,
			inet_ntoa(addr),
			appid,
			(Source == NULL) ? " " : Source);
}

void
rcv_statistics_cb(const void * AttributeBlock, const lbm_rcv_transport_stats_t * Statistics, void * ClientData)
{
	FILE * outfile = NULL;
	lbm_ulong_t source;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}

	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			outfile = RcvTCPFile;
			RcvTCPCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			switch (source)
			{
				case LBMMON_ATTR_SOURCE_IM:
					outfile = CtxIMRcvLBTRMFile;
					CtxIMRcvLBTRMCount++;
					break;

				default:
					outfile = RcvLBTRMFile;
					RcvLBTRMCount++;
					break;
			}
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			outfile = RcvLBTRUFile;
			RcvLBTRUCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			outfile = RcvLBTIPCFile;
			RcvLBTIPCCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			outfile = RcvLBTRDMAFile;
			RcvLBTRDMACount++;
			break;
	}
	write_common_data(outfile, AttributeBlock, Statistics->source);
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu\n",
					Statistics->transport.tcp.bytes_rcved,
					Statistics->transport.tcp.lbm_msgs_rcved,
					Statistics->transport.tcp.lbm_msgs_no_topic_rcved,
					Statistics->transport.tcp.lbm_reqs_rcved);
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtrm.msgs_rcved,
					Statistics->transport.lbtrm.bytes_rcved,
					Statistics->transport.lbtrm.nak_pckts_sent,
					Statistics->transport.lbtrm.naks_sent,
					Statistics->transport.lbtrm.lost,
					Statistics->transport.lbtrm.ncfs_ignored,
					Statistics->transport.lbtrm.ncfs_shed,
					Statistics->transport.lbtrm.ncfs_rx_delay,
					Statistics->transport.lbtrm.ncfs_unknown,
					Statistics->transport.lbtrm.nak_stm_min,
					Statistics->transport.lbtrm.nak_stm_mean,
					Statistics->transport.lbtrm.nak_stm_max,
					Statistics->transport.lbtrm.nak_tx_min,
					Statistics->transport.lbtrm.nak_tx_mean,
					Statistics->transport.lbtrm.nak_tx_max,
					Statistics->transport.lbtrm.duplicate_data,
					Statistics->transport.lbtrm.unrecovered_txw,
					Statistics->transport.lbtrm.unrecovered_tmo,
					Statistics->transport.lbtrm.lbm_msgs_rcved,
					Statistics->transport.lbtrm.lbm_msgs_no_topic_rcved,
					Statistics->transport.lbtrm.lbm_reqs_rcved,
					Statistics->transport.lbtrm.dgrams_dropped_size,
					Statistics->transport.lbtrm.dgrams_dropped_type,
					Statistics->transport.lbtrm.dgrams_dropped_version,
					Statistics->transport.lbtrm.dgrams_dropped_hdr,
					Statistics->transport.lbtrm.dgrams_dropped_other,
					Statistics->transport.lbtrm.out_of_order);
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtru.msgs_rcved,
					Statistics->transport.lbtru.bytes_rcved,
					Statistics->transport.lbtru.nak_pckts_sent,
					Statistics->transport.lbtru.naks_sent,
					Statistics->transport.lbtru.lost,
					Statistics->transport.lbtru.ncfs_ignored,
					Statistics->transport.lbtru.ncfs_shed,
					Statistics->transport.lbtru.ncfs_rx_delay,
					Statistics->transport.lbtru.ncfs_unknown,
					Statistics->transport.lbtru.nak_stm_min,
					Statistics->transport.lbtru.nak_stm_mean,
					Statistics->transport.lbtru.nak_stm_max,
					Statistics->transport.lbtru.nak_tx_min,
					Statistics->transport.lbtru.nak_tx_mean,
					Statistics->transport.lbtru.nak_tx_max,
					Statistics->transport.lbtru.duplicate_data,
					Statistics->transport.lbtru.unrecovered_txw,
					Statistics->transport.lbtru.unrecovered_tmo,
					Statistics->transport.lbtru.lbm_msgs_rcved,
					Statistics->transport.lbtru.lbm_msgs_no_topic_rcved,
					Statistics->transport.lbtru.lbm_reqs_rcved,
					Statistics->transport.lbtru.dgrams_dropped_size,
					Statistics->transport.lbtru.dgrams_dropped_type,
					Statistics->transport.lbtru.dgrams_dropped_version,
					Statistics->transport.lbtru.dgrams_dropped_hdr,
					Statistics->transport.lbtru.dgrams_dropped_sid,
					Statistics->transport.lbtru.dgrams_dropped_other);
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtipc.msgs_rcved,
					Statistics->transport.lbtipc.bytes_rcved,
					Statistics->transport.lbtipc.lbm_msgs_rcved,
					Statistics->transport.lbtipc.lbm_msgs_no_topic_rcved,
					Statistics->transport.lbtipc.lbm_reqs_rcved);
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtrdma.msgs_rcved,
					Statistics->transport.lbtrdma.bytes_rcved,
					Statistics->transport.lbtrdma.lbm_msgs_rcved,
					Statistics->transport.lbtrdma.lbm_msgs_no_topic_rcved,
					Statistics->transport.lbtrdma.lbm_reqs_rcved);
			break;
	}
	if (outfile != NULL)
	{
		fflush(outfile);
	}
}

void
src_statistics_cb(const void * AttributeBlock, const lbm_src_transport_stats_t * Statistics, void * ClientData)
{
	FILE * outfile = NULL;
	lbm_ulong_t source;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}

	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			outfile = SrcTCPFile;
			SrcTCPCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			switch (source)
			{
				case LBMMON_ATTR_SOURCE_IM:
					outfile = CtxIMSrcLBTRMFile;
					CtxIMSrcLBTRMCount++;
					break;

				default:
					outfile = SrcLBTRMFile;
					SrcLBTRMCount++;
					break;
			}
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			outfile = SrcLBTRUFile;
			SrcLBTRUCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			outfile = SrcLBTIPCFile;
			SrcLBTIPCCount++;
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			outfile = SrcLBTRDMAFile;
			SrcLBTRDMACount++;
			break;
	}
	write_common_data(outfile, AttributeBlock, Statistics->source);
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			fprintf(outfile,
					"%lu,%lu\n",
					Statistics->transport.tcp.num_clients,
					Statistics->transport.tcp.bytes_buffered);
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtrm.msgs_sent,
					Statistics->transport.lbtrm.bytes_sent,
					Statistics->transport.lbtrm.txw_msgs,
					Statistics->transport.lbtrm.txw_bytes,
					Statistics->transport.lbtrm.nak_pckts_rcved,
					Statistics->transport.lbtrm.naks_rcved,
					Statistics->transport.lbtrm.naks_ignored,
					Statistics->transport.lbtrm.naks_shed,
					Statistics->transport.lbtrm.naks_rx_delay_ignored,
					Statistics->transport.lbtrm.rxs_sent,
					Statistics->transport.lbtrm.rctlr_data_msgs,
					Statistics->transport.lbtrm.rctlr_rx_msgs,
					Statistics->transport.lbtrm.rx_bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			fprintf(outfile,
					"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					Statistics->transport.lbtru.msgs_sent,
					Statistics->transport.lbtru.bytes_sent,
					Statistics->transport.lbtru.nak_pckts_rcved,
					Statistics->transport.lbtru.naks_rcved,
					Statistics->transport.lbtru.naks_ignored,
					Statistics->transport.lbtru.naks_shed,
					Statistics->transport.lbtru.naks_rx_delay_ignored,
					Statistics->transport.lbtru.rxs_sent,
					Statistics->transport.lbtru.num_clients,
					Statistics->transport.lbtru.rx_bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			fprintf(outfile,
					"%lu,%lu,%lu\n",
					Statistics->transport.lbtipc.num_clients,
					Statistics->transport.lbtipc.msgs_sent,
					Statistics->transport.lbtipc.bytes_sent);
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			fprintf(outfile,
					"%lu,%lu,%lu\n",
					Statistics->transport.lbtrdma.num_clients,
					Statistics->transport.lbtrdma.msgs_sent,
					Statistics->transport.lbtrdma.bytes_sent);
			break;
	}
	if (outfile != NULL)
	{
		fflush(outfile);
	}
}

void
evq_statistics_cb(const void * AttributeBlock, const lbm_event_queue_stats_t * Statistics, void * ClientData)
{
	FILE * outfile = EvqFile;

	write_common_data(outfile, AttributeBlock, NULL);
	fprintf(outfile,
			"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			"%lu,%lu,%lu,%lu\n",
			Statistics->data_msgs,
			Statistics->data_msgs_tot,
			Statistics->data_msgs_svc_min,
			Statistics->data_msgs_svc_mean,
			Statistics->data_msgs_svc_max,
			Statistics->resp_msgs,
			Statistics->resp_msgs_tot,
			Statistics->resp_msgs_svc_min,
			Statistics->resp_msgs_svc_mean,
			Statistics->resp_msgs_svc_max,
			Statistics->topicless_im_msgs,
			Statistics->topicless_im_msgs_tot,
			Statistics->topicless_im_msgs_svc_min,
			Statistics->topicless_im_msgs_svc_mean,
			Statistics->topicless_im_msgs_svc_max,
			Statistics->wrcv_msgs,
			Statistics->wrcv_msgs_tot,
			Statistics->wrcv_msgs_svc_min,
			Statistics->wrcv_msgs_svc_mean,
			Statistics->wrcv_msgs_svc_max,
			Statistics->io_events,
			Statistics->io_events_tot,
			Statistics->io_events_svc_min,
			Statistics->io_events_svc_mean,
			Statistics->io_events_svc_max,
			Statistics->timer_events,
			Statistics->timer_events_tot,
			Statistics->timer_events_svc_min,
			Statistics->timer_events_svc_mean,
			Statistics->timer_events_svc_max,
			Statistics->source_events,
			Statistics->source_events_tot,
			Statistics->source_events_svc_min,
			Statistics->source_events_svc_mean,
			Statistics->source_events_svc_max,
			Statistics->unblock_events,
			Statistics->unblock_events_tot,
			Statistics->cancel_events,
			Statistics->cancel_events_tot,
			Statistics->cancel_events_svc_min,
			Statistics->cancel_events_svc_mean,
			Statistics->cancel_events_svc_max,
			Statistics->context_source_events,
			Statistics->context_source_events_tot,
			Statistics->context_source_events_svc_min,
			Statistics->context_source_events_svc_mean,
			Statistics->context_source_events_svc_max,
			Statistics->events,
			Statistics->events_tot,
			Statistics->age_min,
			Statistics->age_mean,
			Statistics->age_max);
	fflush(outfile);
	EvqCount++;
}

void
ctx_statistics_cb(const void * AttributeBlock, const lbm_context_stats_t * Statistics, void * ClientData)
{
	FILE * outfile = CtxFile;

	write_common_data(outfile, AttributeBlock, NULL);
	fprintf(outfile,
			"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
			Statistics->tr_dgrams_sent,
			Statistics->tr_bytes_sent,
			Statistics->tr_dgrams_rcved,
			Statistics->tr_bytes_rcved,
			Statistics->tr_dgrams_dropped_ver,
			Statistics->tr_dgrams_dropped_type,
			Statistics->tr_dgrams_dropped_malformed,
			Statistics->tr_dgrams_send_failed,
			Statistics->tr_src_topics,
			Statistics->tr_rcv_topics,
			Statistics->tr_rcv_unresolved_topics,
			Statistics->lbtrm_unknown_msgs_rcved,
			Statistics->lbtru_unknown_msgs_rcved,
			Statistics->send_blocked,
			Statistics->send_would_block,
			Statistics->resp_blocked,
			Statistics->resp_would_block,
			Statistics->uim_dup_msgs_rcved,
			Statistics->uim_msgs_no_stream_rcved);
	fflush(outfile);
	CtxCount++;
}

int main(int argc, char **argv)
{
	lbmmon_rctl_t * monctl;
	lbmmon_rctl_attr_t * attr;
	lbmmon_rcv_statistics_func_t rcv = { rcv_statistics_cb };
	lbmmon_src_statistics_func_t src = { src_statistics_cb };
	lbmmon_evq_statistics_func_t evq = { evq_statistics_cb };
	lbmmon_ctx_statistics_func_t ctx = { ctx_statistics_cb };
	int rc;
	int c;
	int errflag = 0;
	char * transport_options = NULL;
	char transport_options_string[1024];

#ifdef _WIN32
	{
		WSADATA wsadata;
		int status;

		/* Windows socket setup code */
		if ((status = WSAStartup(MAKEWORD(2,2), &wsadata)) != 0)
		{
			fprintf(stderr, "%s: WSA startup error - %d\n", argv[0], status);
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

	memset(transport_options_string, 0, sizeof(transport_options_string));

	while ((c = getopt(argc, argv, OptionString)) != EOF)
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

			case 't':
				if (strlen(transport_options_string) > 0)
				{
					strncat(transport_options_string,
							";",
							sizeof(transport_options_string) - strlen(transport_options_string) - 1);
				}
				strncat(transport_options_string,
						"topic=",
						sizeof(transport_options_string) - strlen(transport_options_string) - 1);
				strncat(transport_options_string,
						optarg,
						sizeof(transport_options_string) - strlen(transport_options_string) - 1);
				break;

			default:
				errflag++;
				break;
		}
	}

	if (errflag != 0)
	{
		fprintf(stderr, "%s\n%s\n%s", argv[0], lbm_version(), Usage);
		exit(1);
	}

	SrcTCPFile = fopen("src_tcp.dat", "w");
	fprintf(SrcTCPFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(SrcTCPFile, "\"Clients\",\"Bytes Buffered\"\n");

	RcvTCPFile = fopen("rcv_tcp.dat", "w");
	fprintf(RcvTCPFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(RcvTCPFile, "\"Bytes Received\",\"Messages Received\",\"Messages Received with No Topic\",\"Requests Received\"\n");

	SrcLBTRMFile = fopen("src_lbtrm.dat", "w");
	fprintf(SrcLBTRMFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(SrcLBTRMFile, "\"Messages Sent\",\"Bytes Sent\",\"Messages in Transmission Window\",\"Bytes in Transmission Window\",");
	fprintf(SrcLBTRMFile, "\"NAK Packets Received\",\"NAKs Received\",\"NAKs Ignored\",\"NAKs Shed\",");
	fprintf(SrcLBTRMFile, "\"NAKs Ignored due to Retransmit Delay\",\"Retransmission Datagrams Sent\",\"Data Messages Queued by Rate Control\",");
	fprintf(SrcLBTRMFile, "\"Retransmission Messages Queued by Rate Control\",\"Retransmission Bytes Sent\"\n");

	RcvLBTRMFile = fopen("rcv_lbtrm.dat", "w");
	fprintf(RcvLBTRMFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(RcvLBTRMFile, "\"LBT-RM Datagrams Received\",\"LBT-RM Bytes Received\",\"NAK Packets Sent\",\"NAKs Sent\",");
	fprintf(RcvLBTRMFile, "\"Lost Messages\",\"NCFs Received: Ignored\",\"NCFs Received: Shed\",\"NCFs Received: Retransmit Delay\",");
	fprintf(RcvLBTRMFile, "\"NCFs Received: Unknown\",\"Minimum Loss Recovery Time\",\"Mean Loss Recovery Time\",\"Maximim Loss Recovery Time\",");
	fprintf(RcvLBTRMFile, "\"Minimum Transmissions per NAK\",\"Mean Transmissions per NAK\",\"Maximum Transmissions per NAK\",");
	fprintf(RcvLBTRMFile, "\"Duplicate Datagrams Received\",\"Unrecoverable Messages: Window Advance\",\"Unrecoverable Messages: NAK Generation Expired\",");
	fprintf(RcvLBTRMFile, "\"LBM Messages Received\",\"LBM Messages Received with Uninteresting Topic\",\"Requests Received\",");
	fprintf(RcvLBTRMFile, "\"Datagrams Dropped (Size)\",\"Datagrams Dropped (Type)\",\"Datagrams Dropped (Version)\",");
	fprintf(RcvLBTRMFile, "\"Datagrams Dropped (Header)\",\"Datagrams Dropped (SID)\",\"Datagrams Dropped (Other)\",");
	fprintf(RcvLBTRMFile, "\"Datagrams Received Out Of Order\"\n");

	SrcLBTRUFile = fopen("src_lbtru.dat", "w");
	fprintf(SrcLBTRUFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(SrcLBTRUFile, "\"Messages Sent\",\"Bytes Sent\",\"NAK Packets Received\",\"NAKs Received\",");
	fprintf(SrcLBTRUFile, "\"NAKs Ignored\",\"NAKs Shed\",\"NAKs Ignored due to Retransmit Delay\",\"Retransmission Datagrams Sent\",");
	fprintf(SrcLBTRUFile, "\"Clients\",\"Retransmission Bytes Sent\"\n");

	RcvLBTRUFile = fopen("rcv_lbtru.dat", "w");
	fprintf(RcvLBTRUFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(RcvLBTRUFile, "\"LBT-RU Datagrams Received\",\"LBT-RU Bytes Received\",\"NAK Packets Sent\",\"NAKs Sent\",");
	fprintf(RcvLBTRUFile, "\"Lost Datagrams\",\"NCFs Received: Ignored\",\"NCFs Received: Shed\",\"NCFs Received: Retransmit Delay\",");
	fprintf(RcvLBTRUFile, "\"NCFs Received: Unknown\",\"Minimum Loss Recovery Time\",\"Mean Loss Recovery Time\",\"Maximim Loss Recovery Time\",");
	fprintf(RcvLBTRUFile, "\"Minimum Transmissions per NAK\",\"Mean Transmissions per NAK\",\"Maximum Transmissions per NAK\",");
	fprintf(RcvLBTRUFile, "\"Duplicate Datagrams Received\",\"Unrecoverable Messages: Window Advance\",\"Unrecoverable Messages: NAK Generation Expired\",");
	fprintf(RcvLBTRMFile, "\"LBM Messages Received\",\"LBM Messages Received with Uninteresting Topic\",\"Requests Received\",");
	fprintf(RcvLBTRMFile, "\"Datagrams Dropped (Size)\",\"Datagrams Dropped (Type)\",\"Datagrams Dropped (Version)\",");
	fprintf(RcvLBTRMFile, "\"Datagrams Dropped (Header)\",\"Datagrams Dropped (SID)\",\"Datagrams Dropped (Other)\"\n");

	SrcLBTIPCFile = fopen("src_lbtipc.dat", "w");
	fprintf(SrcLBTIPCFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(SrcLBTIPCFile, "\"Clients\",\"Messages Sent\",\"Bytes Sent\"\n");

	RcvLBTIPCFile = fopen("rcv_lbtipc.dat", "w");
	fprintf(RcvLBTIPCFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(RcvLBTIPCFile, "\"Messages Received\",\"Bytes Received\",\"LBM Messages Received\",\"Messages Received with Uninteresting Topic\",\"Requests Received\"\n");

	SrcLBTRDMAFile = fopen("src_lbtrdma.dat", "w");
	fprintf(SrcLBTRDMAFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(SrcLBTRDMAFile, "\"Clients\",\"Messages Sent\",\"Bytes Sent\"\n");

	RcvLBTRDMAFile = fopen("rcv_lbtrdma.dat", "w");
	fprintf(RcvLBTRDMAFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(RcvLBTRDMAFile, "\"Messages Received\",\"Bytes Received\",\"LBM Messages Received\",\"Messages Received with No Topic\",\"Requests Received\"\n");

	EvqFile = fopen("evq.dat", "w");
	fprintf(EvqFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(EvqFile, "\"Data messages enqueued\",\"Total data messages enqueued\",\"Data message service minimum time\",\"Data message service mean time\",\"Data message service maximum time\",");
	fprintf(EvqFile, "\"Responses enqueued\",\"Total responses enqueued\",\"Request service minimum time\",\"Request service mean time\",\"Request service maximum time\",");
	fprintf(EvqFile, "\"Topicless immediate messages enqueued\",\"Total topicless immediate messages enqueued\",\"Topicless immediate message service minimum time\",\"Topicless immediate message service mean time\",\"Topicless immediate message service maximum time\",");
	fprintf(EvqFile, "\"Wildcard messages enqueued\",\"Total wildcard messages enqueued\",\"Wildcard message service minimum time\",\"Wildcard message service mean time\",\"Wildcard message service maximum time\",");
	fprintf(EvqFile, "\"I/O events enqueued\",\"Total I/O events enqueued\",\"I/O event service minimum time\",\"I/O event service mean time\",\"I/O event service maximum time\",");
	fprintf(EvqFile, "\"Timer events enqueued\",\"Total timer events enqueued\",\"Timer event service minimum time\",\"Timer event service mean time\",\"Timer event service maximum time\",");
	fprintf(EvqFile, "\"Source events enqueued\",\"Total source events enqueued\",\"Source event service minimum time\",\"Source event service mean time\",\"Source event service maximum time\",");
	fprintf(EvqFile, "\"Unblock events enqueued\",\"Total unblock events enqueued\",");
	fprintf(EvqFile, "\"Cancel events enqueued\",\"Total cancel events enqueued\",\"Cancel event service minimum time\",\"Cancel event service mean time\",\"Cancel event service maximum time\",");
	fprintf(EvqFile, "\"Context source events enqueued\",\"Total context source events enqueued\",\"Context source event service minimum time\",\"Context source event service mean time\",\"Context source event service maximum time\",");
	fprintf(EvqFile, "\"Events enqueued\",\"Total events enqueued\",\"Event latency minimum time\",\"Event latency mean time\",\"Event latency maximum time\"\n");

	CtxFile = fopen("ctx.dat", "w");
	fprintf(CtxFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(CtxFile, "\"Topic resolution datagrams sent\",\"Topic resolution bytes sent\",\"Topic resolution datagrams received\",\"Topic resolution bytes received\",");
	fprintf(CtxFile, "\"Topic resolution datagrams dropped (version)\",\"Topic resolution datagrams dropped (type)\",\"Topic resolution datagrams dropped (malformed)\",\"Topic resolution datagram send failures\",");
	fprintf(CtxFile, "\"Source topics\",\"Receive topics\",\"Unresolved receive topics\",\"Unknown LBT-RM datagrams received\",\"Unknown LBT-RU datagrams received\",");
	fprintf(CtxFile, "\"Message sends which blocked\",\"Message sends which would block\",\"Response sends which blocked\",\"Response sends which would block\"\n");

	CtxIMSrcLBTRMFile = fopen("ctx_im_src_lbtrm.dat", "w");
	fprintf(CtxIMSrcLBTRMFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(CtxIMSrcLBTRMFile, "\"Messages Sent\",\"Bytes Sent\",\"Messages in Transmission Window\",\"Bytes in Transmission Window\",");
	fprintf(CtxIMSrcLBTRMFile, "\"NAK Packets Received\",\"NAKs Received\",\"NAKs Ignored\",\"NAKs Shed\",");
	fprintf(CtxIMSrcLBTRMFile, "\"NAKs Ignored due to Retransmit Delay\",\"Retransmission Datagrams Sent\",\"Data Messages Queued by Rate Control\",");
	fprintf(CtxIMSrcLBTRMFile, "\"Retransmission Messages Queued by Rate Control\",\"Retransmission Bytes Sent\"\n");

	CtxIMRcvLBTRMFile = fopen("ctx_im_rcv_lbtrm.dat", "w");
	fprintf(CtxIMRcvLBTRMFile, "\"Timestamp\",\"Address\",\"ApplicationID\",\"Source\",");
	fprintf(CtxIMRcvLBTRMFile, "\"LBT-RM Messages Received\",\"LBT-RM Bytes Received\",\"NAK Packets Sent\",\"NAKs Sent\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Lost Messages\",\"NCFs Received: Ignored\",\"NCFs Received: Shed\",\"NCFs Received: Retransmit Delay\",");
	fprintf(CtxIMRcvLBTRMFile, "\"NCFs Received: Unknown\",\"Minimum Loss Recovery Time\",\"Mean Loss Recovery Time\",\"Maximim Loss Recovery Time\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Minimum Transmissions per NAK\",\"Mean Transmissions per NAK\",\"Maximum Transmissions per NAK\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Duplicate Messages Received\",\"Unrecoverable Messages: Window Advance\",\"Unrecoverable Messages: NAK Generation Expired\",");
	fprintf(CtxIMRcvLBTRMFile, "\"LBM Messages Received\",\"LBM Messages Received with Uninteresting Topic\",\"Requests Received\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Datagrams Dropped (Size)\",\"Datagrams Dropped (Type)\",\"Datagrams Dropped (Version)\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Datagrams Dropped (Header)\",\"Datagrams Dropped (SID)\",\"Datagrams Dropped (Other)\",");
	fprintf(CtxIMRcvLBTRMFile, "\"Datagrams Received Out Of Order\"\n");

	if (strlen(transport_options_string) > 0)
	{
		transport_options = transport_options_string;
	}

	rc = lbmmon_rctl_attr_create(&attr);
	if (attr == NULL)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_create() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_RECEIVER_CALLBACK, (void *) &rcv, sizeof(rcv));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_SOURCE_CALLBACK, (void *) &src, sizeof(src));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_EVENT_QUEUE_CALLBACK, (void *) &evq, sizeof(evq));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_attr_setopt(attr, LBMMON_RCTL_CONTEXT_CALLBACK, (void *) &ctx, sizeof(ctx));
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_attr_setopt() failed, %s\n", lbmmon_errmsg());
		exit(1);
	}
	rc = lbmmon_rctl_create(&monctl, lbmmon_format_csv_module(), NULL, lbmmon_transport_lbm_module(), (void *) transport_options, attr, NULL);
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_create() failed\n");
		exit(1);
	}
	lbmmon_rctl_attr_delete(attr);

	signal(SIGINT, SignalHandler);
	while (Terminate == 0)
	{
		SLEEP_SEC(2);
		printf("Source stats received... TCP: %u, LBTRU: %u, LBTRM: %u, LBTIPC: %u LBTRDMA: %u\n",
			   SrcTCPCount, SrcLBTRUCount, SrcLBTRMCount, SrcLBTIPCCount, SrcLBTRDMACount);
		printf("Receiver stats received... TCP: %u, LBTRU: %u, LBTRM: %u, LBTIPC: %u LBTRDMA: %u\n",
			   RcvTCPCount, RcvLBTRUCount, RcvLBTRMCount, RcvLBTIPCCount, RcvLBTRDMACount);
		printf("Event queue stats received: %u\n",
			   EvqCount);
		printf("Context stats received: %u\n",
			   CtxCount);
		printf("Context IM source stats received... LBTRM: %u\n",
			   CtxIMSrcLBTRMCount);
		printf("Context IM receiver stats received... LBTRM: %u\n",
			   CtxIMRcvLBTRMCount);
	}

	lbmmon_rctl_destroy(monctl);

	fclose(RcvTCPFile);
	fclose(SrcTCPFile);
	fclose(RcvLBTRUFile);
	fclose(SrcLBTRUFile);
	fclose(RcvLBTRMFile);
	fclose(SrcLBTRMFile);
	fclose(RcvLBTIPCFile);
	fclose(SrcLBTIPCFile);
	fclose(RcvLBTRDMAFile);
	fclose(SrcLBTRDMAFile);
	fclose(EvqFile);
	fclose(CtxFile);
	fclose(CtxIMSrcLBTRMFile);
	fclose(CtxIMRcvLBTRMFile);

	return (0);
}

