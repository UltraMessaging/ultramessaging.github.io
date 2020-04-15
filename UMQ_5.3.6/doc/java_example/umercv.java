import com.latencybusters.lbm.*;
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

class umercv {

	private static final int DEFAULT_MAX_NUM_SRCS = 10000;
	private static int nstats = 10;
	private static int reap_msgs = 0;
	private static int stat_secs = 0;
	private static boolean eventq = false;
	private static boolean sequential = true;
	private static boolean verbose = false;
	private static boolean end_on_eos = false;
	private static boolean dereg = false;
	private static boolean summary = false;
	private static String purpose = "Purpose: Receive messages on a single topic.";

	private static String usage = "Usage: umercv [options] topic\n"
			+ "Available options:\n"
			+ "  -1 act as a UME 1.2 and earlier receiver would act\n"
			//+ "  -A display messages as ASCII text\n" // TODO:  implement ASCII option
			+ "  -c filename = read config file filename\n"
			+ "  -E = exit after source ends\n"
			+ "  -D = Deregister after receiving 1000 messages\n"
			+ "  -e = use LBM embedded mode\n"
			+ "  -X num_msgs = send an eXplicit ACK every num_msgs messages\n"
			+ "  -i offset = use offset to calculate Registration ID\n"
			+ "              (as source registration ID + offset)\n"
			+ "              offset of 0 forces creation of regid by store\n"
			+ "  -N seqnum_offset = display recovery sequence number info and set low seqnum to low+seqnum_offset\n"
			+ "  -S = exit after source ends, print throughput summary\n"
			+ "  -s num_secs = print statistics every num_secs along with bandwidth\n"
			+ "  -h = help\n"
			+ "  -q = use an LBM event queue\n"
			+ "  -r msgs = delete receiver after msgs messages\n"
			+ "  -v = be verbose about each message\n"
			//+ "  -V = verify message contents\n" // TODO:  implement verified messages
			+ "\nMonitoring options:\n"
			+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
			+ "  --monitor-rcv NUM = monitor receiver every NUM seconds\n"
			+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
			+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
			+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
			+ "  --monitor-format FMT = use monitor format module FMT\n"
			+ "                         FMT may be `csv'\n"
			+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
			+ "  --monitor-appid ID = use ID as application ID string\n";

	private static LBMContextThread ctxthread = null;
	
	

	public static void main(String[] args) {
		String conffname = null;
		int monitor_context_ivl = 0;
		boolean monitor_context = false;
		int monitor_receiver_ivl = 0;
		boolean monitor_receiver = false;
		int mon_transport = LBMMonitor.TRANSPORT_LBM;
		int mon_format = LBMMonitor.FORMAT_CSV;
		String mon_format_options = "";
		String mon_transport_options = "";
		String application_id = null;
		String regid_offset = null;
		boolean compat10 = false;
		int exack = 0;
		long seqnum_offset = 0;
		boolean sqn_info = false;
		LBMObjectRecycler objRec = new LBMObjectRecycler();
		
		LBM lbm = null;
		try {
			lbm = new LBM();
		} catch (LBMException ex) {
			System.err.println("Error initializing LBM: " + ex.toString());
			System.exit(1);
		}
		org.apache.log4j.Logger logger;
		logger = org.apache.log4j.Logger.getLogger("umercv");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		LongOpt[] longopts = new LongOpt[7];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_RCV = 3;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5;
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT,
				null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-rcv", LongOpt.REQUIRED_ARGUMENT,
				null, OPTION_MONITOR_RCV);
		longopts[2] = new LongOpt("monitor-transport",
				LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[3] = new LongOpt("monitor-transport-opts",
				LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[4] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT,
				null, OPTION_MONITOR_FORMAT);
		longopts[5] = new LongOpt("monitor-format-opts",
				LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[6] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT,
				null, OPTION_MONITOR_APPID);

		Getopt gopt = new Getopt("umercv", args, "+c:eEDi:r:R:s:Shqv1X:N:", longopts);
		int c = -1;
		boolean error = false;
		while ((c = gopt.getopt()) != -1) 
	    {
			try {
				switch (c) {
					case OPTION_MONITOR_APPID:
						application_id = gopt.getOptarg();
						break;
					case OPTION_MONITOR_CTX:
						monitor_context = true;
						monitor_context_ivl = Integer.parseInt(gopt.getOptarg());
						break;
					case OPTION_MONITOR_RCV:
						monitor_receiver = true;
						monitor_receiver_ivl = Integer.parseInt(gopt.getOptarg());
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
					case 'c':
						conffname = gopt.getOptarg();
						break;
					case 'E':
						end_on_eos = true;
						break;
					case 'D':
						dereg = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'S':
						end_on_eos = true;
						summary = true;
						break;
					case 's':
						stat_secs = Integer.parseInt(gopt.getOptarg());
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						regid_offset = gopt.getOptarg();
						break;
					case 'N':
						sqn_info = true;
						seqnum_offset = Long.parseLong(gopt.getOptarg());
						break;
					case 'q':
						eventq = true;
						break;
					case 'r':
						reap_msgs = Integer.parseInt(gopt.getOptarg());
						break;
					case 'v':
						verbose = true;
						break;
					case '1':
						compat10 = true;
						break;
					case 'X':
						exack = Integer.parseInt(gopt.getOptarg());
						break;
					default:
						error = true;
				}
				if (error)
					break;
			} catch (Exception e) {
				/* type conversion exception */
				System.err.println("umercv: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error || gopt.getOptind() >= args.length) {
			print_help_exit(1);
		}
		if (conffname != null) {
			try {
				LBM.setConfiguration(conffname);
			} catch (LBMException ex) {
				System.err.println("Error setting LBM configuration: "
						+ ex.toString());
				System.exit(1);
			}
		}
		LBMContextAttributes ctx_attr = null;
		try {
			ctx_attr = new LBMContextAttributes();
			ctx_attr.setObjectRecycler(objRec, null);
		} catch (LBMException ex) {
			System.err.println("Error creating context attributes: "
					+ ex.toString());
			System.exit(1);
		}
		try {
			if (sequential) {
				ctx_attr.setProperty("operational_mode", "sequential");
			} else {
				// The default for operational_mode is embedded, but set it
				// explicitly in case a configuration file was specified with
				// a different value.
				ctx_attr.setProperty("operational_mode", "embedded");
			}
		} catch (LBMRuntimeException ex) {
			System.err.println("Error setting operational mode: "
					+ ex.toString());
			System.exit(1);
		}
		LBMContext ctx = null;
		try {
			ctx = new LBMContext(ctx_attr);
		} catch (LBMException ex) {
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}
		LBMReceiverAttributes rcv_attr = null;
		try
		{
			rcv_attr = new LBMReceiverAttributes();
			rcv_attr.setObjectRecycler(objRec, null);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating receiver attributes: "
					+ ex.toString());
			System.exit(1);
		}
		
		if (regid_offset != null && compat10) {
			UMERegistrationIdCompat10 umeregidcompat10;
			umeregidcompat10 = new UMERegistrationIdCompat10(regid_offset);
			rcv_attr.setRegistrationIdCallback(umeregidcompat10, null);
			System.out.println("Will use RegID offset " + regid_offset + ".");
		}
		else if (regid_offset != null) {
			UMERegistrationId umeregid;
			umeregid = new UMERegistrationId(regid_offset);
			rcv_attr.setRegistrationIdCallback(umeregid, null);
			System.out.println("Will use RegID offset " + regid_offset + ".");
		}
		else {
			System.out.println("Will not use specific RegID.");
		}
			
		if (sqn_info) {
			UMERcvRecInfo umerecinfocb = new UMERcvRecInfo(seqnum_offset);
			rcv_attr.setRecoverySequenceNumberCallback(umerecinfocb, null);
			System.out.println("Will use seqnum info with low offset " + seqnum_offset + ".");
		}
		
		if (exack > 0) {
			try {
				rcv_attr.setValue("ume_explicit_ack_only", "1");
			}
			catch (LBMException e) {
				System.err.println("Error setting ume_explicit_ack_only=" + exack + e.toString());
				System.exit(1);
			}
		}

		LBMTopic topic = null;
		try {
			topic = ctx.lookupTopic(args[gopt.getOptind()], rcv_attr);
		} catch (LBMException ex) {
			System.err.println("Error looking up topic: " + ex.toString());
			System.exit(1);
		}
		UMERcvEventQueue evq = null;
		UMERcvReceiver rcv = new UMERcvReceiver(verbose, end_on_eos, summary, dereg, exack);
		LBMReceiver lbmrcv = null;
		try {
			if (eventq) {
				if (sequential) {
					System.err.println("Sequential mode with event queue in use");
				} else {
					System.err.println("Embedded mode with event queue in use");
				}
				try {
					evq = new UMERcvEventQueue();
				} catch (LBMException ex) {
					System.err.println("Error creating event queue: "
							+ ex.toString());
					System.exit(1);
				}
				lbmrcv = new LBMReceiver(ctx, topic, rcv, null, evq);
				ctx.enableImmediateMessageReceiver(evq);
			} else if (sequential) {
				System.err.println("No event queue, sequential mode");
				lbmrcv = new LBMReceiver(ctx, topic, rcv, null);
				ctx.enableImmediateMessageReceiver();
			} else {
				System.err.println("No event queue, embedded mode");
				lbmrcv = new LBMReceiver(ctx, topic, rcv, null);
				ctx.enableImmediateMessageReceiver();
			}
		} catch (LBMException ex) {
			System.err.println("Error creating receiver: " + ex.toString());
			System.exit(1);
		}
		rcv.setLBMReceiver(lbmrcv);

		// This immediate-mode receiver is *only* used for topicless
		// immediate-mode sends. Immediate sends that use a topic
		// are received with normal receiver objects.
		ctx.addImmediateMessageReceiver(rcv);
		System.out.flush();
		long start_time;
		long end_time;
		long last_lost = 0, lost_tmp = 0, lost = 0;
		if (sequential) {
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		LBMMonitorSource lbmmonsrc = null;
		if (monitor_context || monitor_receiver) {
			try {
				lbmmonsrc = new LBMMonitorSource(mon_format,
						mon_format_options, mon_transport,
						mon_transport_options);
			} catch (LBMException ex) {
				System.err.println("Error creating monitor source: "
						+ ex.toString());
				System.exit(1);
			}
			try {
				if (monitor_context)
					lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
				else
					lbmmonsrc.start(lbmrcv, application_id,
							monitor_receiver_ivl);
			} catch (LBMException ex) {
				System.err.println("Error enabling monitoring: "
						+ ex.toString());
				System.exit(1);
			}
		}
		LBMReceiverStatistics stats = null;
		boolean have_stats;
		long stat_millis = stat_secs * 1000;
		long stat_time = System.currentTimeMillis() + stat_millis;
		while (true) {
			start_time = System.currentTimeMillis();
			if (eventq) {
				evq.run(1000);
			} else {
				try {
					Thread.sleep(1000);
				} catch (InterruptedException e) { }
			}
			end_time = System.currentTimeMillis();
			
			have_stats = false;
			while (!have_stats){
				try{
					stats = lbmrcv.getStatistics(nstats);
					have_stats = true;
				}
				catch (LBMException ex){
					/* Double the number of stats passed to the API to be retrieved */
					/* Do so until we retrieve stats successfully or hit the max limit */
					nstats *= 2;
					if (nstats > DEFAULT_MAX_NUM_SRCS){
						System.err.println("Error getting receiver statistics: " + ex.toString());
						System.exit(1);
					}
					/* have_stats is still false */
				}
			}
			
			/* If we get here, we have the stats */
			try{
				lost = 0;
				for (int i = 0; i < stats.size(); i++){
					lost += stats.lost(i);
				}
				/* Account for loss in previous iteration */
				lost_tmp = lost;
				if (last_lost <= lost){
					lost -= last_lost;
				}
				else{
					lost = 0;
				}
				last_lost = lost_tmp;
				
				print_bw(end_time - start_time, rcv.msg_count, rcv.byte_count,
						rcv.unrec_count, lost, rcv.burst_loss, rcv.rx_msgs, rcv.otr_msgs);
				rcv.msg_count = 0;
				rcv.byte_count = 0;
				rcv.unrec_count = 0;
				rcv.burst_loss = 0;
				rcv.rx_msgs = 0;
				rcv.otr_msgs = 0;
				
				if (stat_secs != 0 && stat_time <= end_time){
					stat_time = System.currentTimeMillis() + stat_millis;
					print_stats(stats, evq);
				}
				objRec.doneWithReceiverStatistics(stats);
			}
			catch (LBMException ex){
				System.err.println("Error manipulating receiver statistics: " + ex.toString());
				System.exit(1);
			}
			
			if (reap_msgs != 0 && rcv.total_msg_count >= reap_msgs){
				break;
			}
		}
		if (ctxthread != null) {
			ctxthread.terminate();
		}
		if (lbmmonsrc != null) {
			try
			{
				lbmmonsrc.close();
			}
			catch (LBMException ex)
			{
				System.err.println("Error closing monitor source: " + ex.toString());
			}
		}
		System.err.println("Quitting.... received " + rcv.total_msg_count
				+ " messages");
		
		objRec.close();
		try
		{
			lbmrcv.close();
		}
		catch (LBMException ex)
		{
			System.err.println("Error closing receiver: " + ex.toString());
		}
		ctx.close();
	}
	
	private static void print_help_exit(int exit_value){
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private static void print_bw(long msec, long msgs, long bytes, long unrec, long lost, long burst_loss, long rx_msgs, long otr_msgs) {
		double sec;
		double mps = 0.0, bps = 0.0;
		double kscale = 1000.0, mscale = 1000000.0;
		char mgscale = 'K', bscale = 'K';

		sec = msec / 1000.;
		if (sec == 0) return; /* avoid division by zero */

		mps = ((double) msgs) / sec;
		bps = ((double) bytes * 8) / sec;
		if (mps <= mscale) {
			mgscale = 'K';
			mps /= kscale;
		} else {
			mgscale = 'M';
			mps /= mscale;
		}
		if (bps <= mscale) {
			bscale = 'K';
			bps /= kscale;
		} else {
			bscale = 'M';
			bps /= mscale;
		}
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
		
		if ((rx_msgs > 0) || (otr_msgs > 0)){
			System.err.print(sec + " secs. " + nf.format(mps) + " " + mgscale
				+ "msgs/sec. " + nf.format(bps) + " " + bscale + "bps" + " [RX: " + rx_msgs + "][OTR: " + otr_msgs + "]");
		}
		else{
			System.err.print(sec + " secs. " + nf.format(mps) + " " + mgscale
				+ "msgs/sec. " + nf.format(bps) + " " + bscale + "bps");
		}		
		
		if (lost != 0 || unrec != 0 || burst_loss != 0) {
			System.err.print(" [" + lost + " pkts lost, " + unrec
					+ " msgs unrecovered, " + burst_loss + " bursts]");
		}
		System.err.println();
	}

	private static void print_stats(LBMReceiverStatistics stats, LBMEventQueue evq) {
		
		try {
			if (evq != null){
				if ( Integer.parseInt(evq.getAttributeValue("queue_size_warning")) > 0)	{
					System.err.println("Event queue size: " + evq.queueSize());
				}
			}
			for (int i = 0; i < stats.size(); i++)
			{
				switch(stats.type(i))
				{
					case LBM.TRANSPORT_STAT_TCP:
						System.out.println("TCP, source " + stats.source(i)
													 + ", received "
													 + stats.messagesReceived(i)
													 + "/"
													 + stats.bytesReceived(i)
													 + ", no topics "
													 + stats.noTopicMessagesReceived(i)
													 + ", requests "
													 + stats.lbmRequestsReceived(i));
						break;
					case LBM.TRANSPORT_STAT_LBTRU:
					case LBM.TRANSPORT_STAT_LBTRM:
						if (stats.type() == LBM.TRANSPORT_STAT_LBTRU)
							System.out.println("LBT-RU");
						else
							System.out.println("LBT-RM");
						System.out.println(", source " + stats.source(i)
													 + ", received "
													 + stats.messagesReceived(i)
													 + "/"
													 + stats.bytesReceived(i)
													 + ", naks "
													 + stats.nakPacketsSent(i)
													 + "/"
													 + stats.naksSent(i)
													 + ", lost "
													 + stats.lost(i)
													 + ", ncfs "
													 + stats.ncfsIgnored(i)
													 + "/"
													 + stats.ncfsShed(i)
													 + "/"
													 + stats.ncfsRetransmissionDelay(i)
													 + "/"
													 + stats.ncfsUnknown(i)
													 + ", recovery "
													 + stats.minimumRecoveryTime(i)
													 + "/"
													 + stats.meanRecoveryTime(i)
													 + "/"
													 + stats.maximumRecoveryTime(i)
													 + ", nak tx "
													 + stats.minimumNakTransmissions(i)
													 + "/"
													 + stats.minimumNakTransmissions(i)
													 + "/"
													 + stats.maximumNakTransmissions(i)
													 + ", dup "
													 + stats.duplicateMessages(i)
													 + ", unrecovered "
													 + stats.unrecoveredMessagesWindowAdvance(i)
													 + "/"
													 + stats.unrecoveredMessagesNakGenerationTimeout(i)
													 + ", LBM msgs " + stats.lbmMessagesReceived(i)
													 + ", no topics "
													 + stats.noTopicMessagesReceived(i)
													 + ", requests "
													 + stats.lbmRequestsReceived(i));
						break;
					case LBM.TRANSPORT_STAT_LBTIPC:
						System.out.print("LBT-IPC, source "
									+ stats.source(i)
									+ ", received "
									+ stats.messagesReceived(i)
									+ " msgs/"
									+ stats.bytesReceived(i)
									+ " bytes. LBM msgs "
									+ stats.lbmMessagesReceived(i)
									+ ", no topics "
									+ stats.noTopicMessagesReceived(i)
									+ ", requests "
									+ stats.lbmRequestsReceived(i));
						break;
					case LBM.TRANSPORT_STAT_LBTRDMA:
						System.out.print("LBT-RDMA, source "
									+ stats.source(i)
									+ ", received "
									+ stats.messagesReceived(i)
									+ " msgs/"
									+ stats.bytesReceived(i)
									+ " bytes. LBM msgs "
									+ stats.lbmMessagesReceived(i)
									+ ", no topics "
									+ stats.noTopicMessagesReceived(i)
									+ ", requests "
									+ stats.lbmRequestsReceived(i));
						break;
				}
			}
			System.out.flush();
		} catch (LBMException ex) {
			System.err.println("Error initializing LBM: " + ex.toString());
			System.exit(1);
		}
	}	
}

class UMERcvEventQueue extends LBMEventQueue implements LBMEventQueueCallback {
	public UMERcvEventQueue() throws LBMException {
		super();
		addMonitor(this);
	}
	
	public void monitor(Object cbArg, int evtype, int evq_size, long evq_delay)
	{	
		System.err.println("Event Queue Monitor: Type: " + evtype +
			", Size: " + evq_size +
			", Delay: " + evq_delay + " usecs.");
	}
}

class UMERegistrationIdCompat10 implements LBMRegistrationIdCallback {
	private long _regid_offset;

	public UMERegistrationIdCompat10(String regid_offset) {
		try {
			_regid_offset = Long.parseLong(regid_offset);
		} catch (Exception ex) {
			System.err
					.println("Can't convert registration ID offset to a long: "
							+ ex.toString());
			System.exit(1);
		}
	}

	public long setRegistrationId(Object cbArg, String source, long sourceRegId) {
		long regid = (_regid_offset == 0 ? 0 : sourceRegId + _regid_offset);
		if (regid < 0) {
			System.out.println("Would have requested registration ID [" + regid + "], but negative registration IDs are invalid.");
			regid = 0;
		}
		System.out.println("Requesting registration ID [" + regid
				+ "], Source: " + source + "/" + sourceRegId);
		return regid;
	}
}

class UMERegistrationId implements UMERegistrationIdExCallback {
	private long _regid_offset;

	public UMERegistrationId(String regid_offset) {
		try {
			_regid_offset = Long.parseLong(regid_offset);
		} catch (Exception ex) {
			System.err
					.println("Can't convert registration ID offset to a long: "
							+ ex.toString());
			System.exit(1);
		}
	}

	public long setRegistrationId(Object cbArg, UMERegistrationIdExCallbackInfo cbInfo) {
		long regid = (_regid_offset == 0 ? 0 : cbInfo.sourceRegistrationId() + _regid_offset);
		if (regid < 0) {
			System.out.println("Would have requested registration ID [" + regid + "], but negative registration IDs are invalid.");
			regid = 0;
		}

		System.out.println("Store " + cbInfo.storeIndex() + ": " + cbInfo.store() + "["
				+ cbInfo.source() + "][" + cbInfo.sourceRegistrationId() + "] Flags " + cbInfo.flags()
				+ ". Requesting regid: " + regid);
		return regid;
	}
}


class UMERcvRecInfo implements UMERecoverySequenceNumberCallback {

	private long _seqnum_offset = 0;
	
	public UMERcvRecInfo(long seqnum_offset) {
		_seqnum_offset = seqnum_offset;
	}

	public int setRecoverySequenceNumberInfo(Object cbArg,
			UMERecoverySequenceNumberCallbackInfo cbInfo) {
		
		long new_low = cbInfo.lowSequenceNumber() + _seqnum_offset;
		if (new_low < 0) {
			System.out.println("New low sequence number would be negative.  Leaving low SQN unchanged.");
			new_low = cbInfo.lowSequenceNumber();
		}
		System.out.println("SQNs Low " + cbInfo.lowSequenceNumber() + " (will set to "
				+ new_low + "), Low rxreqmax " + cbInfo.lowRxReqMaxSequenceNumber()
				+ ", High " + cbInfo.highSequenceNumber());
		try {
			cbInfo.setLowSequenceNumber(new_low);
		}
		catch (LBMEInvalException e) {
			System.err.println(e.getMessage());
		}
		System.out.flush();
		return 0;
	}
	
}

class UMERcvReceiver implements LBMReceiverCallback,
		LBMImmediateMessageCallback {
	public long imsg_count = 0;

	public long msg_count = 0;

	public long total_msg_count = 0;

	public long subtotal_msg_count = 0;

	public long byte_count = 0;

	public long unrec_count = 0;

	public long total_unrec_count = 0;

	public long burst_loss = 0;

	public long rx_msgs = 0;

	public long otr_msgs = 0;

	public long data_start_time = 0;

	public long data_end_time = 0;

	public int stotal_msg_count = 0;

	public long total_byte_count = 0;

	boolean _verbose = false;

	boolean _end_on_eos = false;

	boolean _summary = false;
	
	int _exack = 0;

	boolean _dereg = false;

	public LBMReceiver _lbmrcv = null;
	

	public UMERcvReceiver(boolean verbose, boolean end_on_eos, boolean summary, boolean dereg, 
			int exack) {
		_verbose = verbose;
		_end_on_eos = end_on_eos;
		_summary = summary;
		_exack = exack;
		_dereg = dereg;
	}

	public void setLBMReceiver(  LBMReceiver lbmrcv)
	{
		_lbmrcv = lbmrcv;
	}

	// This immediate-mode receiver is *only* used for topicless
	// immediate-mode sends. Immediate sends that use a topic
	// are received with normal receiver objects.
	public int onReceiveImmediate(Object cbArg, LBMMessage msg) {
		imsg_count++;
		return onReceive(cbArg, msg);
	}

	public int onReceive(Object cbArg, LBMMessage msg) {
		switch (msg.type()) {
			case LBM.MSG_DATA:
				if (stotal_msg_count == 0)
					data_start_time = System.currentTimeMillis();
				else
					data_end_time = System.currentTimeMillis();
				msg_count++;
				total_msg_count++;
				stotal_msg_count++;
				subtotal_msg_count++;
				byte_count += msg.data().length;
				total_byte_count += msg.data().length;
				
				if ((msg.flags() & LBM.MSG_FLAG_UME_RETRANSMIT) != 0) {
					rx_msgs++;
				}
				if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0) {
					otr_msgs++;
				}
				if (total_msg_count == 1000)
				{
					if ( _dereg)
					{
						System.out.println(" Sending DEREGISTRATION IN MSG_DATA");
						try {
							_lbmrcv.umederegister();
						}
						catch (LBMException e) {
							System.err.println(e.getMessage());
						}
					}
				}
				
				if (_verbose) {
					System.err.print("[" + msg.topicName() + "][" + msg.source()
							+ "][" + msg.sequenceNumber() + "]");
					if ((msg.flags() & LBM.MSG_FLAG_UME_RETRANSMIT) != 0) {
						System.err.print("-RX-");
					}
					if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0) {
						System.err.print("-OTR-");
					}
					System.err.print(", ");
					System.err.println(msg.data().length + " bytes");
				}
				if (_exack > 0)
				{
					if ((msg.sequenceNumber() % _exack) == 0)
					{
						if (_verbose)
							System.out.println(" Sending Explicit ACK");
						try
						{
							msg.sendExplicitAck();
						}
						catch (LBMException e)
						{
							System.err.println("msg.sendExplicitAck(): " + e.getMessage());
						}
					}
				}
				break;
			case LBM.MSG_BOS:
				System.err.println("[" + msg.topicName() + "][" + msg.source()
						+ "], Beginning of Transport Session");
				break;
			case LBM.MSG_EOS:
				System.err.println("[" + msg.topicName() + "][" + msg.source()
						+ "], End of Transport Session");
				if (_end_on_eos) {
					if (_summary)
						print_summary();

					end();
				}
				subtotal_msg_count = 0;
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS:
				unrec_count++;
				total_unrec_count++;
				if (_verbose)
					System.err.println("[" + msg.topicName() + "][" + msg.source()
							+ "][" + Long.toHexString(msg.sequenceNumber())
							+ "], LOST");
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
				burst_loss++;
				if (_verbose)
					System.err.println("[" + msg.topicName() + "][" + msg.source()
							+ "][" + Long.toHexString(msg.sequenceNumber())
							+ "], LOST BURST");
				break;
			case LBM.MSG_REQUEST:
				if (stotal_msg_count == 0)
					data_start_time = System.currentTimeMillis();
				else
					data_end_time = System.currentTimeMillis();
				msg_count++;
				stotal_msg_count++;
				subtotal_msg_count++;
				byte_count += msg.data().length;
				total_byte_count += msg.data().length;
				if (total_msg_count == 1000)
				{
					if ( _dereg)
					{
						System.out.println(" Sending DEREGISTRATION IN REQ");
						try {
							_lbmrcv.umederegister();
						}
						catch (LBMException e) {
							System.err.println(e.getMessage());
						}
					}
				}
				break;
			case LBM.MSG_UME_REGISTRATION_ERROR:
				System.out.println("[" + msg.topicName() + "][" + msg.source()
						+ "] UME registration error: " + msg.dataString());
				break;
			case LBM.MSG_UME_REGISTRATION_SUCCESS:
				System.out.println("[" + msg.topicName() + "][" + msg.source()
						+ "] UME registration successful. Src RegID "
						+ msg.sourceRegistrationId() + " RegID "
						+ msg.receiverRegistrationId());
				break;
			case LBM.MSG_UME_REGISTRATION_SUCCESS_EX:
				UMERegistrationSuccessInfo reg = msg.registrationSuccessInfo();
				System.out.print("[" + msg.topicName() + "][" + msg.source()
						+ "] store " + reg.storeIndex() + ": "
						+ reg.store() + " UME registration successful. SrcRegID "
						+ reg.sourceRegistrationId() + " RcvRegID " + reg.receiverRegistrationId()
						+ ". Flags " + reg.flags() + " ");
				if ((reg.flags() & LBM.MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_OLD) != 0)
					System.out.print("OLD[SQN " + reg.sequenceNumber() + "] ");
				if ((reg.flags() & LBM.MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_NOCACHE) != 0)
					System.out.print("NOCACHE ");
				System.out.println();
				break;
			case LBM.MSG_UME_DEREGISTRATION_SUCCESS_EX:
				System.out.print("DEREGISTRATION SUCCESSFUL ");
				UMEDeregistrationSuccessInfo dereg = msg.deregistrationSuccessInfo();
				System.out.print("[" + msg.topicName() + "][" + msg.source()
						+ "] store " + dereg.storeIndex() + ": "
						+ dereg.store() + " UME deregistration successful. SrcRegID "
						+ dereg.sourceRegistrationId() + " RcvRegID " + dereg.receiverRegistrationId()
						+ ". Flags " + dereg.flags() + " ");
				System.out.println();
				break;
			case LBM.MSG_UME_DEREGISTRATION_COMPLETE_EX:
				System.out.print("DEREGISTRATION COMPLETE ");
				System.out.println();
				break;
			case LBM.MSG_UME_REGISTRATION_COMPLETE_EX:
				UMERegistrationCompleteInfo regcomplete = msg.registrationCompleteInfo();
				System.out.print("[" + msg.topicName() + "][" + msg.source()
						+ "] UME registration complete. SQN " + regcomplete.sequenceNumber()
						+ ". Flags " + regcomplete.flags() + " ");
				if ((regcomplete.flags() & LBM.MSG_UME_REGISTRATION_COMPLETE_EX_FLAG_QUORUM) != 0) {
					System.out.print("QUORUM ");
				}
				if ((regcomplete.flags() & LBM.MSG_UME_REGISTRATION_COMPLETE_EX_FLAG_RXREQMAX) != 0) {
					System.out.print("RXREQMAX ");
				}
				System.out.println();
				break;
			case LBM.MSG_UME_REGISTRATION_CHANGE:
				System.out.println("[" + msg.topicName() + "][" + msg.source()
						+ "] UME registration change: " + msg.dataString());
				break;
			default:
				System.err.println("Unknown lbm_msg_t type " + msg.type() + " ["
						+ msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		System.out.flush();
		msg.dispose(); // Send ACK now
		return 0;
	}

	private void print_summary() {
		double total_time_sec, mps, bps;

		total_time_sec = 0.0;
		mps = 0.0;
		bps = 0.0;

		long bits_received = total_byte_count * 8;
		long total_time = data_end_time - data_start_time;

		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);

		total_time_sec = total_time / 1000.0;

		if (total_time_sec > 0) {
			mps = stotal_msg_count / total_time_sec;
			bps = bits_received / total_time_sec;
		}

		System.out.println("\nTotal time         : "
				+ nf.format(total_time_sec) + "  sec");
		System.out.println("Messages received  : " + stotal_msg_count);
		System.out.println("Bytes received     : " + total_byte_count);
		System.out.println("Avg. throughput    : " + nf.format(mps / 1000.0)
				+ " Kmsgs/sec, " + nf.format(bps / 1000000.0) + " Mbps\n\n");

	}

	private void end() {
		System.err.println("Quitting.... received " + total_msg_count
				+ " messages");
		System.exit(0);
	}

}
