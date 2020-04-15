import com.latencybusters.lbm.*;
import java.util.*;
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

class lbmwrcv
{
	private static final int DEFAULT_MAX_NUM_SRCS = 10000;
	private static int nstats = 10;
	private static int reap_msgs = 0;
	private static int msgs = 200;
	private static boolean eventq = false;
	private static boolean verbose = false;
	private static boolean end_on_eos = false;
	private static boolean sequential = true;
	private static boolean dereg = false;
    private static boolean print_stats_flag = false;
	private static String purpose = "Purpose: Receive messages using a wildcard receiver.";
	private static String usage =
	"Usage: lbmwrcv [options] topic\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -E = exit after source ends\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -q = use an LBM event queue\n"
	+ "  -r msgs = delete receiver after msgs messages\n"
	+ "  -s = print statistics along with bandwidth\n"
	+ "  -N NUM = subscribe to channel NUM\n"
	+ "  -v = be verbose about each message\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
	+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
	+ "  --monitor-format FMT = use monitor format module FMT\n"
	+ "                         FMT may be `csv'\n"
	+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
	+ "  --monitor-appid ID = use ID as application ID string\n"
	+ "\n"
	;
	private static LBMContextThread ctxthread = null;

	public static void main(String[] args)
	{
		lbmwrcv rcvapp = new lbmwrcv(args);
	}

	int monitor_context_ivl = 0;
	boolean monitor_context = false;
	int mon_transport = LBMMonitor.TRANSPORT_LBM;
	int mon_format = LBMMonitor.FORMAT_CSV;
	String mon_format_options = "";
	String mon_transport_options = "";
	String application_id = null;
	LBM lbm = null;
	String pattern = null;
	ArrayList<Integer> channels = null;

	private void process_cmdline(String[] args)
	{
		LongOpt[] longopts = new LongOpt[6];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[2] = new LongOpt("monitor-transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[3] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[4] = new LongOpt("monitor-format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[5] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_APPID);
                
		Getopt gopt = new Getopt("lbmwrcv", args, "+c:DEehqr:sN:v", longopts);
		int c = -1;
		boolean error = false;
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
						monitor_context_ivl = Integer.parseInt(gopt.getOptarg());
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
						try 
						{
							LBM.setConfiguration(gopt.getOptarg());
						}
						catch (LBMException ex) 
						{
							System.err.println("Error setting LBM configuration: " + ex.toString());
							System.exit(1);
						}
						break;
					case 'D':
						dereg = true;
						break;
					case 'E':
						end_on_eos = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'h':
						print_help_exit(0);
					case 'q':
						eventq = true;
						break;
					case 'r':
						reap_msgs = Integer.parseInt(gopt.getOptarg());
						break;
					case 's':
						print_stats_flag = true;
						break;
					case 'N':
						channels.add(Integer.parseInt(gopt.getOptarg()));
						break;
					case 'v':
						verbose = true;
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
				System.err.println("lbmwrcv: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error || gopt.getOptind() >= args.length)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		pattern = args[gopt.getOptind()];
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmwrcv(String[] args)
	{
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
		logger = org.apache.log4j.Logger.getLogger("lbmwrcv");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		channels = new ArrayList<Integer>();

		process_cmdline(args);

		LBMContextAttributes lbmcattr = null;
		try
		{
			lbmcattr = new LBMContextAttributes();
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating attributes: " + ex.toString());
			System.exit(1);
		}
		try
		{
			if (sequential)
			{
				lbmcattr.setProperty("operational_mode", "sequential");
			}
			else
			{
				// The default for operational_mode is embedded, but set it
				// explicitly in case a configuration file was specified with
				// a different value.
				lbmcattr.setProperty("operational_mode", "embedded");
			}
		}
		catch (LBMRuntimeException ex)
		{
			System.err.println("Error setting operational_mode: " + ex.toString());
			System.exit(1);
		}
		LBMWRcvSourceNotify srcNotify = new LBMWRcvSourceNotify();
		lbmcattr.enableSourceNotification();
		LBMContext ctx = null;
		try
		{
			ctx = new LBMContext(lbmcattr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}
		try
		{
			ctx.addSourceNotifyCallback(srcNotify);
		}
		catch (LBMException ex)
		{
			System.err.println("Error adding source notification callback: " + ex.toString());
			System.exit(1);
		}
		LBMWildcardReceiverAttributes wrcv_attr = null;
		try
		{
			wrcv_attr = new LBMWildcardReceiverAttributes();
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating wildcard attributes: " + ex.toString());
			System.exit(1);
		}
		String pattern_type = null;
		try
		{
			pattern_type = wrcv_attr.getValue("pattern_type");
		}
		catch (LBMException ex)
		{
			System.err.println("Error getting wildcard pattern type: " + ex.toString());
			System.exit(1);
		}
		LBMWRcvTopicFilter topicFilter = null;
		if (pattern.equals("*")
			&& (pattern_type.equalsIgnoreCase("PCRE")
				|| pattern_type.equalsIgnoreCase("regex")))
		{
			topicFilter = new LBMWRcvTopicFilter();
			pattern_type = "appcb";
			try
			{
				wrcv_attr.setValue("pattern_type", pattern_type);
			}
			catch (LBMException ex)
			{
				System.err.println("Error setting pattern type: " + ex.toString());
				System.exit(1);
			}
			wrcv_attr.setPatternCallback(topicFilter, null);
		}
		System.err.println("Creating wildcard receiver for pattern ["
				   + pattern
				   + "] - using pattern type: "
				   + pattern_type);
		LBMWRcvReceiver wrcv = new LBMWRcvReceiver(verbose, end_on_eos);
		LBMWildcardReceiver lbmwrcv = null;
		LBMWRcvEventQueue evq = null;
		try
		{
			if (eventq)
			{
				if (sequential)
				{
					System.err.println("Sequential mode with event queue in use");
				}
				else
				{
					System.err.println("Embedded mode with event queue in use");
				}
				try
				{
					evq = new LBMWRcvEventQueue();
				}
				catch (LBMException ex)
				{
					System.err.println("Error creating event queue: " + ex.toString());
					System.exit(1);
				}
				lbmwrcv = new LBMWildcardReceiver(ctx,
												  pattern,
												  null,
												  wrcv_attr,
												  wrcv,
												  null,
												  evq);
				ctx.enableImmediateMessageReceiver(evq);
			}
			else if (sequential)
			{
				System.err.println("No event queue, sequential mode");
				lbmwrcv = new LBMWildcardReceiver(ctx,
												  pattern,
												  null,
												  wrcv_attr,
												  wrcv,
												  null,
												  null);
				ctx.enableImmediateMessageReceiver();
			}
			else
			{
				System.err.println("No event queue, embedded mode");
				lbmwrcv = new LBMWildcardReceiver(ctx,
												  pattern,
												  null,
												  wrcv_attr,
												  wrcv,
												  null);
				ctx.enableImmediateMessageReceiver();
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating wildcard receiver: " + ex.toString());
			System.exit(1);
		}

		wrcv.setLBMWildcardReceiver(lbmwrcv);
		wrcv.setDeregFlag(dereg);

		

		if(channels.size() > 0)
		{
			for(int i=0;i<channels.size();i++) {
				  try {
					  lbmwrcv.subscribeChannel(channels.get(i), wrcv, null);
				  } catch (LBMException ex) {
					  System.err.println("Error subscribing to channel: " + ex.toString());
				  }
			}
		}
		// This immediate-mode receiver is *only* used for topicless
		// immediate-mode sends.  Immediate sends that use a topic
		// are received with normal receiver objects.
		ctx.addImmediateMessageReceiver(wrcv, null);

		LBMMonitorSource lbmmonsrc = null;
		if (monitor_context)
		{
			try
			{
				lbmmonsrc = new LBMMonitorSource(mon_format, mon_format_options, mon_transport, mon_transport_options);
				lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
			}
			catch (LBMException ex)
			{
				System.err.println("Error starting monitoring: " + ex.toString());
				System.exit(1);
			}
		}

		long start_time;
		long end_time;
		long last_lost = 0, lost_tmp = 0, lost = 0;
		boolean have_stats;
		LBMReceiverStatistics stats = null;
		if (sequential) {
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		while (true){
			start_time = System.currentTimeMillis();
			if (eventq){
				evq.run(1000);
			}
			else{
				try{
					Thread.sleep(1000);
				}
				catch (InterruptedException e) { }
			}
			end_time = System.currentTimeMillis();
                        
			stats = null;
			have_stats = false;
			while(!have_stats){
				try{				
					stats = ctx.getReceiverStatistics(nstats);
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

				print_bw(end_time - start_time,
					 wrcv.msg_count,
					 wrcv.byte_count,
					 wrcv.unrec_count,
					 lost,
					 wrcv.rx_msgs,
					 wrcv.otr_msgs,
					 wrcv.burst_loss);
					 
				if (print_stats_flag){
					print_stats(stats, evq);
				}				
					
				wrcv.msg_count = 0;
				wrcv.byte_count = 0;
				wrcv.unrec_count = 0;
				wrcv.burst_loss = 0;
				wrcv.rx_msgs = 0;
				wrcv.otr_msgs = 0;
			}
			catch (LBMException ex){
				System.err.println("Error manipulating receiver statistics: " + ex.toString());
				System.exit(1);
			}			
                        
			if (reap_msgs != 0 && wrcv.total_msg_count >= reap_msgs){
			    break;
			}
		}
		if (ctxthread != null)
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
		System.err.println("Quitting.... received "
				   + wrcv.total_msg_count
				   + " messages");
		boolean closed = false;
		do {
			try
			{
				lbmwrcv.close();
				closed = true;
			}
			catch (LBMException ex)
			{
				if(ex.errorNumber() == LBM.EOP) continue;

				System.err.println("Error closing receiver: " + ex.toString());
				System.exit(1);
			}
		} while(closed == false);
		ctx.close();
	}
        
    private static void print_stats(LBMReceiverStatistics stats, LBMEventQueue evq) throws LBMException
	{
		if (evq != null)
		{
			if (Integer.parseInt(evq.getAttributeValue("queue_size_warning")) > 0)
			{
				System.out.println("Event queue size: " + evq.queueSize());
			}
		}
		for (int i = 0; i < stats.size(); i++)
		{
			switch(stats.type(i))
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.out.println("TCP, source " + stats.source(i) 
									  + ", received " + stats.lbmMessagesReceived(i)
									  + "/" + stats.bytesReceived(i)
									  + ", no topics " + stats.noTopicMessagesReceived(i)
									  + ", requests " + stats.lbmRequestsReceived(i));
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
				case LBM.TRANSPORT_STAT_LBTRM:
					if (stats.type() == LBM.TRANSPORT_STAT_LBTRU)
						System.out.print("LBT-RU");
					else
						System.out.print("LBT-RM");
					System.out.println(", source " + stats.source(i)
									+ ", received " + stats.messagesReceived(i)
									+ "/" + stats.bytesReceived(i)
									+ ", naks " + stats.nakPacketsSent(i)
									+ "/" + stats.naksSent(i)
									+ ", lost " + stats.lost(i)
									+ ", ncfs " + stats.ncfsIgnored(i)
									+ "/" + stats.ncfsShed(i)
									+ "/" + stats.ncfsRetransmissionDelay(i)
									+ "/" + stats.ncfsUnknown(i)
									+ ", recovery " + stats.minimumRecoveryTime(i)
									+ "/" + stats.meanRecoveryTime(i)
									+ "/" + stats.maximumRecoveryTime(i)
									+ ", nak tx " + stats.minimumNakTransmissions(i)
									+ "/" + stats.minimumNakTransmissions(i)
									+ "/" + stats.maximumNakTransmissions(i)
									+ ", dup " + stats.duplicateMessages(i)
									+ ", unrecovered " + stats.unrecoveredMessagesWindowAdvance(i)
									+ "/" + stats.unrecoveredMessagesNakGenerationTimeout(i)
									+ ", LBM msgs " + stats.lbmMessagesReceived(i)
									+ ", no topics " + stats.noTopicMessagesReceived(i)
									+ ", requests " + stats.lbmRequestsReceived(i));
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.out.println("LBT-IPC, source " + stats.source(i)
									+ ", received " + stats.messagesReceived(i)
									+ " msgs/" + stats.bytesReceived(i)
									+ " bytes. LBM msgs " + stats.lbmMessagesReceived(i)
									+ ", no topics " + stats.noTopicMessagesReceived(i)
									+ ", requests " + stats.lbmRequestsReceived(i));
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.out.println("LBT-RDMA, source " + stats.source(i)
									+ ", received " + stats.messagesReceived(i)
									+ " msgs/" + stats.bytesReceived(i)
									+ " bytes. LBM msgs " + stats.lbmMessagesReceived(i)
									+ ", no topics " + stats.noTopicMessagesReceived(i)
									+ ", requests " + stats.lbmRequestsReceived(i));
					break;
			}
		}
		System.out.flush();
	}

	private static void print_bw(long msec, long msgs, long bytes, long unrec, long lost, long rx_msgs, long otr_msgs, long burst_loss)
	{
		char scale[] = {'\0', 'K', 'M', 'G'};
		double mps = 0.0, bps = 0.0, sec = 0.0;
		double kscale = 1000.0;
		int msg_scale_index = 0, bit_scale_index = 0;

		sec = msec/1000.0;
		if (sec == 0) return; /* avoid division by zero */
		mps = ((double)msgs)/sec;
		bps = ((double)bytes*8)/sec;
		
		while (mps >= kscale) {
			mps /= kscale;
			msg_scale_index++;
		}
	
		while (bps >= kscale) {
			bps /= kscale;
			bit_scale_index++;
		}
		
		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
                
		if ((rx_msgs > 0) || (otr_msgs > 0)) {
			System.err.print(sec
							+ " secs. "
							+ nf.format(mps)
							+ " " + scale[msg_scale_index]
							+ "msgs/sec. "
							+ nf.format(bps)
							+ " " + scale[bit_scale_index]
							+ "bps"
							+ " [RX: " + rx_msgs + "][OTR: " + otr_msgs + "]");
		}
		else{
			System.err.print(sec
							+ " secs. "
							+ nf.format(mps)
							+ " " + scale[msg_scale_index]
							+ "msgs/sec. "
							+ nf.format(bps)
							+ " " + scale[bit_scale_index]
							+ "bps");
		}
                
		if (lost != 0 || unrec != 0 || burst_loss != 0)
		{
			System.err.print(" ["
					   + lost
					   + " pkts lost, "
					   + unrec
					   + " msgs unrecovered, "
					   + burst_loss
					   + " bursts]");
		}
		System.err.println();
	}

}

class LBMWRcvSourceNotify implements LBMSourceNotification
{
	public int sourceNotification(String topic, String source, Object cbArg)
	{
		System.err.println("new topic ["
				   + topic 
				   + "], source ["
				   + source
				   + "]");
		return 0;
	}
}

class LBMWRcvTopicFilter implements LBMWildcardPatternCallback
{
	public int comparePattern(String topic, Object cbArg)
	{
		/* match everything */
		System.err.println("[" + topic + "] topic accepted.");
		return 0;
	}
}

class LBMWRcvEventQueue extends LBMEventQueue implements LBMEventQueueCallback
{
	public LBMWRcvEventQueue() throws LBMException
	{
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

class LBMWRcvReceiver implements LBMReceiverCallback, LBMImmediateMessageCallback
{
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
	public LBMWildcardReceiver _wrcv;

	boolean _verbose = false;
	boolean _end_on_eos = false;
	boolean _dereg = false;

	public LBMWRcvReceiver(boolean verbose, boolean end_on_eos) {
		_verbose = verbose;
		_end_on_eos = end_on_eos;
	}
	public void setLBMWildcardReceiver(  LBMWildcardReceiver lbmrcv)
    {
        _wrcv = lbmrcv;
    }

	public void setDeregFlag (boolean dereg)
	{
		_dereg = dereg;
	}


	// This immediate-mode receiver is *only* used for topicless
	// immediate-mode sends.  Immediate sends that use a topic
	// are received with normal receiver objects.
	public int onReceiveImmediate(Object cbArg, LBMMessage msg)
	{
		imsg_count++;
		return onReceive(cbArg, msg);
	}

	public int onReceive(Object cbArg, LBMMessage msg)
	{
		switch (msg.type())
		{
			case LBM.MSG_DATA:
				msg_count++;
				total_msg_count++;
				subtotal_msg_count++;
				byte_count += msg.data().length;
                                
				if ((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0){
					rx_msgs++;
				}
				if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0){
					otr_msgs++;
				}
				if ((total_msg_count == 1000) && (_dereg == true))
				{
					try {
						System.out.println("About to send Wildcard receiver deregister\n");
						_wrcv.umederegister();
						_dereg = false;
					}
					catch (LBMException e) {
						System.err.println(e.getMessage());
					}
				}
                                
				if (_verbose)
				{					
					long sqn = msg.sequenceNumber();
					if ((msg.flags() & (LBM.MSG_FLAG_HF_32 | LBM.MSG_FLAG_HF_64)) != 0) {
						sqn = msg.hfSequenceNumber();
					}
					System.out.format("@%d.%06d[%s%s][%s][%s]%s%s%s%s%s%s%s, %s bytes\n",
							msg.timestampSeconds(), msg.timestampMicroseconds(), msg.topicName(),
							((msg.channelInfo() != null) ? ":" + msg.channelInfo().channelNumber() : ""), 
							msg.source(), sqn >= 0 ? sqn : msg.hfSequenceNumberBigInt(),
							((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0 ? "-RX" : ""),
							((msg.flags() & LBM.MSG_FLAG_OTR) != 0 ? "-OTR" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_64) != 0 ? "-HF64" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_32) != 0 ? "-HF32" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_DUPLICATE) != 0 ? "-HFDUP" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_PASS_THROUGH) != 0 ? "-PASS" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_OPTIONAL) != 0 ? "-HFOPT" : ""),
							msg.dataLength());
				}
				break;
			case LBM.MSG_BOS:
				System.out.println("[" + msg.topicName() + "][" + msg.source() + "], Beginning of Transport Session");
				break;
			case LBM.MSG_EOS:
				System.out.println("[" + msg.topicName() + "][" + msg.source() + "], End of Transport Session");
				if (_end_on_eos)
				{
					end();
				}
				subtotal_msg_count = 0;
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS:
				unrec_count++;
				total_unrec_count++;
				if (_verbose)
				{
					long sqn = msg.sequenceNumber();
					if ((msg.flags() & (LBM.MSG_FLAG_HF_32 | LBM.MSG_FLAG_HF_64)) != 0) {
						sqn = msg.hfSequenceNumber();
					}
					System.out.format("[%s][%s][%s]%s%s-RESET\n", msg.topicName(), msg.source(), sqn >= 0 ? sqn : msg.hfSequenceNumberBigInt(),
							((msg.flags() & LBM.MSG_FLAG_HF_64) != 0 ? "-HF64" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_32) != 0 ? "-HF32" : ""));
				}
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
				burst_loss++;
				if (_verbose)
				{
					System.out.print("[" + msg.topicName() + "][" + msg.source() + "],");
					System.out.println(" LOST BURST");
				}
				break;
			case LBM.MSG_REQUEST:
				msg_count++;
				total_msg_count++;
				subtotal_msg_count++;
				byte_count += msg.data().length;
				if (_verbose)
				{
					System.out.print("Request ["
							   + msg.topicName()
							   + "]["
							   + msg.source()
							   + "], "
							   + msg.sequenceNumber()
							   + " bytes");
					System.out.println(msg.data().length + " bytes");
				}
				break;
			case LBM.MSG_NO_SOURCE_NOTIFICATION:
				System.out.println("["
						   + msg.topicName()
						   + "], no sources found for topic");
				break;
			case LBM.MSG_HF_RESET:
				if (_verbose) {
					long sqn = msg.sequenceNumber();
					if ((msg.flags() & (LBM.MSG_FLAG_HF_32 | LBM.MSG_FLAG_HF_64)) != 0) {
						sqn = msg.hfSequenceNumber();
					}
					System.out.format("[%s][%s][%s]%s%s%s%s-RESET\n", msg.topicName(), msg.source(), sqn >= 0 ? sqn : msg.hfSequenceNumberBigInt(),
							((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0 ? "-RX" : ""),
							((msg.flags() & LBM.MSG_FLAG_OTR) != 0 ? "-OTR" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_64) != 0 ? "-HF64" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_32) != 0 ? "-HF32" : ""));
				}
				break;
			case LBM.MSG_UME_DEREGISTRATION_SUCCESS_EX:
				System.out.print("DEREGISTRATION SUCCESSFUL ");
                System.out.println();
				break;
			case LBM.MSG_UME_DEREGISTRATION_COMPLETE_EX:
                System.out.print("DEREGISTRATION COMPLETE ");
                System.out.println();
                break;
			default:
				System.out.println("Unknown lbm_msg_t type " + msg.type() + " [" + msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		System.out.flush();
		msg.dispose();
		return 0;
	}

	private void end()
	{
		System.err.println("Quitting.... received "
				   + total_msg_count
				   + " messages");
		System.exit(0);
	}
}
