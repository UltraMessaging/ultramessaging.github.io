/*
  lbmmonudp.c: example LBM monitoring application.

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
#include <errno.h>
#include <signal.h>
#include <limits.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <sys/timeb.h>
	#define strcasecmp stricmp
	#define snprintf _snprintf
	typedef int ssize_t;
#else
	#include <unistd.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/time.h>
	#include <sys/socket.h>
	#include <signal.h>
	#if defined(__TANDEM)
		#include <strings.h>
	#endif
#endif
#if defined(__VMS)
	typedef int socklen_t;
#endif
#include "replgetopt.h"
#include <lbm/lbm.h>
#include <lbm/lbmmon.h>
#include "monmodopts.h"

#ifdef _WIN32
	#define LBMMONUDP_INVALID_HANDLE INVALID_SOCKET
	#define LBMMONUDP_SOCKET_ERROR SOCKET_ERROR
#else
	#define LBMMONUDP_INVALID_HANDLE -1
	#define LBMMONUDP_SOCKET_ERROR -1
#endif
#ifndef INADDR_NONE
	#define INADDR_NONE ((in_addr_t) 0xffffffff)
#endif

static const char *rcsid_example_lbmmonudp = "$Id: //UMprod/REL_5_3_6/29West/lbm/src/example/lbmmonudp.c#2 $";

#if defined(_WIN32)
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

const char Purpose[] = "Purpose: Receive LBM statistics and forward as CSV over UDP.";
const char Usage[] =
"Usage: %s [options]\n"
"Available options:\n"
"  -3, --force-32bit          force all data values to fit within 32 bits\n"
"                             default is to use native data size\n"
"                             applies only to 64-bit platforms\n"
"  -a, --address=IP           send CSV data to unicast address IP\n"
"  -b, --broadcast=IP         send CSV data to broadcast address IP\n"
"  -f, --format=FMT           use monitor format module FMT\n"
"                             FMT may be `csv'\n"
"      --format-opts=OPTS     use OPTS as format module options\n"
"  -h, --help                 display this help and exit\n"
"  -i, --interface=IP         send multicast via interface IP\n"
"  -m, --multicast=GRP        send CSV data to multicast group GRP\n"
"  -p, --port=NUM             send CSV data on UDP port NUM\n"
"                             default is port 1234\n"
"  -t, --transport=TRANS      use monitor transport module TRANS\n"
"                             TRANS may be `lbm' or `udp', default is `lbm'\n"
"      --transport-opts=OPTS  use OPTS as transport module options\n"
"  -T, --ttl=NUM              send multicast with TTL NUM\n"
"                             default is 1\n"
MONMODULEOPTS_RECEIVER;

const char OptionString[] = "3a:b:f:hi:m:p:t:T:";
#define OPTION_MONITOR_TRANSPORT_OPTS 0
#define OPTION_MONITOR_FORMAT_OPTS 1
const struct option OptionTable[] =
{
	{ "force-32bit", no_argument, NULL, '3' },
	{ "address", required_argument, NULL, 'a' },
	{ "broadcast", required_argument, NULL, 'b' },
	{ "format", required_argument, NULL, 'f' },
	{ "format-opts", required_argument, NULL, OPTION_MONITOR_FORMAT_OPTS },
	{ "help", no_argument, NULL, 'h' },
	{ "interface", required_argument, NULL, 'i' },
	{ "multicast", required_argument, NULL, 'm' },
	{ "port", required_argument, NULL, 'p' },
	{ "transport", required_argument, NULL, 't' },
	{ "transport-opts", required_argument, NULL, OPTION_MONITOR_TRANSPORT_OPTS },
	{ "ttl", required_argument, NULL, 'T' },
	{ NULL, 0, NULL, 0 }
};

/* Data version constants */
#define LBMMONUDP_TCP_SRC_V1 1
#define LBMMONUDP_TCP_RCV_V1 2
#define LBMMONUDP_LBTRM_SRC_V1 3
#define LBMMONUDP_LBTRM_RCV_V1 4
#define LBMMONUDP_LBTRU_SRC_V1 5
#define LBMMONUDP_LBTRU_RCV_V1 6
#define LBMMONUDP_EVQ_V1 7
#define LBMMONUDP_CTX_V1 8
#define LBMMONUDP_CTX_IM_LBTRM_SRC_V1 9
#define LBMMONUDP_CTX_IM_LBTRM_RCV_V1 10
#define LBMMONUDP_LBTIPC_SRC_V1 11
#define LBMMONUDP_LBTIPC_RCV_V1 12
#define LBMMONUDP_LBTRDMA_SRC_V1 13
#define LBMMONUDP_LBTRDMA_RCV_V1 14

/* Current data versions */
#define LBMMONUDP_TCP_SRC_VER LBMMONUDP_TCP_SRC_V1
#define LBMMONUDP_TCP_RCV_VER LBMMONUDP_TCP_RCV_V1
#define LBMMONUDP_LBTRM_SRC_VER LBMMONUDP_LBTRM_SRC_V1
#define LBMMONUDP_LBTRM_RCV_VER LBMMONUDP_LBTRM_RCV_V1
#define LBMMONUDP_LBTRU_SRC_VER LBMMONUDP_LBTRU_SRC_V1
#define LBMMONUDP_LBTRU_RCV_VER LBMMONUDP_LBTRU_RCV_V1
#define LBMMONUDP_EVQ_VER LBMMONUDP_EVQ_V1
#define LBMMONUDP_CTX_VER LBMMONUDP_CTX_V1
#define LBMMONUDP_CTX_IM_LBTRM_SRC_VER LBMMONUDP_CTX_IM_LBTRM_SRC_V1
#define LBMMONUDP_CTX_IM_LBTRM_RCV_VER LBMMONUDP_CTX_IM_LBTRM_RCV_V1
#define LBMMONUDP_LBTIPC_SRC_VER LBMMONUDP_LBTIPC_SRC_V1
#define LBMMONUDP_LBTIPC_RCV_VER LBMMONUDP_LBTIPC_RCV_V1
#define LBMMONUDP_LBTRDMA_SRC_VER LBMMONUDP_LBTRDMA_SRC_V1
#define LBMMONUDP_LBTRDMA_RCV_VER LBMMONUDP_LBTRDMA_RCV_V1

/* Default values */
#define DEFAULT_TTL 1
#define DEFAULT_PORT 1234

/* Global variables */
unsigned int Terminate = 0;
unsigned int Force32Bit = 0;
struct sockaddr_in Peer;
unsigned long int ValueMask = ULONG_MAX;
#ifdef _WIN32
SOCKET Socket;
#else
int Socket;
#endif
unsigned long int MessagesReceived = 0;
unsigned long int MessagesSent = 0;

static const char *
last_socket_error(void)
{
	static char message[512];
#ifdef _WIN32
	snprintf(message, sizeof(message), "error %d", WSAGetLastError());
#else
	snprintf(message, sizeof(message), "error %d, %s", errno, strerror(errno));
#endif
	return (message);
}

void
SignalHandler(int signo)
{
	Terminate = 1;
}

unsigned long int
preprocess_value(unsigned long int Value)
{
	return (Value & ValueMask);
}

void
send_packet(const char * Buffer, size_t Length)
{
	int rc;

	if ((Buffer == NULL) || (Length == 0))
	{
		return;
	}
	rc = sendto(Socket, Buffer, Length, 0, (struct sockaddr *) &Peer, sizeof(Peer));
	if (rc == LBMMONUDP_SOCKET_ERROR)
	{
		fprintf(stderr, "sendto() failed, %s\n", last_socket_error());
	}
	else
	{
		++MessagesSent;
	}
}

void
format_common_data(char * Buffer, size_t Size, unsigned int Format, const void * AttributeBlock, const char * Source)
{
	time_t timestamp = 0;
	struct in_addr addr;
	char appid[256];

	lbmmon_attr_get_timestamp(AttributeBlock, &timestamp);
	memset(appid, 0, sizeof(appid));
	lbmmon_attr_get_appsourceid(AttributeBlock, appid, sizeof(appid));
	lbmmon_attr_get_ipv4sender(AttributeBlock, (lbm_uint_t *) &(addr.s_addr));
	snprintf(Buffer,
			Size,
			"%u,%d,\"%s\",\"%s\",\"%s\",",
			Format,
			(int) timestamp,
			inet_ntoa(addr),
			appid,
			(Source == NULL) ? " " : Source);
}

void
rcv_statistics_cb(const void * AttributeBlock, const lbm_rcv_transport_stats_t * Statistics, void * ClientData)
{
	unsigned int format;
	char buffer[4096];
	char * ptr;
	size_t remaining_len;
	lbm_ulong_t source;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}

	memset(buffer, 0, sizeof(buffer));
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			format = LBMMONUDP_TCP_RCV_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			switch (source)
			{
				case LBMMON_ATTR_SOURCE_IM:
					format = LBMMONUDP_CTX_IM_LBTRM_RCV_VER;
					break;
				default:
					format = LBMMONUDP_LBTRM_RCV_VER;
					break;
			}
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			format = LBMMONUDP_LBTRU_RCV_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			format = LBMMONUDP_LBTIPC_RCV_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			format = LBMMONUDP_LBTRDMA_RCV_VER;
			break;

		default:
			return;
	}
	++MessagesReceived;
	format_common_data(buffer, sizeof(buffer), format, AttributeBlock, Statistics->source);
	ptr = &(buffer[strlen(buffer)]);
	remaining_len = sizeof(buffer) - strlen(buffer);
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.tcp.bytes_rcved),
					 preprocess_value(Statistics->transport.tcp.lbm_msgs_rcved),
					 preprocess_value(Statistics->transport.tcp.lbm_msgs_no_topic_rcved),
					 preprocess_value(Statistics->transport.tcp.lbm_reqs_rcved));
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtrm.msgs_rcved),
					 preprocess_value(Statistics->transport.lbtrm.bytes_rcved),
					 preprocess_value(Statistics->transport.lbtrm.nak_pckts_sent),
					 preprocess_value(Statistics->transport.lbtrm.naks_sent),
					 preprocess_value(Statistics->transport.lbtrm.lost),
					 preprocess_value(Statistics->transport.lbtrm.ncfs_ignored),
					 preprocess_value(Statistics->transport.lbtrm.ncfs_shed),
					 preprocess_value(Statistics->transport.lbtrm.ncfs_rx_delay),
					 preprocess_value(Statistics->transport.lbtrm.ncfs_unknown),
					 preprocess_value(Statistics->transport.lbtrm.nak_stm_min),
					 preprocess_value(Statistics->transport.lbtrm.nak_stm_mean),
					 preprocess_value(Statistics->transport.lbtrm.nak_stm_max),
					 preprocess_value(Statistics->transport.lbtrm.nak_tx_min),
					 preprocess_value(Statistics->transport.lbtrm.nak_tx_mean),
					 preprocess_value(Statistics->transport.lbtrm.nak_tx_max),
					 preprocess_value(Statistics->transport.lbtrm.duplicate_data),
					 preprocess_value(Statistics->transport.lbtrm.unrecovered_txw),
					 preprocess_value(Statistics->transport.lbtrm.unrecovered_tmo),
					 preprocess_value(Statistics->transport.lbtrm.lbm_msgs_rcved),
					 preprocess_value(Statistics->transport.lbtrm.lbm_msgs_no_topic_rcved),
					 preprocess_value(Statistics->transport.lbtrm.lbm_reqs_rcved),
					 preprocess_value(Statistics->transport.lbtrm.dgrams_dropped_size),
					 preprocess_value(Statistics->transport.lbtrm.dgrams_dropped_type),
					 preprocess_value(Statistics->transport.lbtrm.dgrams_dropped_version),
					 preprocess_value(Statistics->transport.lbtrm.dgrams_dropped_hdr),
					 preprocess_value(Statistics->transport.lbtrm.dgrams_dropped_other),
					 preprocess_value(Statistics->transport.lbtrm.out_of_order));
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtru.msgs_rcved),
					 preprocess_value(Statistics->transport.lbtru.bytes_rcved),
					 preprocess_value(Statistics->transport.lbtru.nak_pckts_sent),
					 preprocess_value(Statistics->transport.lbtru.naks_sent),
					 preprocess_value(Statistics->transport.lbtru.lost),
					 preprocess_value(Statistics->transport.lbtru.ncfs_ignored),
					 preprocess_value(Statistics->transport.lbtru.ncfs_shed),
					 preprocess_value(Statistics->transport.lbtru.ncfs_rx_delay),
					 preprocess_value(Statistics->transport.lbtru.ncfs_unknown),
					 preprocess_value(Statistics->transport.lbtru.nak_stm_min),
					 preprocess_value(Statistics->transport.lbtru.nak_stm_mean),
					 preprocess_value(Statistics->transport.lbtru.nak_stm_max),
					 preprocess_value(Statistics->transport.lbtru.nak_tx_min),
					 preprocess_value(Statistics->transport.lbtru.nak_tx_mean),
					 preprocess_value(Statistics->transport.lbtru.nak_tx_max),
					 preprocess_value(Statistics->transport.lbtru.duplicate_data),
					 preprocess_value(Statistics->transport.lbtru.unrecovered_txw),
					 preprocess_value(Statistics->transport.lbtru.unrecovered_tmo),
					 preprocess_value(Statistics->transport.lbtru.lbm_msgs_rcved),
					 preprocess_value(Statistics->transport.lbtru.lbm_msgs_no_topic_rcved),
					 preprocess_value(Statistics->transport.lbtru.lbm_reqs_rcved),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_size),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_type),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_version),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_hdr),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_sid),
					 preprocess_value(Statistics->transport.lbtru.dgrams_dropped_hdr));
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtipc.msgs_rcved),
					 preprocess_value(Statistics->transport.lbtipc.bytes_rcved),
					 preprocess_value(Statistics->transport.lbtipc.lbm_msgs_rcved),
					 preprocess_value(Statistics->transport.lbtipc.lbm_msgs_no_topic_rcved),
					 preprocess_value(Statistics->transport.lbtipc.lbm_reqs_rcved));
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtrdma.msgs_rcved),
					 preprocess_value(Statistics->transport.lbtrdma.bytes_rcved),
					 preprocess_value(Statistics->transport.lbtrdma.lbm_msgs_rcved),
					 preprocess_value(Statistics->transport.lbtrdma.lbm_msgs_no_topic_rcved),
					 preprocess_value(Statistics->transport.lbtrdma.lbm_reqs_rcved));
			break;
	}
	send_packet(buffer, strlen(buffer));
}

void
src_statistics_cb(const void * AttributeBlock, const lbm_src_transport_stats_t * Statistics, void * ClientData)
{
	unsigned int format;
	char buffer[4096];
	char * ptr;
	size_t remaining_len;
	lbm_ulong_t source;

	if (lbmmon_attr_get_source(AttributeBlock, &source) != 0)
	{
		source = LBMMON_ATTR_SOURCE_NORMAL;
	}

	memset(buffer, 0, sizeof(buffer));
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			format = LBMMONUDP_TCP_SRC_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			switch (source)
			{
				case LBMMON_ATTR_SOURCE_IM:
					format = LBMMONUDP_CTX_IM_LBTRM_SRC_VER;
					break;
	
				default:
					format = LBMMONUDP_LBTRM_SRC_VER;
					break;
			}
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			format = LBMMONUDP_LBTRU_SRC_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			format = LBMMONUDP_LBTIPC_SRC_VER;
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			format = LBMMONUDP_LBTRDMA_SRC_VER;
			break;

		default:
			return;
	}
	++MessagesReceived;
	format_common_data(buffer, sizeof(buffer), format, AttributeBlock, Statistics->source);
	ptr = &(buffer[strlen(buffer)]);
	remaining_len = sizeof(buffer) - strlen(buffer);
	switch (Statistics->type)
	{
		case LBM_TRANSPORT_STAT_TCP:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu\n",
					 preprocess_value(Statistics->transport.tcp.num_clients),
					 preprocess_value(Statistics->transport.tcp.bytes_buffered));
			break;

		case LBM_TRANSPORT_STAT_LBTRM:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtrm.msgs_sent),
					 preprocess_value(Statistics->transport.lbtrm.bytes_sent),
					 preprocess_value(Statistics->transport.lbtrm.txw_msgs),
					 preprocess_value(Statistics->transport.lbtrm.txw_bytes),
					 preprocess_value(Statistics->transport.lbtrm.nak_pckts_rcved),
					 preprocess_value(Statistics->transport.lbtrm.naks_rcved),
					 preprocess_value(Statistics->transport.lbtrm.naks_ignored),
					 preprocess_value(Statistics->transport.lbtrm.naks_shed),
					 preprocess_value(Statistics->transport.lbtrm.naks_rx_delay_ignored),
					 preprocess_value(Statistics->transport.lbtrm.rxs_sent),
					 preprocess_value(Statistics->transport.lbtrm.rctlr_data_msgs),
					 preprocess_value(Statistics->transport.lbtrm.rctlr_rx_msgs),
					 preprocess_value(Statistics->transport.lbtrm.rx_bytes_sent));
			break;

		case LBM_TRANSPORT_STAT_LBTRU:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtru.msgs_sent),
					 preprocess_value(Statistics->transport.lbtru.bytes_sent),
					 preprocess_value(Statistics->transport.lbtru.nak_pckts_rcved),
					 preprocess_value(Statistics->transport.lbtru.naks_rcved),
					 preprocess_value(Statistics->transport.lbtru.naks_ignored),
					 preprocess_value(Statistics->transport.lbtru.naks_shed),
					 preprocess_value(Statistics->transport.lbtru.naks_rx_delay_ignored),
					 preprocess_value(Statistics->transport.lbtru.rxs_sent),
					 preprocess_value(Statistics->transport.lbtru.num_clients),
					 preprocess_value(Statistics->transport.lbtru.rx_bytes_sent));
			break;

		case LBM_TRANSPORT_STAT_LBTIPC:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtipc.num_clients),
					 preprocess_value(Statistics->transport.lbtipc.msgs_sent),
					 preprocess_value(Statistics->transport.lbtipc.bytes_sent));
			break;

		case LBM_TRANSPORT_STAT_LBTRDMA:
			snprintf(ptr,
					 remaining_len,
					 "%lu,%lu,%lu\n",
					 preprocess_value(Statistics->transport.lbtrdma.num_clients),
					 preprocess_value(Statistics->transport.lbtrdma.msgs_sent),
					 preprocess_value(Statistics->transport.lbtrdma.bytes_sent));
			break;
	}
	send_packet(buffer, strlen(buffer));
}

void
evq_statistics_cb(const void * AttributeBlock, const lbm_event_queue_stats_t * Statistics, void * ClientData)
{
	unsigned int format;
	char buffer[4096];
	char * ptr;
	size_t remaining_len;

	memset(buffer, 0, sizeof(buffer));
	format = LBMMONUDP_EVQ_VER;
	++MessagesReceived;
	format_common_data(buffer, sizeof(buffer), format, AttributeBlock, NULL);
	ptr = &(buffer[strlen(buffer)]);
	remaining_len = sizeof(buffer) - strlen(buffer);
	snprintf(ptr,
			 remaining_len,
			 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
			 "%lu,%lu,%lu,%lu\n",
			 preprocess_value(Statistics->data_msgs),
			 preprocess_value(Statistics->data_msgs_tot),
			 preprocess_value(Statistics->data_msgs_svc_min),
			 preprocess_value(Statistics->data_msgs_svc_mean),
			 preprocess_value(Statistics->data_msgs_svc_max),
			 preprocess_value(Statistics->resp_msgs),
			 preprocess_value(Statistics->resp_msgs_tot),
			 preprocess_value(Statistics->resp_msgs_svc_min),
			 preprocess_value(Statistics->resp_msgs_svc_mean),
			 preprocess_value(Statistics->resp_msgs_svc_max),
			 preprocess_value(Statistics->topicless_im_msgs),
			 preprocess_value(Statistics->topicless_im_msgs_tot),
			 preprocess_value(Statistics->topicless_im_msgs_svc_min),
			 preprocess_value(Statistics->topicless_im_msgs_svc_mean),
			 preprocess_value(Statistics->topicless_im_msgs_svc_max),
			 preprocess_value(Statistics->wrcv_msgs),
			 preprocess_value(Statistics->wrcv_msgs_tot),
			 preprocess_value(Statistics->wrcv_msgs_svc_min),
			 preprocess_value(Statistics->wrcv_msgs_svc_mean),
			 preprocess_value(Statistics->wrcv_msgs_svc_max),
			 preprocess_value(Statistics->io_events),
			 preprocess_value(Statistics->io_events_tot),
			 preprocess_value(Statistics->io_events_svc_min),
			 preprocess_value(Statistics->io_events_svc_mean),
			 preprocess_value(Statistics->io_events_svc_max),
			 preprocess_value(Statistics->timer_events),
			 preprocess_value(Statistics->timer_events_tot),
			 preprocess_value(Statistics->timer_events_svc_min),
			 preprocess_value(Statistics->timer_events_svc_mean),
			 preprocess_value(Statistics->timer_events_svc_max),
			 preprocess_value(Statistics->source_events),
			 preprocess_value(Statistics->source_events_tot),
			 preprocess_value(Statistics->source_events_svc_min),
			 preprocess_value(Statistics->source_events_svc_mean),
			 preprocess_value(Statistics->source_events_svc_max),
			 preprocess_value(Statistics->unblock_events),
			 preprocess_value(Statistics->unblock_events_tot),
			 preprocess_value(Statistics->cancel_events),
			 preprocess_value(Statistics->cancel_events_tot),
			 preprocess_value(Statistics->cancel_events_svc_min),
			 preprocess_value(Statistics->cancel_events_svc_mean),
			 preprocess_value(Statistics->cancel_events_svc_max),
			 preprocess_value(Statistics->context_source_events),
			 preprocess_value(Statistics->context_source_events_tot),
			 preprocess_value(Statistics->context_source_events_svc_min),
			 preprocess_value(Statistics->context_source_events_svc_mean),
			 preprocess_value(Statistics->context_source_events_svc_max),
			 preprocess_value(Statistics->events),
			 preprocess_value(Statistics->events_tot),
			 preprocess_value(Statistics->age_min),
			 preprocess_value(Statistics->age_mean),
			 preprocess_value(Statistics->age_max));
	send_packet(buffer, strlen(buffer));
}

void
ctx_statistics_cb(const void * AttributeBlock, const lbm_context_stats_t * Statistics, void * ClientData)
{
	unsigned int format;
	char buffer[4096];
	char * ptr;
	size_t remaining_len;

	memset(buffer, 0, sizeof(buffer));
	format = LBMMONUDP_CTX_VER;
	++MessagesReceived;
	format_common_data(buffer, sizeof(buffer), format, AttributeBlock, NULL);
	ptr = &(buffer[strlen(buffer)]);
	remaining_len = sizeof(buffer) - strlen(buffer);
	snprintf(ptr,
			 remaining_len,
			 "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
			 preprocess_value(Statistics->tr_dgrams_sent),
			 preprocess_value(Statistics->tr_bytes_sent),
			 preprocess_value(Statistics->tr_dgrams_rcved),
			 preprocess_value(Statistics->tr_bytes_rcved),
			 preprocess_value(Statistics->tr_dgrams_dropped_ver),
			 preprocess_value(Statistics->tr_dgrams_dropped_type),
			 preprocess_value(Statistics->tr_dgrams_dropped_malformed),
			 preprocess_value(Statistics->tr_dgrams_send_failed),
			 preprocess_value(Statistics->tr_src_topics),
			 preprocess_value(Statistics->tr_rcv_topics),
			 preprocess_value(Statistics->tr_rcv_unresolved_topics),
			 preprocess_value(Statistics->lbtrm_unknown_msgs_rcved),
			 preprocess_value(Statistics->lbtru_unknown_msgs_rcved),
			 preprocess_value(Statistics->send_blocked),
			 preprocess_value(Statistics->send_would_block),
			 preprocess_value(Statistics->resp_blocked),
			 preprocess_value(Statistics->resp_would_block),
			 preprocess_value(Statistics->uim_dup_msgs_rcved),
			 preprocess_value(Statistics->uim_msgs_no_stream_rcved));
	send_packet(buffer, strlen(buffer));
}

#define MODE_UNKNOWN 0
#define MODE_UNICAST 1
#define MODE_BROADCAST 2
#define MODE_MULTICAST 3

int
main(int argc, char * * argv)
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
	char * format_options = NULL;
	char format_options_string[1024];
	const lbmmon_transport_func_t * transport = lbmmon_transport_lbm_module();
	const lbmmon_format_func_t * format = lbmmon_format_csv_module();
	unsigned long port_value;
	unsigned short port = DEFAULT_PORT;
	unsigned long ttl_value;
	unsigned char ttl = DEFAULT_TTL;
	unsigned char mode = MODE_UNKNOWN;
	struct in_addr multicast_group;
	struct in_addr interface;
	struct in_addr address;
	struct in_addr broadcast_address;

	multicast_group.s_addr = 0;
	interface.s_addr = 0;
	address.s_addr = 0;
	broadcast_address.s_addr = 0;
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
	memset(format_options_string, 0, sizeof(format_options_string));

	while ((c = getopt_long(argc, argv, OptionString, OptionTable, NULL)) != EOF)
	{
		switch (c)
		{
			case '3':
				if (((unsigned long int) UINT_MAX) != ULONG_MAX)
				{
					ValueMask = UINT_MAX;
				}
				break;
			case 'a':
				if (optarg != NULL)
				{
					address.s_addr = inet_addr(optarg);
					if (address.s_addr == INADDR_NONE)
					{
						++errflag;
					}
					else
					{
						mode = MODE_UNICAST;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 'b':
				if (optarg != NULL)
				{
					broadcast_address.s_addr = inet_addr(optarg);
					if (broadcast_address.s_addr == INADDR_NONE)
					{
						++errflag;
					}
					else
					{
						mode = MODE_BROADCAST;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 'i':
				if (optarg != NULL)
				{
					interface.s_addr = inet_addr(optarg);
					if (interface.s_addr == INADDR_NONE)
					{
						++errflag;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 'm':
				if (optarg != NULL)
				{
					multicast_group.s_addr = inet_addr(optarg);
					if ((multicast_group.s_addr == INADDR_NONE) || (!IN_MULTICAST(ntohl(multicast_group.s_addr))))
					{
						++errflag;
					}
					else
					{
						mode = MODE_MULTICAST;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 'p':
				if (optarg != NULL)
				{
					port_value = strtoul(optarg, NULL, 0);
					if (((port_value == ULONG_MAX) && (errno = ERANGE)) || (port_value > (unsigned long) USHRT_MAX))
					{
						++errflag;
					}
					else
					{
						port = (unsigned short) port_value;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 't':
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "lbm") == 0)
					{
						transport = lbmmon_transport_lbm_module();
					}
					else if (strcasecmp(optarg, "udp") == 0)
					{
						transport = lbmmon_transport_udp_module();						
					}
					else
					{
						++errflag;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case 'T':
				if (optarg != NULL)
				{
					ttl_value = strtoul(optarg, NULL, 0);
					if (((ttl_value == ULONG_MAX) && (errno = ERANGE)) || (ttl_value > (unsigned long) UCHAR_MAX))
					{
						++errflag;
					}
					else
					{
						ttl = (unsigned char) ttl_value;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_TRANSPORT_OPTS:
				if (optarg != NULL)
				{
					strncpy(transport_options_string, optarg, sizeof(transport_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case 'f':
				if (optarg != NULL)
				{
					if (strcasecmp(optarg, "csv") == 0)
					{
						format = lbmmon_format_csv_module();
					}
					else
					{
						++errflag;
					}
				}
				else
				{
					++errflag;
				}
				break;
			case OPTION_MONITOR_FORMAT_OPTS:
				if (optarg != NULL)
				{
					strncpy(format_options_string, optarg, sizeof(format_options_string));
				}
				else
				{
					++errflag;
				}
				break;
			case 'h':
				fprintf(stderr, "%s\n%s\n", lbm_version(), Purpose);
				fprintf(stderr, Usage, argv[0]);
				exit(0);
			default:
				errflag++;
				break;
		}
	}

	if (errflag != 0)
	{
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	if (mode == MODE_UNKNOWN)
	{
		fprintf(stderr, "One of --address, --broadcast, or --multicast must be specified\n");
		fprintf(stderr, "%s\n", lbm_version());
		fprintf(stderr, Usage, argv[0]);
		exit(1);
	}

	/* Create the socket */
	Socket = socket(PF_INET, SOCK_DGRAM, 0);
	if (Socket == LBMMONUDP_INVALID_HANDLE)
	{
		fprintf(stderr, "socket() failed, %s", last_socket_error());
		exit(1);
	}

	/* If broadcast mode, enable broadcast on the socket */
	if (mode == MODE_BROADCAST)
	{
		int option = 1;
		socklen_t len = sizeof(option);
		rc = setsockopt(Socket, SOL_SOCKET, SO_BROADCAST, (void *) &option, len);
		if (rc == LBMMONUDP_SOCKET_ERROR)
		{
			fprintf(stderr, "setsockopt(...,SO_BROADCAST,...) failed, %s", last_socket_error());
#ifdef _WIN32
			closesocket(Socket);
#else
			close(Socket);
#endif
			exit(1);
		}
	}

	/* For multicast, set the outgoing interface and TTL. */
	if (mode == MODE_MULTICAST)
	{
		struct in_addr ifc_addr;
		rc = setsockopt(Socket, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
		if (rc == LBMMONUDP_SOCKET_ERROR)
		{
			fprintf(stderr, "setsockopt(...,IP_MULTICAST_TTL,...) failed, %s", last_socket_error());
#ifdef _WIN32
			closesocket(Socket);
#else
			close(Socket);
#endif
			exit(1);
		}
		ifc_addr.s_addr = interface.s_addr;
		rc = setsockopt(Socket, IPPROTO_IP, IP_MULTICAST_IF, (void *) &ifc_addr, sizeof(ifc_addr));
		if (rc == LBMMONUDP_SOCKET_ERROR)
		{
			fprintf(stderr, "setsockopt(...,IP_MULTICAST_IF,...) failed, %s", last_socket_error());
#ifdef _WIN32
			closesocket(Socket);
#else
			close(Socket);
#endif
			exit(1);
		}
	}

	/* Build the peer sockaddr_in. */
	Peer.sin_family = AF_INET;
	Peer.sin_port = htons(port);
	switch (mode)
	{
		case MODE_UNICAST:
		default:
			Peer.sin_addr.s_addr = address.s_addr;
			break;

		case MODE_BROADCAST:
			Peer.sin_addr.s_addr = broadcast_address.s_addr;
			break;

		case MODE_MULTICAST:
			Peer.sin_addr.s_addr = multicast_group.s_addr;
			break;
	}

	/* Setup the monitor receiver */
	if (strlen(transport_options_string) > 0)
	{
		transport_options = transport_options_string;
	}
	if (strlen(format_options_string) > 0)
	{
		format_options = format_options_string;
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

	rc = lbmmon_rctl_create(&monctl, format, format_options, transport, transport_options, attr, NULL);
	if (rc != 0)
	{
		fprintf(stderr, "call to lbmmon_rctl_create() failed\n");
#ifdef _WIN32
		closesocket(Socket);
#else
		close(Socket);
#endif
		exit(1);
	}
	lbmmon_rctl_attr_delete(attr);

	signal(SIGINT, SignalHandler);
	while (Terminate == 0)
	{
		SLEEP_SEC(2);
		printf("Stat msgs received/sent %lu/%lu\n", MessagesReceived, MessagesSent);
	}

	lbmmon_rctl_destroy(monctl);

#ifdef _WIN32
	closesocket(Socket);
#else
	close(Socket);
#endif

	return (0);
}

