import com.latencybusters.lbm.*;
import java.util.Date;
import java.text.NumberFormat;

// See https://communities.informatica.com/infakb/faq/5/Pages/80008.aspx
import org.openmdx.uses.gnu.getopt.*;

/*
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

class lbmmon
{
	private static String purpose = "Purpose: Example LBM statistics monitoring application.";
	private static String usage =
	"Usage: lbmmon [options]\n"
	+ "Available options:\n"
	+ "  -h, --help                 help\n"
	+ "  -t, --transport TRANS      use transport module TRANS\n"
	+ "                             TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "      --transport-opts OPTS  use OPTS as transport module options\n"
	+ "  -f, --format FMT           use format module FMT\n"
	+ "                             FMT may be `csv'\n"
	+ "      --format-opts OPTS     use OPTS as format module options\n"
	+ "\n"
	+ "Transport and format options are passed as name=value pairs, separated by a semicolon.\n"
	+ "\n"
	+ "LBM transport options:\n"
	+ "  config=FILE            use LBM configuration file FILE\n"
	+ "  topic=TOPIC            receive statistics on topic TOPIC\n"
	+ "                         default is /29west/statistics\n"
	+ "  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n"
	+ "\n"
	+ "UDP transport options:\n"
	+ "  port=NUM               receive on UDP port NUM\n"
	+ "  interface=IP           receive multicast on interface IP\n"
	+ "  mcgroup=GRP            receive on multicast group GRP\n"
	+ "\n"
	+ "LBMSNMP transport options:\n"
	+ "  config=FILE            use LBM configuration file FILE\n"
	+ "  topic=TOPIC            receive statistics on topic TOPIC\n"
	+ "                         default is /29west/statistics\n"
	+ "  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n"
	+ "\n"
	+ "CSV format options:\n"
	+ "  separator=CHAR         separate CSV fields with character CHAR\n"
	+ "                         defaults to `,'\n"
	;

	
	public static void main(String[] args)
	{
		LBM lbm = null;
		try
		{
			lbm = new LBM();
		}
		catch (LBMException ex)
		{
			System.err.println("Error initializing LBM: " + ex.toString());
			System.exit(1);
		}
		org.apache.log4j.Logger logger;
		logger = org.apache.log4j.Logger.getLogger("lbmmon");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		LBMObjectRecyclerBase objRec = new LBMObjectRecycler();
		int transport = LBMMonitor.TRANSPORT_LBM;
		int format = LBMMonitor.FORMAT_CSV;
		String transport_options = "";
		String format_options = "";
		LongOpt[] longopts = new LongOpt[5];
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		longopts[0] = new LongOpt("transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[1] = new LongOpt("transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[2] = new LongOpt("format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[3] = new LongOpt("format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[4] = new LongOpt("help", LongOpt.NO_ARGUMENT, null, 'h');
		Getopt gopt = new Getopt("lbmmon", args, "+t:f:", longopts);
		int c = -1;
		boolean error = false;
		while ((c = gopt.getopt()) != -1)
		{
			switch (c)
			{
				case 'h':
					print_help_exit(0);
				case 'f':
				case OPTION_MONITOR_FORMAT:
					if (gopt.getOptarg().compareToIgnoreCase("csv") == 0)
						format = LBMMonitor.FORMAT_CSV;
					else
						error = true;
					break;
				case 't':
				case OPTION_MONITOR_TRANSPORT:
					if (gopt.getOptarg().compareToIgnoreCase("lbm") == 0)
					{
						transport = LBMMonitor.TRANSPORT_LBM;
					}
					else
					{
						if (gopt.getOptarg().compareToIgnoreCase("udp") == 0)
						{
							transport = LBMMonitor.TRANSPORT_UDP;
						}
						else
						{
							if (gopt.getOptarg().compareToIgnoreCase("lbmsnmp") == 0)
							{
								transport = LBMMonitor.TRANSPORT_LBMSNMP;
							}
							else
							{
								error = true;
							}
						}
					}
					break;
				case OPTION_MONITOR_TRANSPORT_OPTS:
					transport_options += gopt.getOptarg();
					break;
				case OPTION_MONITOR_FORMAT_OPTS:
					format_options += gopt.getOptarg();
					break;
				default:
					error = true;
					break;
			}
			if (error)
				break;
		}
		if (error)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		LBMMonitorReceiver lbmmonrcv = null;
		try
		{
			lbmmonrcv = new LBMMonitorReceiver(format, format_options, transport, transport_options, objRec, null);
			//If not using object recycling
			//lbmmonrcv = new LBMMonitorReceiver(format, format_options, transport, transport_options);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating monitor receiver: " + ex.toString());
			System.exit(1);
		}
		LBMMonCallbacks lbmmoncbs = new LBMMonCallbacks(lbmmonrcv, objRec);
		lbmmonrcv.addStatisticsCallback(lbmmoncbs);
		for (;;)
		{
			try
			{
				Thread.sleep(2000);
			}
			catch (InterruptedException e) { }
		}
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}
}

class LBMMonCallbacks extends LBMMonitorStatisticsCallbackObject
{
	private LBMMonitorReceiver _lbmmonrcv;
	private LBMObjectRecyclerBase _objRec;
	
	LBMMonCallbacks(LBMMonitorReceiver lbmmonrcv, LBMObjectRecyclerBase objRec)
	{
		_lbmmonrcv = lbmmonrcv;
		_objRec = objRec;
	}

	public void onReceive(LBMSourceStatistics stats)
	{
		System.err.print("\nSource statistics received");
		try
		{
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("Source: " + stats.source());
			System.err.println("Transport: " + stats.typeName());
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.err.println("\tClients       : " +
									   stats.numberOfClients());
					System.err.println("\tBytes buffered: " +
									   stats.bytesBuffered());
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.err.println("\tLBT-RM datagrams sent                                 : " +
									   stats.messagesSent());                                  
					System.err.println("\tLBT-RM datagram bytes sent                            : " +
									   stats.bytesSent());                                     
					System.err.println("\tLBT-RM datagrams in transmission window               : " +
									   stats.transmissionWindowMessages());                    
					System.err.println("\tLBT-RM datagram bytes in transmission window          : " +
									   stats.transmissionWindowBytes());                       
					System.err.println("\tLBT-RM NAK packets received                           : " +
									   stats.nakPacketsReceived());                            
					System.err.println("\tLBT-RM NAKs received                                  : " +
									   stats.naksReceived());                                  
					System.err.println("\tLBT-RM NAKs ignored                                   : " +
									   stats.naksIgnored());                                   
					System.err.println("\tLBT-RM NAKs shed                                      : " +
									   stats.naksShed());                                      
					System.err.println("\tLBT-RM NAKs ignored (retransmit delay)                : " +
									   stats.naksIgnoredRetransmitDelay());
					System.err.println("\tLBT-RM retransmission datagrams sent                  : " +
									   stats.retransmissionsSent());
					System.err.println("\tLBT-RM retransmission datagrams bytes sent            : " +
									   stats.retransmissionBytesSent());
					System.err.println("\tLBT-RM datagrams queued by rate control               : " +
									   stats.messagesQueued());
					System.err.println("\tLBT-RM retransmission datagrams queued by rate control: " +
									   stats.retransmissionsQueued());
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
					System.err.println("\tLBT-RU datagrams sent                    : " +
									   stats.messagesSent());                      
					System.err.println("\tLBT-RU datagram bytes sent               : " +
									   stats.bytesSent());                         
					System.err.println("\tLBT-RU NAK packets received              : " +
									   stats.nakPacketsReceived());                
					System.err.println("\tLBT-RU NAKs received                     : " +
									   stats.naksReceived());                      
					System.err.println("\tLBT-RU NAKs ignored                      : " +
									   stats.naksIgnored());                       
					System.err.println("\tLBT-RU NAKs shed                         : " +
									   stats.naksShed());
					System.err.println("\tLBT-RU NAKs ignored (retransmit delay)   : " +
									   stats.naksIgnoredRetransmitDelay());
					System.err.println("\tLBT-RU retransmission datagrams sent     : " +
									   stats.retransmissionsSent());
					System.err.println("\tLBT-RU retransmission datagram bytes sent: " +
							   		   stats.retransmissionBytesSent());
					System.err.println("\tClients                                  : " +
									   stats.numberOfClients());
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.err.println("\tClients                    :" + stats.numberOfClients());
					System.err.println("\tLBT-IPC datagrams sent     :" + stats.messagesSent());
					System.err.println("\tLBT-IPC datagram bytes sent:" + stats.bytesSent());
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.err.println("\tClients                    :" + stats.numberOfClients());
					System.err.println("\tLBT-RDMA datagrams sent     :" + stats.messagesSent());
					System.err.println("\tLBT-RDMA datagram bytes sent:" + stats.bytesSent());
					break;
				default:
					System.err.println("Error: unknown transport type received." + stats.type());
					break;
			}
		}
		catch (Exception ex)
		{
			System.err.println("Error getting source statistics: " + ex.toString());
		}
		_objRec.doneWithSourceStatistics(stats);
	}

	public void onReceive(LBMReceiverStatistics stats)
	{
		System.err.print("\nReceiver statistics received");
		try
		{
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("Source: " + stats.source());
			System.err.println("Transport: " + stats.typeName());
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.err.println("\tLBT-TCP bytes received                                    : " +
									   stats.bytesReceived());
					System.err.println("\tLBM messages received                                     : " +
									   stats.lbmMessagesReceived());
					System.err.println("\tLBM messages received with uninteresting topic            : " +
									   stats.noTopicMessagesReceived());
					System.err.println("\tLBM requests received                                     : " +
									   stats.lbmRequestsReceived());
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.err.println("\tLBT-RM datagrams received                                 : " +
									   stats.messagesReceived());                                  
					System.err.println("\tLBT-RM datagram bytes received                            : " +
									   stats.bytesReceived());                                     
					System.err.println("\tLBT-RM NAK packets sent                                   : " +
									   stats.nakPacketsSent());                                    
					System.err.println("\tLBT-RM NAKs sent                                          : " +
									   stats.naksSent());                                          
					System.err.println("\tLost LBT-RM datagrams detected                            : " +
									   stats.lost());                                              
					System.err.println("\tNCFs received (ignored)                                   : " +
									   stats.ncfsIgnored());                                       
					System.err.println("\tNCFs received (shed)                                      : " +
									   stats.ncfsShed());                                          
					System.err.println("\tNCFs received (retransmit delay)                          : " +
									   stats.ncfsRetransmissionDelay());                           
					System.err.println("\tNCFs received (unknown)                                   : " +
									   stats.ncfsUnknown());                                       
					System.err.println("\tLoss recovery minimum time                                : " +
									   stats.minimumRecoveryTime() + "ms");                        
					System.err.println("\tLoss recovery mean time                                   : " +
									   stats.meanRecoveryTime() + "ms");                           
					System.err.println("\tLoss recovery maximum time                                : " +
									   stats.maximumRecoveryTime() + "ms");                        
					System.err.println("\tMinimum transmissions per individual NAK                  : " +
									   stats.minimumNakTransmissions());                           
					System.err.println("\tMean transmissions per individual NAK                     : " +
									   stats.meanNakTransmissions());                              
					System.err.println("\tMaximum transmissions per individual NAK                  : " +
									   stats.maximumNakTransmissions());                           
					System.err.println("\tDuplicate LBT-RM datagrams received                       : " +
									   stats.duplicateMessages());
					System.err.println("\tLBT-RM datagrams unrecoverable (window advance)           : " +
									   stats.unrecoveredMessagesWindowAdvance());
					System.err.println("\tLBT-RM datagrams unrecoverable (NAK generation expiration): " +
									   stats.unrecoveredMessagesNakGenerationTimeout());
					System.err.println("\tLBT-RM LBM messages received                              : " +
									   stats.lbmMessagesReceived());                               
					System.err.println("\tLBT-RM LBM messages received with uninteresting topic     : " +
									   stats.noTopicMessagesReceived());                           
					System.err.println("\tLBT-RM LBM requests received                              : " +
									   stats.lbmRequestsReceived());                               
					System.err.println("\tLBT-RM datagrams dropped (size)                           : " +
									   stats.datagramsDroppedIncorrectSize());                     
					System.err.println("\tLBT-RM datagrams dropped (type)                           : " +
									   stats.datagramsDroppedType());                              
					System.err.println("\tLBT-RM datagrams dropped (version)                        : " +
									   stats.datagramsDroppedVersion());                           
					System.err.println("\tLBT-RM datagrams dropped (header)                         : " +
									   stats.datagramsDroppedHeader());                            
					System.err.println("\tLBT-RM datagrams dropped (other)                          : " +
									   stats.datagramsDroppedOther());
					System.err.println("\tLBT-RM datagrams received out of order                    : " +
									   stats.outOfOrder());
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
					System.err.println("\tLBT-RU datagrams received                                 : " +
									   stats.messagesReceived());
					System.err.println("\tLBT-RU datagram bytes received                            : " +
									   stats.bytesReceived());
					System.err.println("\tLBT-RU NAK packets sent                                   : " +
									   stats.nakPacketsSent());                                    
					System.err.println("\tLBT-RU NAKs sent                                          : " +
									   stats.naksSent());                                          
					System.err.println("\tLost LBT-RU datagrams detected                            : " +
									   stats.lost());                                              
					System.err.println("\tNCFs received (ignored)                                   : " +
									   stats.ncfsIgnored());                                       
					System.err.println("\tNCFs received (shed)                                      : " +
									   stats.ncfsShed());                                          
					System.err.println("\tNCFs received (retransmit delay)                          : " +
									   stats.ncfsRetransmissionDelay());                           
					System.err.println("\tNCFs received (unknown)                                   : " +
									   stats.ncfsUnknown());                                       
					System.err.println("\tLoss recovery minimum time                                : " +
									   stats.minimumRecoveryTime() + "ms");                        
					System.err.println("\tLoss recovery mean time                                   : " +
									   stats.meanRecoveryTime() + "ms");                           
					System.err.println("\tLoss recovery maximum time                                : " +
									   stats.maximumRecoveryTime() + "ms");                        
					System.err.println("\tMinimum transmissions per individual NAK                  : " +
									   stats.minimumNakTransmissions());                           
					System.err.println("\tMean transmissions per individual NAK                     : " +
									   stats.meanNakTransmissions());                              
					System.err.println("\tMaximum transmissions per individual NAK                  : " +
									   stats.maximumNakTransmissions());                           
					System.err.println("\tDuplicate LBT-RU datagrams received                       : " +
									   stats.duplicateMessages());                                 
					System.err.println("\tLBT-RU datagrams unrecoverable (window advance)           : " +
									   stats.unrecoveredMessagesWindowAdvance());
					System.err.println("\tLBT-RU datagrams unrecoverable (NAK generation expiration): " +
									   stats.unrecoveredMessagesNakGenerationTimeout());
					System.err.println("\tLBT-RU LBM messages received                              : " +
									   stats.lbmMessagesReceived());                               
					System.err.println("\tLBT-RU LBM messages received with uninteresting topic     : " +
									   stats.noTopicMessagesReceived());                           
					System.err.println("\tLBT-RU LBM requests received                              : " +
									   stats.lbmRequestsReceived());                               
					System.err.println("\tLBT-RU datagrams dropped (size)                           : " +
									   stats.datagramsDroppedIncorrectSize());                     
					System.err.println("\tLBT-RU datagrams dropped (type)                           : " +
									   stats.datagramsDroppedType());                              
					System.err.println("\tLBT-RU datagrams dropped (version)                        : " +
									   stats.datagramsDroppedVersion());                           
					System.err.println("\tLBT-RU datagrams dropped (header)                         : " +
									   stats.datagramsDroppedHeader());                            
					System.err.println("\tLBT-RU datagrams dropped (SID)                            : " +
									   stats.datagramsDroppedSID());                               
					System.err.println("\tLBT-RU datagrams dropped (other)                          : " +
									   stats.datagramsDroppedOther());
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.err.println("\tLBT-IPC datagrams received                                :" + stats.messagesReceived());
					System.err.println("\tLBT-IPC datagram bytes received                           :" + stats.bytesReceived());
					System.err.println("\tLBT-IPC LBM messages received                             :" + stats.lbmMessagesReceived());
					System.err.println("\tLBT-IPC LBM messages received with uninteresting topic    :" + stats.noTopicMessagesReceived());
					System.err.println("\tLBT-IPC LBM requests received                             :" + stats.lbmRequestsReceived());
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.err.println("\tLBT-RDMA datagrams received                                :" + stats.messagesReceived());
					System.err.println("\tLBT-RDMA datagram bytes received                           :" + stats.bytesReceived());
					System.err.println("\tLBT-RDMA LBM messages received                             :" + stats.lbmMessagesReceived());
					System.err.println("\tLBT-RDMA LBM messages received with uninteresting topic    :" + stats.noTopicMessagesReceived());
					System.err.println("\tLBT-RDMA LBM requests received                             :" + stats.lbmRequestsReceived());
					break;
				default:
					System.err.println("Error: unknown transport type received." + stats.type());
					break;
			}
		}
		catch (Exception ex)
		{
			System.err.println("Error getting receiver statistics: " + ex.toString());
		}
		_objRec.doneWithReceiverStatistics(stats);
	}
	public void onReceive(LBMContextStatistics stats)
	{
		System.err.print("\nContext statistics received");
		try
		{
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("\tTopic resolution datagrams sent               : " +
								stats.topicResolutionDatagramsSent());
			System.err.println("\tTopic resolution datagrams received           : " +
								stats.topicResolutionDatagramsReceived());
			System.err.println("\tTopic resolution datagram bytes sent          : " +
								stats.topicResolutionBytesSent());
			System.err.println("\tTopic resolution datagram bytes received      : " +
								stats.topicResolutionBytesReceived());
			System.err.println("\tTopic resolution datagrams dropped version    : " +
								stats.topicResolutionDatagramsDroppedVersion());
			System.err.println("\tTopic resolution datagrams dropped type       : " +
								stats.topicResolutionDatagramsDroppedType());
			System.err.println("\tTopic resolution datagrams dropped malformed  : " +
								stats.topicResolutionDatagramsDroppedMalformed());
			System.err.println("\tTopic resolution datagrams send failed        : " +
								stats.topicResolutionDatagramsSendFailed());
			System.err.println("\tTopic resolution source topics                : " +
								stats.topicResolutionSourceTopics());
			System.err.println("\tTopic resolution receiver topics              : " +
								stats.topicResolutionReceiverTopics());
			System.err.println("\tTopic resolution unresolved receiver topics   : " +
								stats.topicResolutionUnresolvedReceiverTopics());
			System.err.println("\tLBT-RM unknown datagrams received             : " +
								stats.lbtrmUnknownMessagesReceived());
			System.err.println("\tLBT-RU unknown datagrams received             : " +
								stats.lbtruUnknownMessagesReceived());
			System.err.println("\tLBM send calls which blocked                  : " +
								stats.sendBlocked());
			System.err.println("\tLBM send calls which returned EWOULDBLOCK     : " +
								stats.sendWouldBlock());
			System.err.println("\tLBM response calls which blocked              : " +
								stats.responseBlocked());
			System.err.println("\tLBM response calls which returned EWOULDBLOCK : " +
								stats.responseWouldBlock());
		}
		catch (Exception ex){
			System.err.println("Error getting receiver statistics: " + ex.toString());
		}
		_objRec.doneWithContextStatistics(stats);
	}
	
	public void onReceive(LBMEventQueueStatistics stats)
	{
		System.err.print("\nEvent Queue statistics received");
		try {
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("\tData messages enqueued                                        : " +
								stats.dataMessages());
			System.err.println("\tTotal data messages enqueued                                  : " +
								stats.dataMessagesTotal());
			System.err.println("\tData messages min service time (microseconds)                 : " +
								stats.dataMessagesMinimumServiceTime());
			System.err.println("\tData messages mean service time (microseconds)                : " +
								stats.dataMessagesMeanServiceTime());
			System.err.println("\tData messages max service time (microseconds)                 : " +
								stats.dataMessagesMaximumServiceTime());
			System.err.println("\tResponse messages enqueued                                    : " +
								stats.responseMessages());
			System.err.println("\tTotal response messages enqueued                              : " +
								stats.responseMessagesTotal());
			System.err.println("\tResponse messages min service time (microseconds)             : " +
								stats.responseMessagesMinimumServiceTime());
			System.err.println("\tResponse messages mean service time (microseconds)            : " +
								stats.responseMessagesMeanServiceTime());
			System.err.println("\tResponse messages max service time (microseconds)             : " +
								stats.responseMessagesMaximumServiceTime());
			System.err.println("\tTopicless immediate messages enqueued                         : " +
								stats.topiclessImmediateMessages());
			System.err.println("\tTotal topicless immediate messages enqueued                   : " +
								stats.topiclessImmediateMessagesTotal());
			System.err.println("\tTopicless immediate messages min service time (microseconds)  : " +
								stats.topiclessImmediateMessagesMinimumServiceTime());
			System.err.println("\tTopicless immediate messages mean service time (microseconds) : " +
								stats.topiclessImmediateMessagesMeanServiceTime());
			System.err.println("\tTopicless immediate messages max service time (microseconds)  : " +
								stats.topiclessImmediateMessagesMaximumServiceTime());
			System.err.println("\tWildcard receiver messages enqueued                           : " +
								stats.wildcardReceiverMessages());
			System.err.println("\tTotal wildcard receiver messages enqueued                     : " +
								stats.wildcardReceiverMessagesTotal());
			System.err.println("\tWildcard receiver messages min service time (microseconds)    : " +
								stats.wildcardReceiverMessagesMinimumServiceTime());
			System.err.println("\tWildcard receiver messages mean service time (microseconds)   : " +
								stats.wildcardReceiverMessagesMeanServiceTime());
			System.err.println("\tWildcard receiver messages max service time (microseconds)    : " +
								stats.wildcardReceiverMessagesMaximumServiceTime());
			System.err.println("\tI/O events enqueued                                           : " +
								stats.ioEvents());
			System.err.println("\tTotal I/O events enqueued                                     : " +
								stats.ioEventsTotal());
			System.err.println("\tI/O events min service time (microseconds)                    : " +
								stats.ioEventsMinimumServiceTime());
			System.err.println("\tI/O events mean service time (microseconds)                   : " +
								stats.ioEventsMeanServiceTime());
			System.err.println("\tI/O events max service time (microseconds)                    : " +
								stats.ioEventsMaximumServiceTime());
			System.err.println("\tTimer events enqueued                                         : " +
								stats.timerEvents());
			System.err.println("\tTotal timer events enqueued                                   : " +
								stats.timerEventsTotal());
			System.err.println("\tTimer events min service time (microseconds)                  : " +
								stats.timerEventsMinimumServiceTime());
			System.err.println("\tTimer events mean service time (microseconds)                 : " +
								stats.timerEventsMeanServiceTime());
			System.err.println("\tTimer events max service time (microseconds)                  : " +
								stats.timerEventsMaximumServiceTime());
			System.err.println("\tSource events enqueued                                        : " +
								stats.sourceEvents());
			System.err.println("\tTotal source events enqueued                                  : " +
								stats.sourceEventsTotal());
			System.err.println("\tSource events min service time (microseconds)                 : " +
								stats.sourceEventsMinimumServiceTime());
			System.err.println("\tSource events mean service time (microseconds)                : " +
								stats.sourceEventsMeanServiceTime());
			System.err.println("\tSource events max service time (microseconds)                 : " +
								stats.sourceEventsMaximumServiceTime());
			System.err.println("\tUnblock events enqueued                                       : " +
								stats.unblockEvents());
			System.err.println("\tTotal unblock events enqueued                                 : " +
								stats.unblockEventsTotal());
			System.err.println("\tCancel events enqueued                                        : " +
								stats.cancelEvents());
			System.err.println("\tTotal cancel events enqueued                                  : " +
								stats.cancelEventsTotal());
			System.err.println("\tCancel events min service time (microseconds)                 : " +
								stats.cancelEventsMinimumServiceTime());
			System.err.println("\tCancel events mean service time (microseconds)                : " +
								stats.cancelEventsMeanServiceTime());
			System.err.println("\tCancel events max service time (microseconds)                 : " +
								stats.cancelEventsMaximumServiceTime());
			System.err.println("\tCallback events enqueued                                      : " +
								stats.callbackEvents());
			System.err.println("\tTotal callback events enqueued                                : " +
								stats.callbackEventsTotal());
			System.err.println("\tCallback events min service time (microseconds)               : " +
								stats.callbackEventsMinimumServiceTime());
			System.err.println("\tCallback events mean service time (microseconds)              : " +
								stats.callbackEventsMeanServiceTime());
			System.err.println("\tCallback events max service time (microseconds)               : " +
								stats.callbackEventsMaximumServiceTime());			
			System.err.println("\tContext source events enqueued                                : " +
								stats.contextSourceEvents());
			System.err.println("\tTotal context source events enqueued                          : " +
								stats.contextSourceEventsTotal());
			System.err.println("\tContext source events min service time (microseconds)         : " +
								stats.contextSourceEventsMinimumServiceTime());
			System.err.println("\tContext source events mean service time (microseconds)        : " +
								stats.contextSourceEventsMeanServiceTime());
			System.err.println("\tContext source events max service time (microseconds)         : " +
								stats.contextSourceEventsMaximumServiceTime());
			System.err.println("\tEvents currently enqueued                                     : " +
								stats.events());
			System.err.println("\tTotal events enqueued                                         : " +
								stats.eventsTotal());
			System.err.println("\tMinimum age of events enqueued (microseconds)                 : " +
								stats.minimumAge());
			System.err.println("\tMean age of events enqueued (microseconds)                    : " +
								stats.meanAge());
			System.err.println("\tMax age of events enqueued (microseconds)                     : " +
								stats.maximumAge());
		}
		catch (Exception ex){
			System.err.println("Error getting event queue statistics: " + ex.toString());
		}
		_objRec.doneWithEventQueueStatistics(stats);
	}
	public void onReceive(LBMImmediateMessageSourceStatistics stats)
	{
		System.err.print("\nImmediate message source statistics received");
		try
		{
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("Source: " + stats.source());
			System.err.println("Transport: " + stats.typeName());
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.err.println("\tClients       : " +
									   stats.numberOfClients());
					System.err.println("\tBytes buffered: " +
									   stats.bytesBuffered());
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.err.println("\tLBT-RM datagrams sent                                 : " +
									   stats.messagesSent());
					System.err.println("\tLBT-RM datagrams bytes sent                           : " +
									   stats.bytesSent());
					System.err.println("\tLBT-RM datagrams in transmission window               : " +
									   stats.transmissionWindowMessages());
					System.err.println("\tLBT-RM datagram bytes in transmission window          : " +
									   stats.transmissionWindowBytes());
					System.err.println("\tLBT-RM NAK packets received                           : " +
									   stats.nakPacketsReceived());                            
					System.err.println("\tLBT-RM NAKs received                                  : " +
									   stats.naksReceived());                                  
					System.err.println("\tLBT-RM NAKs ignored                                   : " +
									   stats.naksIgnored());                                   
					System.err.println("\tLBT-RM NAKs shed                                      : " +
									   stats.naksShed());                                      
					System.err.println("\tLBT-RM NAKs ignored (retransmit delay)                : " +
									   stats.naksIgnoredRetransmitDelay());
					System.err.println("\tLBT-RM retransmission datagrams sent                  : " +
									   stats.retransmissionsSent());
					System.err.println("\tLBT-RM retransmission datagram bytes sent             : " +
							   		   stats.retransmissionBytesSent());
					System.err.println("\tLBT-RM datagrams queued by rate control               : " +
									   stats.messagesQueued());
					System.err.println("\tLBT-RM retransmission datagrams queued by rate control: " +
									   stats.retransmissionsQueued());
					break;
				default:
					System.err.println("Error: unknown transport type received." + stats.type());
					break;
			}
		}
		catch (Exception ex)
		{
			System.err.println("Error getting immediate message source statistics: " + ex.toString());
		}
		_objRec.doneWithImmediateMessageSourceStatistics(stats);
	}
	
	public void onReceive(LBMImmediateMessageReceiverStatistics stats)
	{
		System.err.print("\nImmediate message receiver statistics received");
		try
		{
			System.err.print(" from " + stats.getApplicationSourceId());
			System.err.print(" at " + stats.getSender().toString());
			System.err.println(", sent " + stats.getTimestamp().toString());
			System.err.println("Source: " + stats.source());
			System.err.println("Transport: " + stats.typeName());
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.err.println("\tLBT-TCP bytes received                                    : " +
									   stats.bytesReceived());
					System.err.println("\tLBM messages received                                     : " +
									   stats.lbmMessagesReceived());
					System.err.println("\tLBM messages received with uninteresting topic            : " +
									   stats.noTopicMessagesReceived());
					System.err.println("\tLBM requests received                                     : " +
									   stats.lbmRequestsReceived());
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.err.println("\tLBT-RM datagrams received                                 : " +
									   stats.messagesReceived());
					System.err.println("\tLBT-RM datagram bytes received                            : " +
									   stats.bytesReceived());
					System.err.println("\tLBT-RM NAK packets sent                                   : " +
									   stats.nakPacketsSent());
					System.err.println("\tLBT-RM NAKs sent                                          : " +
									   stats.naksSent());
					System.err.println("\tLost LBT-RM datagrams detected                            : " +
									   stats.lost());
					System.err.println("\tNCFs received (ignored)                                   : " +
									   stats.ncfsIgnored());                                       
					System.err.println("\tNCFs received (shed)                                      : " +
									   stats.ncfsShed());                                          
					System.err.println("\tNCFs received (retransmit delay)                          : " +
									   stats.ncfsRetransmissionDelay());                           
					System.err.println("\tNCFs received (unknown)                                   : " +
									   stats.ncfsUnknown());                                       
					System.err.println("\tLoss recovery minimum time                                : " +
									   stats.minimumRecoveryTime() + "ms");                        
					System.err.println("\tLoss recovery mean time                                   : " +
									   stats.meanRecoveryTime() + "ms");                           
					System.err.println("\tLoss recovery maximum time                                : " +
									   stats.maximumRecoveryTime() + "ms");                        
					System.err.println("\tMinimum transmissions per individual NAK                  : " +
									   stats.minimumNakTransmissions());                           
					System.err.println("\tMean transmissions per individual NAK                     : " +
									   stats.meanNakTransmissions());                              
					System.err.println("\tMaximum transmissions per individual NAK                  : " +
									   stats.maximumNakTransmissions());
					System.err.println("\tDuplicate LBT-RM datagrams received                       : " +
									   stats.duplicateMessages());
					System.err.println("\tLBT-RM datagrams unrecoverable (window advance)           : " +
									   stats.unrecoveredMessagesWindowAdvance());
					System.err.println("\tLBT-RM datagrams unrecoverable (NAK generation expiration): " +
									   stats.unrecoveredMessagesNakGenerationTimeout());
					System.err.println("\tLBT-RM LBM messages received                              : " +
									   stats.lbmMessagesReceived());                               
					System.err.println("\tLBT-RM LBM messages received with uninteresting topic     : " +
									   stats.noTopicMessagesReceived());                           
					System.err.println("\tLBT-RM LBM requests received                              : " +
									   stats.lbmRequestsReceived());                               
					System.err.println("\tLBT-RM datagrams dropped (size)                           : " +
									   stats.datagramsDroppedIncorrectSize());                     
					System.err.println("\tLBT-RM datagrams dropped (type)                           : " +
							   			stats.datagramsDroppedType());                             
					System.err.println("\tLBT-RM datagrams dropped (version)                        : " +
							   			stats.datagramsDroppedVersion());                          
					System.err.println("\tLBT-RM datagrams dropped (header)                         : " +
							   			stats.datagramsDroppedHeader());                           
					System.err.println("\tLBT-RM datagrams dropped (other)                          : " +
							   			stats.datagramsDroppedOther());
					System.err.println("\tLBT-RM datagrams received out of order                    : " +
							   			stats.outOfOrder());
					break;
				default:
					System.err.println("Error: unknown transport type received." + stats.type());
					break;
			}
		}
		catch (Exception ex)
		{
			System.err.println("Error getting immediate message receiver statistics: " + ex.toString());
		}
		_objRec.doneWithImmediateMessageReceiverStatistics(stats);
	}
}

