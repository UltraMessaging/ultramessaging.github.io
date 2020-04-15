import com.latencybusters.lbm.*;

import java.util.*;
import java.text.NumberFormat;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.util.concurrent.Semaphore;

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

import java.io.*;

class umqsrc
{
	private static String pcid = "";
	private static int msgs = 10000000;
	private static int stats_sec = 0;
	private static int verbose = 0;
	private static boolean sequential = true;
	public static int flightsz = 0;
	public static int appsent = 0;
	public static int stablerecv = 0;
	public static int initial_ulb_reg = 1;
	private static int pause_ivl = 0;
	private static String queueName = null;
	private static String appsets = null;
	private static int msgs_per_ivl = 1;
	public static Semaphore flightlock;
	public static long last_clientd_sent = 0;
	public static long last_clientd_stable = 0;
	public static long sleep_before_sending = 0;
	private static int msgs_per_sec = 0;
	private static UMQIndexInfo index_info = null;
	public static boolean immediate = false;
	private static String purpose = "Purpose: Send messages on a single topic.";
	private static String usage =
	"Usage: umqsrc [options] topic\n"
	+ "Available options:\n"
	+ "  -A cfg = use ULB application set specification cfg\n"
	+ "  -c filename = read config parameters from filename\n"
	+ "  -C filename = read context config parameters from filename\n"
    + "  -e = use LBM sequential mode\n"
	+ "  -f NUM = allow NUM unstabilized messages in flight (determines message rate)"
	+ "  -h = help\n"
    + "  -i = display Message IDs for sent messages\n"
	+ "  -I = submit Immediate Messages to the Queue\n"
	+ "  -l len = send messages of len bytes\n"
	+ "  -L linger = linger for linger seconds before closing context\n"
	+ "  -m NUM = send at NUM messages per second (trumped by -f)"
	+ "  -M msgs = send msgs number of messages\n"
	+ "  -N = display sequence number information source events\n"
	+ "  -n = use non-blocking I/O\n"
	+ "  -P msec = pause after each send msec milliseconds\n"
	+ "  -Q queue = use Queue given by name.\n"
	+ "  -R rate/pct = send with LBT-RM at rate and retranmission pct% \n"
	+ "  -s sec = print stats every sec seconds\n"
    + "  -T = set Message Stability Notification\n"
    + "  -X = Send using numeric or named UMQ index X\n"
	+ "  -v = bump verbose level\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-src NUM = monitor source every NUM seconds\n"
	+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
	+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
	+ "  --monitor-format FMT = use monitor format module FMT\n"
	+ "                         FMT may be `csv'\n"
	+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
	+ "  --monitor-appid ID = use ID as application ID string\n"
	+ "  --flight-size = See -f above\n"
	+ "  --message-rate = See -m above\n"
	+ "  --appsets = see -A above\n"
	;

	public static void main(String[] args)
	{
		int rm_rate = 0;
		int rm_retrans = 0;
		int linger = 5;
		int monitor_context_ivl = 0;
		int monitor_source_ivl = 0;
		boolean monitor_context = false;
		boolean monitor_source = false; 
		boolean latejoin = false;
		String regid = null;
		String storeip = null;
		String storeport = null;
		int mon_transport = LBMMonitor.TRANSPORT_LBM;
		int mon_format = LBMMonitor.FORMAT_CSV;
		String mon_format_options = "";
		String mon_transport_options = "";
		String application_id = null;
		StringTokenizer tokens;
		boolean seqnum_info = false;
		boolean stability = false;
		boolean msg_id_info = false;
		String topicString = null;
		LBMObjectRecycler objRec = new LBMObjectRecycler();

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
		logger = org.apache.log4j.Logger.getLogger("umqsrc");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);
		String conffname = null;
		String cconffname = null;

		LongOpt[] longopts = new LongOpt[10];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_SRC = 3;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;
		final int OPTION_FLIGHT_SIZE = 9;
		final int OPTION_MESSAGE_RATE = 10;
		final int OPTION_APPSETS = 11;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-src", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_SRC);
		longopts[2] = new LongOpt("monitor-transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[3] = new LongOpt("monitor-transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[4] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[5] = new LongOpt("monitor-format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[6] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_APPID);
		longopts[7] = new LongOpt("flight-size", LongOpt.REQUIRED_ARGUMENT, null, OPTION_FLIGHT_SIZE);
		longopts[8] = new LongOpt("message-rate", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MESSAGE_RATE);
		longopts[9] = new LongOpt("appsets", LongOpt.REQUIRED_ARGUMENT, null, OPTION_APPSETS);
		Getopt gopt = new Getopt("umqsrc", args, "+A:c:C:ef:hiIl:L:m:M:nNP:Q:R:s:TvX:", longopts);
		int c = -1;
		int msglen = 25;
		long bytes_sent = 0;
		boolean error = false;
		boolean block = true;
		while ((c = gopt.getopt()) != -1)
		{
			try
			{
				switch (c)
				{
					case OPTION_MONITOR_APPID:
						application_id = gopt.getOptarg();
						break;
					case OPTION_MONITOR_CTX:
						monitor_context = true;
						try {
							monitor_context_ivl = Integer.parseInt(gopt.getOptarg());
						}
						catch (Exception e)
						{
							error = true;
						}
						break;
					case OPTION_MONITOR_SRC:
						monitor_source = true;
						try {
							monitor_source_ivl = Integer.parseInt(gopt.getOptarg());
						}
						catch (Exception e)
						{
							error = true;
						}
						break;
					case OPTION_MONITOR_TRANSPORT:
						if (gopt.getOptarg().compareToIgnoreCase("lbm") == 0)
						{
							mon_transport = LBMMonitor.TRANSPORT_LBM;
						}
						else
						{
							if (gopt.getOptarg().compareToIgnoreCase("udp") == 0)
							{
								mon_transport = LBMMonitor.TRANSPORT_UDP;
							}
							else
							{
								if (gopt.getOptarg().compareToIgnoreCase("lbmsnmp") == 0)
								{
									mon_transport = LBMMonitor.TRANSPORT_LBMSNMP;
								}
								else
								{
									error = true;
								}
							}
						}
						break;
					case OPTION_MONITOR_TRANSPORT_OPTS:
						mon_transport_options += gopt.getOptarg();
						break;
					case OPTION_MONITOR_FORMAT:
						if (gopt.getOptarg().compareToIgnoreCase("csv") == 0)
							mon_format = LBMMonitor.FORMAT_CSV;
						else
							error = true;
						break;
					case OPTION_MONITOR_FORMAT_OPTS:
						mon_format_options += gopt.getOptarg();
						break;
					case OPTION_APPSETS:
					case 'A':
						appsets = gopt.getOptarg();
						break;
					case 'c':
						conffname = gopt.getOptarg();
						break;
					case 'C':
						cconffname = gopt.getOptarg();
						break;
					case 'e':
						sequential = false;
						break;
					case 'f':
					case OPTION_FLIGHT_SIZE:
						flightsz = Integer.parseInt(gopt.getOptarg());
						break;

					case 'h':
						print_help_exit(0);
					case 'i':
						msg_id_info = true;
						break;
					case 'I':
						immediate = true;
						break;
					case 'l':
						msglen = Integer.parseInt(gopt.getOptarg());
						break;
					case 'n':
						block = false;
						break;
					case 'N':
						seqnum_info = true;
						break;
					case 'L':
						linger = Integer.parseInt(gopt.getOptarg());
						break;
					case 'm':
					case OPTION_MESSAGE_RATE:
						msgs_per_sec = Integer.parseInt(gopt.getOptarg());
						break;

					case 'M':
						msgs = Integer.parseInt(gopt.getOptarg());
						break;
					case 'P':
						pause_ivl = Integer.parseInt(gopt.getOptarg());
						break;
					case 'Q':
						queueName = gopt.getOptarg();
						break;
					case 'R':
						tokens = new StringTokenizer(gopt.getOptarg(), "/");
						if (tokens.countTokens() != 2)
						{
							error = true;
							break;
						}
						char mult = 0;
						String rate = tokens.nextToken();
						if (Character.isLetter(rate.charAt(rate.length()-1)))
						{
							mult = rate.charAt(rate.length()-1);
							rate = rate.substring(0, rate.length()-1);
						}
						
						rm_rate = Integer.parseInt(rate);
						
						switch(mult)
						{
							case 'k': case 'K':
								rm_rate *= 1000;
								break;
							case 'm': case 'M':
								rm_rate *= 1000000;
								break;
							case 'g': case 'G':
								rm_rate *= 1000000000;
								break;
							default:
								error = true;
								break;
						}
						String pct = tokens.nextToken();
						mult = 0;
						if (Character.isLetter(pct.charAt(pct.length()-1))
							|| pct.charAt(pct.length()-1) == '%')
						{
							mult = pct.charAt(pct.length()-1);
							pct = pct.substring(0, pct.length()-1);
						}

						rm_retrans = Integer.parseInt(pct);
	
						switch(mult)
						{
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
							default:
								error = true;
								break;
						}
						break;
					case 's':
						stats_sec = Integer.parseInt(gopt.getOptarg());
						break;
					case 'T':
						stability=true;
						break;
					case 'X':
						index_info = new UMQIndexInfo();
						
						try {
							long numeric_index = Long.parseLong(gopt.getOptarg());
							index_info.setNumericIndex(numeric_index);
						}
						catch(NumberFormatException e) {
							byte[] index = gopt.getOptarg().getBytes("ASCII");
							try {
								index_info.setIndex(index, index.length);
							}
							catch (LBMEInvalException ex) {
								System.err.println("Error setting UMQ index: " + ex.toString());
								System.exit(1);
							}
						}
						
						break;
					case 'v':
						verbose++;
						break;
					default:
						error = true;
						break;
				}
				if (error)
					break;
			} 
			catch (Exception e) 
			{
				/* type conversion exception */
				System.err.println("umqsrc: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error || gopt.getOptind() >= args.length)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
			topicString = args[gopt.getOptind()];
			/* Load LBM/UME configuration from file */
		if (conffname != null)
		{
			try
			{
				LBM.setConfiguration(conffname);
			}
			catch (LBMException ex)
			{
				System.err.println("Error setting LBM configuration: " + ex.toString());
				System.exit(1);
			}
		}
		/* Verify the Message Rate and Flight Time args */
		if(msgs_per_sec > 0 && pause_ivl > 0) {
			System.err.println("-m and -P are conflicting options");
			System.err.println(usage);
			System.exit(1);
		}

		/* Make sure we don't have source options with immediate messages */
		if(immediate)
		{
			System.err.print("Submitting Immediate Messages.");
			if(monitor_source) {
				System.err.print("  Cannot monitor source");
				System.err.println(usage);
				System.exit(1);
			}

			if(stats_sec>0) {
				System.err.print("  Cannot print source stats");
				System.err.println(usage);
				System.exit(1);
			}
			System.err.println();
        }

		byte [] message = new byte[msglen];
		UMQSrcCB srccb = new UMQSrcCB(verbose);
		LBMContextAttributes cattr = null;
		try
		{
			cattr = new LBMContextAttributes();
			// Since we are manually validating attributes, retrieve any XML
			// configuration attributes set for this context.
			cattr.setFromXml(cattr.getValue("context_name"));
			cattr.setObjectRecycler(objRec, null);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context attributes: " + ex.toString());
			System.exit(1);
		}
		LBMSourceAttributes sattr = null;
		UMQLongObject cd = new UMQLongObject();
		if(!immediate)
		{
			try
		    {
				sattr = new LBMSourceAttributes();
				// Since we are manually validating attributes, retrieve any XML
				// configuration attributes set for this topic.
				sattr.setFromXml(cattr.getValue("context_name"), topicString);
				sattr.setObjectRecycler(objRec, null);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating source attributes: " + ex.toString());
				System.exit(1);
			}
			sattr.setMessageReclamationCallback(srccb, cd);
		}
		UMQCtxCB ctxcb = new UMQCtxCB(verbose);
		cattr.setContextEventCallback(ctxcb);
		cattr.setContextSourceEventCallback(srccb);

		if(stability)
		{
			try
			{
				cattr.setProperty("umq_message_stability_notification", "1");
            }
			catch(LBMRuntimeException ex)
			{
				System.err.println("Error setting stability notification: " + ex.toString());
				System.exit(1);
			}
		}
		if (!immediate) {
			if (rm_rate != 0)
			{
				try
				{
					sattr.setProperty("transport", "LBTRM");
					cattr.setProperty("transport_lbtrm_data_rate_limit",
									  Integer.toString(rm_rate));
					cattr.setProperty("transport_lbtrm_retransmit_rate_limit",
									  Integer.toString(rm_retrans));
				}
				catch (LBMRuntimeException ex)
				{
					System.err.println("Error setting LBTRM rate: " + ex.toString());
					System.exit(1);
				}
			}
			if (queueName != null)
			{
				try
				{
					sattr.setProperty("umq_queue_name", queueName);	
				}
				catch (LBMRuntimeException ex)
				{
					System.err.println("Error setting queue name: " + ex.toString());
				}
			}
			if (appsets != null)
			{
				try
				{
					sattr.setProperty("umq_ulb_application_set", appsets);
				}
				catch (LBMRuntimeException ex)
				{
					System.err.println("Error setting ULB application set: " + ex.toString());
				}
			}
			try 
			{
				queueName = sattr.getValue("umq_queue_name");
				appsets = sattr.getValue("umq_ulb_application_set");
			}
			catch (LBMException ex)
			{
				System.err.println("Error fetching source attributes: " + ex.toString());
				System.exit(1);
			}
		}

		if ((queueName == null || queueName.compareTo("") == 0) && (appsets == null || appsets.compareTo("") == 0)) {
			System.err.println("Queue name not set and ULB application set not set. Exiting.");
			System.exit(1);
		}
		else
		{
			if (queueName != null && queueName.compareTo("") != 0)
				System.out.println("Using Queue: " + queueName);
			if (appsets != null && appsets.compareTo("") != 0)
			{
				String events = null, assign_funcs = null, lf_behavior = null;

				System.out.println("Using ULB application set(s) \"" + appsets + "\"");
				try
				{
					events = sattr.getValue("umq_ulb_events");
					assign_funcs = sattr.getValue("umq_ulb_application_set_assignment_function");
					lf_behavior = sattr.getValue("umq_ulb_application_set_load_factor_behavior");
				}
				catch (LBMException ex)
				{
					System.err.println("Error fetching source attributes: " + ex.toString());
					System.exit(1);
				}
				if (flightsz > 0) {
					events = "MSG_COMPLETE|RCV_REGISTRATION|RCV_DEREGISTRATION|RCV_TIMEOUT" + ((events.compareTo("0") == 0) ? "" : ("|" + events));
				}
				if (verbose > 0) {
					events = "0xFF";
				}
				if (events.compareTo("0") != 0)
				{
					try
					{
						System.out.println(" ULB events: " + events);
						sattr.setProperty("umq_ulb_events", events);
					}
					catch (LBMRuntimeException ex)
					{
						System.err.println("Error setting ULB events: " + ex.toString());
						System.exit(1);
					}
				}
				System.out.println(" Assignment Function(s) \"" + assign_funcs + "\"");
				System.out.println(" Load Factor Behavior(s) \"" + lf_behavior + "\"");
			}
		}
		
		/* Override the flightsz if a message rate is set */
		if(msgs_per_sec > 0) flightsz = 0;
		if(flightsz > 0) {
			/* Create the flight size semaphore */
			flightlock = new Semaphore(0,true);
                        
			if(immediate) {
				/* Post to the semaphore so we can register. */
				flightlock.release();
			}
		}
		/* Calculate the appropriate message rate */
		if(msgs_per_sec > 0)
			calc_rate_vals();

		System.out.println(msgs_per_sec + " msgs/sec -> " + msgs_per_ivl + " msgs/ivl, "
					       + pause_ivl + " msec ivl " + flightsz + " inflight");
	
		if (cconffname != null)
		{
			try 
			{
				FileInputStream f = new FileInputStream(cconffname);
				cattr.load(f);
			}
			catch (IOException e)
			{
				System.err.println(e.toString());
				System.exit(1);
			}
			catch (LBMRuntimeException ex)
			{
				System.err.println("Error setting context configuration: " + ex.toString());
				System.exit(1);
			}
		}

		try
		{
			if(immediate) {
				if (cattr.getValue("umq_message_stability_notification").equals("1")) {
					System.out.print("Using UMQ Message Stability Notification. ");
					if (verbose >= 1)
					    System.out.println("Will display message stability events. ");
					else
						System.out.println(" Will not display events. ");
					stability = true;
				}
			    else if (flightsz > 0) {
					System.out.println("Enabling message stability notification to control unstablized message backlog");
					cattr.setValue("umq_message_stability_notification", "1");
					stability = true;
			    }
				else
					System.out.println("Not using UMQ Message Stability Notification.");

			} else {
				if (sattr.getValue("umq_message_stability_notification").equals("1")) {
				    System.out.print("Using UMQ Message Stability Notification. ");
					if (verbose >= 1)
	        			System.out.println("Will display message stability events. ");
					else
						System.out.println(" Will not display events. ");
					stability = true;
				}
				else if (flightsz > 0) {
					System.out.println("Enabling message stability notification to control unstablized message backlog");
					sattr.setValue("umq_message_stability_notification", "1");
					stability = true;
				}
			    else
				    System.out.println("Not using UME Message Stability Notification.");
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error checking stability settings: " + ex.toString());
			System.exit(1);
		}
		try
		{
			if (sequential)
			{
				cattr.setProperty("operational_mode", "sequential");
			}
			else
			{
				// The default for operational_mode is embedded, but set it
				// explicitly in case a configuration file was specified with
				// a different value.
				cattr.setProperty("operational_mode", "embedded");
			}
		}
		catch (LBMRuntimeException ex)
		{
			System.err.println("Error setting operational_mode: " + ex.toString());
			System.exit(1);
		}
		if (!immediate && sattr.size() > 0)
			sattr.list(System.out);
		if (cattr.size() > 0)
			cattr.list(System.out);
		LBMContext ctx = null;
		try
		{
			ctx = new LBMContext(cattr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}

		LBMSource src = null;
		if(!immediate) {
			LBMTopic topic = null;
			try
			{
					topic =  ctx.allocTopic(topicString, sattr);
			}
			catch (LBMException ex)
			{
					System.err.println("Error allocating topic: " + ex.toString());
					System.exit(1);
			}
			try
			{
					src = ctx.createSource(topic, srccb, null, null);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating source: " + ex.toString());
				System.exit(1);
			}
			UMQSrcStatsTimer stats = null;
			if (stats_sec > 0)
			{
				try
				{
					stats = new UMQSrcStatsTimer(ctx, src, stats_sec * 1000, objRec);
				}
				catch (LBMException ex)
				{
					System.err.println("Error creating timer: " + ex.toString());
					System.exit(1);
				}
			}
		}
		LBMContextThread ctxthread = null;
		if (sequential)
		{
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		if (sequential)
		{
			System.err.println("Sequential mode");
		}
		else
		{
			System.err.println("Embedded mode");
		}
		try
		{
			Thread.sleep(1000);
		}
		catch (InterruptedException e) { }
		LBMMonitorSource lbmmonsrc = null;
		if (monitor_context || monitor_source)
		{
			try
			{
				lbmmonsrc = new LBMMonitorSource(mon_format, mon_format_options, mon_transport, mon_transport_options);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating monitor source: " + ex.toString());
				System.exit(1);
			}
			try
			{
				if (monitor_context)
					lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
				else
					lbmmonsrc.start(src, application_id, monitor_source_ivl);
			}
			catch (LBMException ex)
			{
				System.err.println("Error starting monitoring: " + ex.toString());
				System.exit(1);
			}
		}
		System.out.println("Sending "
				   + msgs
				   + " messages of size "
				   + msglen
				   + " bytes to topic ["
				   + topicString
				   + "]");
	    System.out.flush();
		long start_time = System.currentTimeMillis();
		boolean regProblem = false;
		LBMSourceSendExInfo exinfo = new LBMSourceSendExInfo();
		for (long count = 0; count < msgs; )
		{
			
			if (index_info != null) {
				exinfo.setFlags(LBM.SRC_SEND_EX_FLAG_UMQ_INDEX | exinfo.flags());
				exinfo.setIndexInfo(index_info);
			}
			
		    for(int ivlcount = 0; ivlcount < msgs_per_ivl; ivlcount++) 
			{
				if (seqnum_info || stability)
				{
					exinfo.setClientObject(new Long(count + 1));
					last_clientd_sent = count + 1;
				}
				try
				{
					int xflag = 0;

					srccb.blocked = true;
					if (seqnum_info) {
						exinfo.setFlags(LBM.SRC_SEND_EX_FLAG_SEQUENCE_NUMBER_INFO | exinfo.flags());
					}
					if(msg_id_info) {
						exinfo.setFlags(LBM.SRC_SEND_EX_FLAG_UMQ_MESSAGE_ID_INFO | exinfo.flags());
					}
					if(flightsz > 0) {
						boolean acquired = false;
						while(acquired == false) {
							try {
								flightlock.acquire();
								acquired = true;
								/* Sleep a bit after a re-registration
								 * to allow topic resolution to take place. */
								if (sleep_before_sending > 0) {
									Thread.sleep(sleep_before_sending);
									sleep_before_sending = 0;
								}
							} catch (InterruptedException ex) {
								/* Interrupted - retry */
							}
						}
						if(flightlock.availablePermits() <= 1) {
							xflag = LBM.MSG_FLUSH;
						}
					}
					if(immediate) {
						ctx.send(queueName, topicString, message, msglen, (block ? 0 : LBM.SRC_NONBLOCK) | xflag, exinfo);
					} else
						src.send(message, msglen, (block ? 0 : LBM.SRC_NONBLOCK) | xflag, exinfo);
					srccb.blocked = false;
					count++;
					appsent++;
				}
				catch (LBMEWouldBlockException ex)
				{
					while (srccb.blocked)
					{
						try
						{
							Thread.sleep(100);
						}
						catch (InterruptedException e) { }
					}
					continue;
				}
				catch(UMENoRegException ex)
				{
					if (!regProblem)
					{
						regProblem = true;
						System.out.println("Send unsuccessful. Waiting...");
						System.out.flush();
					}
					try
					{
						Thread.sleep(1000);
						umqsrc.appsent--;
					}
					catch (InterruptedException e) { }
					continue;
				}
				catch(UMENoQueueException ex)
				{
					if (!regProblem)
					{
						regProblem = true;
						System.out.println("Queue: Send unsuccessful. Waiting...");
					}
					try
					{
						Thread.sleep(1000);
						umqsrc.appsent--;
					}
					catch (InterruptedException e) { }
					continue;
				}
				catch(UMENoStoreException ex)
				{
					if (!regProblem)
					{
						regProblem = true;
						System.out.println("Store: Send unsuccessful. Waiting...");
					}
					try
					{
						Thread.sleep(1000);
						umqsrc.appsent--;
					}
					catch (InterruptedException e) { }
					continue;
				}
				catch (LBMException ex)
				{
					System.err.println("Error sending message: " + ex.toString());
				}
				if (regProblem)
				{
					regProblem = false;
					System.out.println("Send OK. Continuing.");
					System.out.flush();
				}
				bytes_sent += msglen;
		    } /* for(ivlcount) */
			if (pause_ivl > 0)
			{
				try
				{
					Thread.sleep(pause_ivl);
				}
				catch (InterruptedException e) { }
			}
		}
		long end_time = System.currentTimeMillis();
		double secs = (end_time - start_time) / 1000.;
		System.out.println("Sent "
				   + msgs
				   + " messages of size "
				   + msglen
				   + " bytes in "
				   + secs
				   + " seconds.");
		print_bw(secs, msgs, bytes_sent);
		System.out.flush();
		if (linger > 0)
		{
			System.out.println("Lingering for " + linger + " seconds...");
			try
			{
				Thread.sleep(linger * 1000);
			}
			catch (InterruptedException e) { }
		}
		if (sequential)
		{
			ctxthread.terminate();
		}
		if (lbmmonsrc != null)
		{
			try
			{
				lbmmonsrc.close();
			}
			catch (LBMException ex)
			{
				System.err.println("Error closing monitor source: " + ex.toString());
			}
		}
		objRec.close();
		try
		{
			src.close();
		}
		catch (LBMException ex)
		{
			System.err.println("Error closing source: " + ex.toString());
		}
		ctx.close();
		cd.done();
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	/*
 	 * Function that determines how to pace sending of messages to obtain a given
 	 * rate.  Given messages per second, calculates number of messages to send in 
 	 * a particular interval and the number of milliseconds to pause between 
 	 * intervals.
 	 */
	private static void calc_rate_vals()
	{
		int intervals_per_sec = 1000;

		pause_ivl = 20;
		intervals_per_sec = 1000/(pause_ivl);

		while(pause_ivl <= 1000 && msgs_per_sec % intervals_per_sec != 0)
		{
			pause_ivl++;
			while(((1000 % pause_ivl) != 0) && pause_ivl <= 1000)
				pause_ivl++;
			intervals_per_sec = 1000/pause_ivl;
		}
		msgs_per_ivl = msgs_per_sec/intervals_per_sec;
	}

	private static void print_bw(double sec, int msgs, long bytes)
	{
		double mps = 0;
		double bps = 0;
		double kscale = 1000;
		double mscale = 1000000;
		char mgscale = 'K';
		char bscale = 'K';
		
		if (sec == 0) return; /* avoid division by zero */

		mps = msgs/sec;
		bps = bytes*8/sec;
		if (mps <= mscale)
		{
			mgscale = 'K';
			mps /= kscale;
		}
		else
		{
			mgscale = 'M';
			mps /= mscale;
		}
		if (bps <= mscale)
		{
			bscale = 'K';
			bps /= kscale;
		}
		else
		{
			bscale = 'M';
			bps /= mscale;
		}
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
		System.out.println(sec
				   + " secs. "
				   + nf.format(mps)
				   + " " + mgscale + "msgs/sec. "
				   + nf.format(bps)
				   + " " + bscale + "bps");
	}

}

class UMQLongObject
{
	public long value = 0;
	
	public void done()
	{
	}
}

class UMQCtxCB implements LBMContextEventCallback
{
	private int _verbose;

	public UMQCtxCB(int verbose)
	{
	      _verbose = verbose;
	}

	public int onContextEvent(Object arg, LBMContextEvent contextEvent)
	{

	    int i, semval;
	    switch (contextEvent.type())
	    {
			case LBM.CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX:
				UMQContextEventRegistrationCompleteInfo regcomp = contextEvent.registrationCompleteInfo();
				System.out.println("UMQ queue " + 
				   regcomp.queueName() + 
				   "[" + 
				   Long.toHexString(regcomp.queueId()) +
				   "] ctx registration complete. ID " +
				   regcomp.registrationId().toString(16) +
				   " FLags " +
				   Integer.toHexString(regcomp.flags()) +
				   (((regcomp.flags() & LBM.CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)== LBM.CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)?"QUORUM":""));
                if(umqsrc.immediate) {
                    if (umqsrc.flightsz > 0)
                    {
                        semval = umqsrc.flightlock.availablePermits();
                        for (i = (int)(umqsrc.flightsz - semval - (umqsrc.last_clientd_sent - umqsrc.last_clientd_stable)); i > 0; i--)
                        {
                            umqsrc.flightlock.release();
                        }
                    }
                }

				break;
	        case LBM.CONTEXT_EVENT_UMQ_REGISTRATION_SUCCESS_EX:
				UMQContextEventRegistrationSuccessInfo reginfo = contextEvent.registrationSuccessInfo();
				System.out.println("UMQ queue " + 
				   reginfo.queueName() + 
				   "[" + 
				   Long.toHexString(reginfo.queueId()) +
				   "][" + 
				   reginfo.queueInstanceName() + 
				   "][" + 
				   reginfo.queueInstanceIndex() + 
				   "] ctx registration. ID " +
				   reginfo.registrationId().toString(16) +
				   " FLags " +
				   Integer.toHexString(reginfo.flags()) +
				   (((reginfo.flags() & LBM.CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)== LBM.CONTEXT_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM)?"QUORUM":""));
				break;
	        case LBM.CONTEXT_EVENT_UMQ_REGISTRATION_ERROR:
				System.out.println("Error registering context with queue: " + contextEvent.dataString());
				break;
	        case LBM.CONTEXT_EVENT_UMQ_INSTANCE_LIST_NOTIFICATION:
				System.out.println("UMQ Instance list changed: " + contextEvent.dataString());
				break;
			default:
				System.out.println("Unknown context event: " + contextEvent.dataString());
				break;
	    }
		System.out.flush();
	    return 0;
	}
}

class UMQSrcCB implements LBMSourceEventCallback, LBMMessageReclamationCallback, LBMContextSourceEventCallback
{
	public boolean blocked = false;
	private int _verbose;
	private int force_reclaim_total = 0;
	private int lastcount = -1;

	public UMQSrcCB(int verbose)
	{
		_verbose = verbose;
	}

	public int onContextSourceEvent(Object arg, LBMContextSourceEvent contextSourceEvent)
	{
			return onSourceEvent(null, contextSourceEvent);
	}

	public int onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
	{
		long count;
		int i, semval;

		switch (sourceEvent.type())
		{
			case LBM.SRC_EVENT_CONNECT:
				System.out.println("Receiver connect " + sourceEvent.dataString());
				break;
			case LBM.SRC_EVENT_DISCONNECT:
				System.out.println("Receiver disconnect " + sourceEvent.dataString());
				break;
			case LBM.SRC_EVENT_WAKEUP:
				blocked = false;
				break;
			case LBM.SRC_EVENT_UME_STORE_UNRESPONSIVE:
					System.out.println("UME store: "
						+ sourceEvent.dataString());
				break;
			case LBM.SRC_EVENT_SEQUENCE_NUMBER_INFO:
				LBMSourceEventSequenceNumberInfo info = sourceEvent.sequenceNumberInfo();
				if (info.firstSequenceNumber() != info.lastSequenceNumber()) {
					System.out.println("SQN [" + info.firstSequenceNumber()
							+ "," + info.lastSequenceNumber() + "] (cd "
							+ info.clientObject() + ")");
				}
				else {
					System.out.println("SQN " + info.lastSequenceNumber()
							+ " (cd " + info.clientObject() + ")");
				}
				break;
			case LBM.SRC_EVENT_UMQ_REGISTRATION_COMPLETE_EX:
				UMQSourceEventRegistrationCompleteInfo q_reg_complete = sourceEvent.queueRegistrationCompleteInfo();
				if(_verbose > 0) {
					System.out.println("UMQ " 
				    + q_reg_complete.queueName() 
				    + "["
				    + Long.toHexString(q_reg_complete.queueId())
				    + "] src registration complete. Flags "
				    + Integer.toHexString(q_reg_complete.flags())
				    + (((q_reg_complete.flags() & LBM.SRC_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM) == LBM.SRC_EVENT_UMQ_REGISTRATION_COMPLETE_EX_FLAG_QUORUM) ? "QUORUM":""));
				}
				if (umqsrc.flightsz > 0)
				{
				    semval = umqsrc.flightlock.availablePermits();
				    for (i = (int)(umqsrc.flightsz - semval - (umqsrc.last_clientd_sent - umqsrc.last_clientd_stable)); i > 0; i--)
				    {
					umqsrc.flightlock.release();
				    }
				}
				break;
			case LBM.SRC_EVENT_UMQ_MESSAGE_ID_INFO:
				UMQSourceEventMessageIdInfo msgInfo = sourceEvent.messageIdInfo();
				System.out.println("ID [" 
				    + msgInfo.messageId().registrationId().toString(16)
				    + ":"
				    + msgInfo.messageId().msgStamp().toString(16)
				    + "]");
				break;
			case LBM.SRC_EVENT_UMQ_MESSAGE_STABLE_EX:
				UMQSourceEventAckInfo q_ack = sourceEvent.queueAckInfo();
				if(_verbose > 0) {
					System.out.print("UMQ "
						+ q_ack.queueName()
						+ "["
						+ Long.toHexString(q_ack.queueId())
						+ "]["
						+ q_ack.queueInstanceName()
						+ "]["
						+ q_ack.queueInstanceIndex()
						+ "]: message ["
						+ q_ack.messageIdInfo().registrationId().toString(16)
						+ ":"
						+ q_ack.messageIdInfo().msgStamp().toString(16)
						+ "] stable. Flags "
						+ Integer.toHexString(q_ack.flags()));
				if((q_ack.flags() & LBM.SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) != 0)
					System.out.print("IA ");
				if((q_ack.flags() & LBM.SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_INTERGROUP_STABLE) != 0)
					System.out.print("IR ");
				if((q_ack.flags() & LBM.SRC_EVENT_UMQ_MESSAGE_STABLE_EX_FLAG_STABLE) != 0)
					System.out.print("STABLE ");
				System.out.println();
				}
				/* Peg the counter for the received stable message */
				umqsrc.stablerecv++;

				count = ((Long)q_ack.clientObject()).longValue();
				if (umqsrc.flightsz > 0)
				{
				    semval = umqsrc.flightlock.availablePermits();
				    for (i = ((int)(count - umqsrc.last_clientd_stable)) > (umqsrc.flightsz - semval) ? (umqsrc.flightsz - semval) : ((int)(count - umqsrc.last_clientd_stable)); i > 0; i--)
				    {
					umqsrc.flightlock.release();
				    }
				    umqsrc.last_clientd_stable = count;
				}
				break;
			case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED:
				if (_verbose > 0)
					System.out.println("Message reclaimed - sequence number "
						+ Long.toHexString(sourceEvent.sequenceNumber())
						+ " (cd "
						+ Long.toHexString(((Long)sourceEvent.clientObject()).longValue())
						+ ")");
				break;
			case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
				UMESourceEventAckInfo reclaiminfo = sourceEvent.ackInfo();
				if (_verbose > 0) {
					System.out.print("UME message reclaimed (ex) - sequence number "
						+ Long.toHexString(reclaiminfo.sequenceNumber())
						+ " (cd "
						+ Long.toHexString(((Long)reclaiminfo.clientObject()).longValue())
						+ "). Flags 0x"
						+ reclaiminfo.flags());
					if ((reclaiminfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED_EX_FLAG_FORCED) != 0) {
						System.out.print(" FORCED");
					}
					System.out.println();
				}
				break;
			case LBM.SRC_EVENT_UMQ_REGISTRATION_ERROR:
				System.out.println("Error registering source with UMQ queue: " + sourceEvent.dataString());
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_RECEIVER_REGISTRATION_EX:
				UMQSourceEventULBReceiverInfo rcvInfo = sourceEvent.ulbReceiverInfo();

				if (umqsrc.initial_ulb_reg > 0) {
					if (umqsrc.flightsz > 0) {
						semval = umqsrc.flightlock.availablePermits();
						for (i = (umqsrc.flightsz - semval); i > 0; i--) {
							umqsrc.flightlock.release();
						}
					}
					umqsrc.initial_ulb_reg = 0;
				}
				System.out.println("UMQ ULB [" + Long.toHexString(rcvInfo.registrationId()) + "][" + Long.toHexString(rcvInfo.assignmentId()) + "][" +
								   rcvInfo.applicationSetIndex() + "] receiver [" + rcvInfo.receiver() + "] registration.");
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_RECEIVER_DEREGISTRATION_EX:
				UMQSourceEventULBReceiverInfo drcvInfo = sourceEvent.ulbReceiverInfo();

				System.out.println("UMQ ULB [" + Long.toHexString(drcvInfo.registrationId()) + "][" + Long.toHexString(drcvInfo.assignmentId()) + "][" +
								   drcvInfo.applicationSetIndex() + "] receiver [" + drcvInfo.receiver() + "] deregistration.");
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_RECEIVER_READY_EX:
				UMQSourceEventULBReceiverInfo rrcvInfo = sourceEvent.ulbReceiverInfo();

				System.out.println("UMQ ULB [" + Long.toHexString(rrcvInfo.registrationId()) + "][" + Long.toHexString(rrcvInfo.assignmentId()) + "][" +
								   rrcvInfo.applicationSetIndex() + "] receiver [" + rrcvInfo.receiver() + "] ready for messages.");
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_RECEIVER_TIMEOUT_EX:
				UMQSourceEventULBReceiverInfo trcvInfo = sourceEvent.ulbReceiverInfo();

				System.out.println("UMQ ULB [" + Long.toHexString(trcvInfo.registrationId()) + "][" + Long.toHexString(trcvInfo.assignmentId()) + "][" +
								   trcvInfo.applicationSetIndex() + "] receiver [" + trcvInfo.receiver() + "] EOL.");
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_MESSAGE_CONSUMED_EX:
				UMQSourceEventULBMessageInfo cnmsgInfo = sourceEvent.ulbMessageInfo();

				if (_verbose > 0) {
					System.out.println("UMQ ULB message [" + cnmsgInfo.messageIdInfo().registrationId().toString(16) + ":" +
									   cnmsgInfo.messageIdInfo().msgStamp().toString(16) + "] consumed by [" + 
									   Long.toHexString(cnmsgInfo.registrationId()) + "][" + Long.toHexString(cnmsgInfo.assignmentId()) + "][" +
									   cnmsgInfo.applicationSetIndex() + "][" + cnmsgInfo.receiver() + "]");
				}
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_MESSAGE_ASSIGNED_EX:
				UMQSourceEventULBMessageInfo amsgInfo = sourceEvent.ulbMessageInfo();

				if (_verbose > 0) {
					System.out.println("UMQ ULB message [" + amsgInfo.messageIdInfo().registrationId().toString(16) + ":" +
									   amsgInfo.messageIdInfo().msgStamp().toString(16) + "] assigned to [" + 
									   Long.toHexString(amsgInfo.registrationId()) + "][" + Long.toHexString(amsgInfo.assignmentId()) + "][" +
									   amsgInfo.applicationSetIndex() + "][" + amsgInfo.receiver() + "]");
				}
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_MESSAGE_REASSIGNED_EX:
				UMQSourceEventULBMessageInfo ramsgInfo = sourceEvent.ulbMessageInfo();

				if (_verbose > 0) {
					System.out.println("UMQ ULB message [" + ramsgInfo.messageIdInfo().registrationId().toString(16) + ":" +
									   ramsgInfo.messageIdInfo().msgStamp().toString(16) + "] reassigned from [" + 
									   Long.toHexString(ramsgInfo.registrationId()) + "][" + Long.toHexString(ramsgInfo.assignmentId()) + "][" +
									   ramsgInfo.applicationSetIndex() + "][" + ramsgInfo.receiver() + "]");
				}
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_MESSAGE_TIMEOUT_EX:
				UMQSourceEventULBMessageInfo tmsgInfo = sourceEvent.ulbMessageInfo();

				if (_verbose > 0) {
					System.out.println("UMQ ULB message [" + tmsgInfo.messageIdInfo().registrationId().toString(16) + ":" +
									   tmsgInfo.messageIdInfo().msgStamp().toString(16) + "] EOL [" + tmsgInfo.applicationSetIndex() + "]");
				}
				break;
		    case LBM.SRC_EVENT_UMQ_ULB_MESSAGE_COMPLETE_EX:
				UMQSourceEventULBMessageInfo cmmsgInfo = sourceEvent.ulbMessageInfo();

				if (_verbose > 0) {
					System.out.println("UMQ ULB message [" + cmmsgInfo.messageIdInfo().registrationId().toString(16) + ":" +
									   cmmsgInfo.messageIdInfo().msgStamp().toString(16) + "] complete");
				}
				umqsrc.stablerecv++;
				if (umqsrc.flightsz > 0) {
					umqsrc.flightlock.release();
				}
				break;
			case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION:
				if (_verbose > 0) {
					LBMSourceEventFlightSizeNotification note = sourceEvent.flightSizeNotification();
					System.out.print("Flight Size Notification. Type ");
					switch (note.type()) {
						case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UME:
							System.out.print("UME");
							break;
						case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_ULB:
							System.out.print("ULB");
							break;
						case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UMQ:
							System.out.print("UMQ");
							break;
						default:
							System.out.print("unknown");
							break;
					}
					System.out.println(". Inflight is "
						+ (note.state() == LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_STATE_OVER ? "OVER" : "UNDER")
						+ " specified flight size");
				}
				break;
			default:
				System.out.println("Unknown source event "
					+ sourceEvent.type() );
				break;
		}
		System.out.flush();
        sourceEvent.dispose();
		return 0;
	}

	public void onMessageReclaim(Object clientd, String topic, long sqn)
	{
		UMQLongObject t = (UMQLongObject)clientd;
		if (t == null)
		{
			System.err.println("WARNING: source for topic \"" + topic + "\" forced reclaim 0x" + Long.toString(sqn, 16));
		}
		else
		{
			long endt = System.currentTimeMillis();
			endt -= t.value;
			force_reclaim_total++;
			if (endt > 5000)
			{
				System.err.println("WARNING: source for topic \"" + topic + "\" forced reclaim. Total " + force_reclaim_total);
				t.value = System.currentTimeMillis();
			}
		}
	}
}

class UMQSrcStatsTimer extends LBMTimer
{
	LBMSource _src;
	boolean _done = false;
	long _tmo;
	LBMObjectRecyclerBase _recycler;

	public UMQSrcStatsTimer(LBMContext ctx, LBMSource src, long tmo, LBMObjectRecyclerBase objRec) throws LBMException
	{
		super(ctx, tmo, null);
		_src = src;
		_tmo = tmo;
		_recycler = objRec;
	}

	public void done()
	{
		_done = true;
	}

	private void onExpiration()
	{
		print_stats();
		if (!_done)
		{
			try
			{
				this.reschedule(_tmo);
			}
			catch (LBMException ex)
			{
				System.err.println("Error rescheduling timer: " + ex.toString());
			}
		}
	}

	private void print_stats()
	{
		try
		{
			LBMSourceStatistics stats = _src.getStatistics();
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.out.println("TCP, buffered "
								+ stats.bytesBuffered()
								+ ", clients "
								+ stats.numberOfClients()
								+ ", app sent "
								+ umqsrc.appsent
								+ ", stable "
								+ umqsrc.stablerecv
								+ ", inflight "
								+ (umqsrc.stablerecv > umqsrc.appsent ? 
									umqsrc.stablerecv - umqsrc.appsent :
									umqsrc.appsent - umqsrc.stablerecv));
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
					System.out.println("LBT-RU, sent "
								+ stats.messagesSent()
								+ "/"
								+ stats.bytesSent()
								+ ", naks "
								+ stats.naksReceived()
								+ "/"
								+ stats.nakPacketsReceived()
								+ ", ignored "
								+ stats.naksIgnored()
								+ "/"
								+ stats.naksIgnoredRetransmitDelay()
								+ ", shed "
								+ stats.naksShed()
								+ ", rxs "
								+ stats.retransmissionsSent()
								+ ", clients "
								+ stats.numberOfClients()
								+ ", app sent "
								+ umqsrc.appsent
								+ ", stable "
								+ umqsrc.stablerecv
								+ ", inflight "
								+ (umqsrc.stablerecv > umqsrc.appsent ? 
									umqsrc.stablerecv - umqsrc.appsent :
									umqsrc.appsent - umqsrc.stablerecv));
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.out.println("LBT-RM, sent "
								+ stats.messagesSent()
								+ "/"
								+ stats.bytesSent()
								+ ", txw "
								+ stats.transmissionWindowMessages()
								+ "/"
								+ stats.transmissionWindowBytes()
								+ ", naks "
								+ stats.naksReceived()
								+ "/"
								+ stats.nakPacketsReceived()
								+ ", ignored "
								+ stats.naksIgnored()
								+ "/"
								+ stats.naksIgnoredRetransmitDelay()
								+ ", shed "
								+ stats.naksShed()
								+ ", rxs "
								+ stats.retransmissionsSent()
								+ ", rctl "
								+ stats.messagesQueued()
								+ "/"
								+ stats.retransmissionsQueued()
								+ ", app sent "
								+ umqsrc.appsent
								+ ", stable "
								+ umqsrc.stablerecv
								+ ", inflight "
								+ (umqsrc.stablerecv > umqsrc.appsent ? 
									umqsrc.stablerecv - umqsrc.appsent :
									umqsrc.appsent - umqsrc.stablerecv));
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.out.println("LBT-IPC, clients "
								+ stats.numberOfClients()
								+ ", sent " 
								+ stats.messagesSent()
								+ "/"
								+ stats.bytesSent()
								+ ", app sent "
								+ umqsrc.appsent
								+ ", stable "
								+ umqsrc.stablerecv
								+ ", inflight "
								+ (umqsrc.stablerecv > umqsrc.appsent ? 
									umqsrc.stablerecv - umqsrc.appsent :
									umqsrc.appsent - umqsrc.stablerecv));
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.out.println("LBT-RDMA, clients "
								+ stats.numberOfClients()
								+ ", sent " 
								+ stats.messagesSent()
								+ "/"
								+ stats.bytesSent()
								+ ", app sent "
								+ umqsrc.appsent
								+ ", stable "
								+ umqsrc.stablerecv
								+ ", inflight "
								+ (umqsrc.stablerecv > umqsrc.appsent ? 
									umqsrc.stablerecv - umqsrc.appsent :
									umqsrc.appsent - umqsrc.stablerecv));
					break;
			}
			if(_recycler != null) {
				_recycler.doneWithSourceStatistics(stats);
			}
			System.out.flush();
		}
		catch (LBMException ex)
		{
			System.err.println("Error getting source statistics: " + ex.toString());
		}
	}
}

