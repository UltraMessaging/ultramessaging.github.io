import com.latencybusters.lbm.*;
import com.latencybusters.lbm.sdm.*;
import java.util.*;
import java.text.NumberFormat;
import verifiablemsg.*;

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

class lbmrcv
{
	private static final int DEFAULT_MAX_NUM_SRCS = 10000;
	private static int nstats = 10;
	private static int reap_msgs = 0;
	private static int msgs = 200;
	private static boolean eventq = false;
	private static boolean sequential = true;
	public static boolean verbose = false;
	private static boolean end_on_eos = false;
	private static int stat_secs = 0;
	private static boolean summary = false;
	private static String purpose = "Purpose: Receive messages on a single topic.";
	public static boolean sdm = false;
	public static boolean verifiable = false;
	private static String usage =
	"Usage: lbmrcv [options] topic\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -D = Assume received messages are SDM formatted\n"
	+ "  -E = exit after source ends\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -f = use hot-failover\n"
	+ "  -h = help\n"
	+ "  -r msgs = delete receiver after msgs messages\n"
	+ "  -N NUM = subscribe to channel NUM\n"
	+ "  -S = exit after source ends, print throughput summary\n"
	+ "  -s num_secs = print statistics every num_secs along with bandwidth\n"
	+ "  -q = use an LBM event queue\n"
	+ "  -v = be verbose about each message\n"
	+ "  -V = verify message contents\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-rcv NUM = monitor receiver every NUM seconds\n"
	+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
	+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
	+ "  --monitor-format FMT = use monitor format module FMT\n"
	+ "                         FMT may be `csv'\n"
	+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
	+ "  --monitor-appid ID = use ID as application ID string\n"
	;
	private static LBMContextThread ctxthread = null;

	public static void main(String[] args)
	{
		lbmrcv rcvapp = new lbmrcv(args);
	}

	LBM lbm = null;
	String mon_format_options = "";
	String mon_transport_options = "";
	int monitor_context_ivl = 0;
	boolean monitor_context = false;
	int monitor_receiver_ivl = 0;
	boolean monitor_receiver = false;
	int mon_transport = LBMMonitor.TRANSPORT_LBM;
	int mon_format = LBMMonitor.FORMAT_CSV;
	String application_id = null;
	boolean error = false;
	boolean use_hf = false;
	String topicname;
	ArrayList<Integer> channels = null;	
	LBMObjectRecycler objRec = new LBMObjectRecycler();
	

	private void process_cmdline(String[] args)
	{
		LongOpt[] longopts = new LongOpt[7];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_RCV = 3;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-rcv", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_RCV);
		longopts[2] = new LongOpt("monitor-transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[3] = new LongOpt("monitor-transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[4] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[5] = new LongOpt("monitor-format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[6] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_APPID);
                
		Getopt gopt = new Getopt("lbmrcv", args, "c:DEefhr:Ss:qvVN:", longopts);
		int c = -1;
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
						if(verifiable)
						{
							System.err.println("Unable to use SDM because verifiable messages are on. Turn off verifiable messages (-V).");
							System.exit(1);
						}
						sdm = true;
						break;
					case 'E':
						end_on_eos = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'f':
						use_hf = true;
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
					case 'q':
						eventq = true;
						break;
					case 'r':
						reap_msgs = Integer.parseInt(gopt.getOptarg());
						break;
					case 'N':
						channels.add(Integer.parseInt(gopt.getOptarg()));
						break;
					case 'v':
						verbose = true;
						break;
					case 'V':
						if(sdm)
						{
							System.err.println("Unable to use verifiable messages because sdm is on. Turn off sdm (-D).");
							System.exit(1);
						}

						verifiable = true;
						break;
						
					default:
						error = true;
				}
				if (error)
					break;
			}
			catch (Exception e)
			{
				/* type conversion exception */
				System.err.println("lbmrcv: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error || gopt.getOptind() >= args.length)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		topicname = args[gopt.getOptind()];
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmrcv(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmrcv");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);
		
		channels = new ArrayList<Integer>();

		process_cmdline(args);

		LBMContextAttributes ctx_attr = null;
		try
		{
			ctx_attr = new LBMContextAttributes();
			ctx_attr.setObjectRecycler(objRec, null);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context attributes: " + ex.toString());
			System.exit(1);
		}
		try
		{
			if (sequential)
			{
				ctx_attr.setProperty("operational_mode", "sequential");
			}
			else
			{
				// The default for operational_mode is embedded, but set it
				// explicitly in case a configuration file was specified with
				// a different value.
				ctx_attr.setProperty("operational_mode", "embedded");
			}
		}
		catch (LBMRuntimeException ex)
		{
			System.err.println("Error setting operational mode: " + ex.toString());
			System.exit(1);
		}
		LBMRcvEventQueue evq = null;
		LBMRcvReceiver rcv = new LBMRcvReceiver(verbose, end_on_eos, summary);
		if (eventq) {
			try
			{
				evq = new LBMRcvEventQueue();
				ctx_attr.setImmediateMessageCallback(rcv, evq);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating event queue: " + ex.toString());
				System.exit(1);
			}
		}
		else {
			ctx_attr.setImmediateMessageCallback(rcv);
		}
		
		LBMContext ctx = null;
		try
		{
			ctx = new LBMContext(ctx_attr);
			if(ctx.getAttributeValue("request_tcp_bind_request_port").equals("0") == false)
			{
				String request_tcp_iface = ctx.getAttributeValue("request_tcp_interface");
				if(request_tcp_iface.equals("0.0.0.0") == false)
				{
					System.out.println("Immediate messaging target: TCP:"
						+ request_tcp_iface + ":"
						+ ctx.getAttributeValue("request_tcp_port"));
				}
				else
				{
					System.out.println("Immediate messaging target: TCP:"
						+ ctx.getAttributeValue("resolver_multicast_interface") + ":"
						+ ctx.getAttributeValue("request_tcp_port"));
				}
			}
			else
			{
				System.out.println("Request port binding disabled, no immediate messaging target");
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}
		
		LBMTopic topic = null;
		
		try
		{
			LBMReceiverAttributes rcv_attr = new LBMReceiverAttributes();
			rcv_attr.setObjectRecycler(objRec, null);
			topic = new LBMTopic(ctx, topicname, rcv_attr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error looking up topic: " + ex.toString());
			System.exit(1);
		}
		LBMReceiver lbmrcv = null;
		if (use_hf)
			System.err.print("Hot-Failover, ");
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
				if (use_hf)
					lbmrcv = new LBMHotFailoverReceiver(ctx, topic, rcv, null, evq);
				else
					lbmrcv = new LBMReceiver(ctx, topic, rcv, null, evq);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
		}
		else if (sequential)
		{
			System.err.println("No event queue, sequential mode");
			try
			{
				if (use_hf)
					lbmrcv = new LBMHotFailoverReceiver(ctx, topic, rcv, null);
				else
					lbmrcv = new LBMReceiver(ctx, topic, rcv, null);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
		}
		else
		{
			System.err.println("No event queue, embedded mode");
			try
			{
				if (use_hf)
					lbmrcv = new LBMHotFailoverReceiver(ctx, topic, rcv, null);
				else
					lbmrcv = new LBMReceiver(ctx, topic, rcv, null);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
		}
		
		if(channels.size() > 0)
		{
			for(int i=0;i<channels.size();i++) {
				  try {
					  lbmrcv.subscribeChannel(channels.get(i), rcv, null);
				  } catch (LBMException ex) {
					  System.err.println("Error subscribing to channel: " + ex.toString());
				  }
			}
		}

		if (sequential) {
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}

		LBMMonitorSource lbmmonsrc = null;
		if (monitor_context || monitor_receiver)
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
					lbmmonsrc.start(lbmrcv, application_id, monitor_receiver_ivl);
			}
			catch (LBMException ex)
			{
				System.err.println("Error enabling monitoring: " + ex.toString());
				System.exit(1);
			}
		}
		System.out.flush();	
		long start_time;
		long end_time;
		long last_lost = 0, lost_tmp = 0, lost = 0;
		boolean have_stats = false;
		long stat_time = System.currentTimeMillis() + (1000 * stat_secs);
		LBMReceiverStatistics stats = null;

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
			
			have_stats = false;
			while(!have_stats){
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

				print_bw(end_time - start_time,
						 rcv.msg_count,
						 rcv.byte_count,
						 rcv.unrec_count,
						 lost,
						 rcv.rx_msgs,
						 rcv.otr_msgs,
						 rcv.burst_loss);
				if (stat_secs > 0 && stat_time <= end_time){
					stat_time = System.currentTimeMillis() + (1000 * stat_secs);
					print_stats(stats, evq);
				}
			}
			catch(LBMException ex){
				System.err.println("Error manipulating receiver statistics: " + ex.toString());
				System.exit(1);
			}
				
			rcv.msg_count = 0;
			rcv.byte_count = 0;
			rcv.unrec_count = 0;
			rcv.burst_loss = 0;
			rcv.rx_msgs = 0;
			rcv.otr_msgs = 0;
			
			if (reap_msgs != 0 && rcv.total_msg_count >= reap_msgs){
			    break;
			}
			// recycle stats object when finished so it can be reused by LBM
			objRec.doneWithReceiverStatistics(stats);
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
				   + rcv.total_msg_count
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

		sec = msec/1000;
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
                
		if ((rx_msgs > 0) || (otr_msgs > 0)){
			System.out.print(sec
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
			System.out.print(sec
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
			System.out.print(" ["
					   + lost
					   + " pkts lost, "
					   + unrec
					   + " msgs unrecovered, "
					   + burst_loss
					   + " bursts]");
		}
		System.out.println();
		System.out.flush();	
	}

}

class LBMRcvEventQueue extends LBMEventQueue implements LBMEventQueueCallback
{
	public LBMRcvEventQueue() throws LBMException
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

class LBMRcvReceiver implements LBMReceiverCallback, LBMImmediateMessageCallback
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

	public long data_start_time = 0;
	public long data_end_time = 0;

	public int stotal_msg_count = 0;
	public long total_byte_count = 0;

	LBMSDMessage SDMsg;

	boolean _verbose = false;
	boolean _end_on_eos = false;
	boolean _summary = false;
		
	public LBMRcvReceiver(boolean verbose, boolean end_on_eos, boolean summary) {
		_verbose = verbose;
		_end_on_eos = end_on_eos;
		_summary = summary;
		SDMsg = new LBMSDMessage();
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
				if (stotal_msg_count == 0)
					data_start_time = System.currentTimeMillis();
				else
					data_end_time = System.currentTimeMillis();
				msg_count++;
				total_msg_count++;
				stotal_msg_count++;
				subtotal_msg_count++;
				/* When using Zero Object Delivery, be sure to use the
				 * LBMMessage.dataLength() method to obtain message length,
				 * rather than using LBMMessage.data().length.  Calling
				 * LBMMessage.data() will cause the creation of a new
				 * byte[] array object, which is unnecessary if all you need
				 * is the message length. */
				byte_count += msg.dataLength();
				total_byte_count += msg.dataLength();
                                
				if ((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0) {
					rx_msgs++;
				}
				if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0) {
					otr_msgs++;
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
							((msg.flags() & LBM.MSG_FLAG_HF_64) != 0 ? "-HF64" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_32) != 0 ? "-HF32" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_DUPLICATE) != 0 ? "-HFDUP" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_PASS_THROUGH) != 0 ? "-PASS" : ""),
							((msg.flags() & LBM.MSG_FLAG_HF_OPTIONAL) != 0 ? "-HFOPT" : ""),
							((msg.flags() & LBM.MSG_FLAG_OTR) != 0 ? "-OTR" : ""),
							msg.dataLength());

					if(lbmrcv.verifiable)
					{
						int rc = VerifiableMessage.verifyMessage(msg.dataBuffer(), msg.dataLength(), lbmrcv.verbose);
						if(rc == 0)
						{
							System.out.println("Message sqn " + msg.sequenceNumber() + " does not verify!");
						}
						else if (rc == -1)
						{
							System.err.println("Message sqn " + msg.sequenceNumber() + "is not a verifiable message.");
							System.err.println("Use -V option on source and restart receiver.");
						}
						else
						{
							if(lbmrcv.verbose)
							{
								System.out.println("Message sqn " + msg.sequenceNumber() + " verifies");
							}
						}
					}
					else if(lbmrcv.sdm)
					{
						try
						{
							SDMsg.parse(msg.data());

							LBMSDMField f = SDMsg.locate("Sequence Number");

							long recvdseq = ((LBMSDMFieldInt64)f).get();
							System.out.println("SDM Message contains " + SDMsg.count() + " fields and Field Sequence Number == " + recvdseq);
						}
						catch (LBMSDMException sdme)
						{
							System.out.println("Error occurred processing received SDM Message: " + sdme);
						}
					}
				}

				break;
			case LBM.MSG_BOS:
				System.out.println("[" + msg.topicName() + "][" + msg.source() + "], Beginning of Transport Session");
				break;
			case LBM.MSG_EOS:
				System.out.println("[" + msg.topicName() + "][" + msg.source() + "], End of Transport Session");
				if (_end_on_eos)
				{
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
				if (stotal_msg_count == 0)
					data_start_time = System.currentTimeMillis();
				else
					data_end_time = System.currentTimeMillis();
				msg_count++;
				stotal_msg_count++;
				subtotal_msg_count++;
				byte_count += msg.dataLength();
				total_byte_count += msg.dataLength();
				if (_verbose)
				{
					System.out.print("Request ["
							   + msg.topicName()
							   + "]["
							   + msg.source()
							   + "], "
							   + msg.sequenceNumber()
							   + " bytes");
					System.out.println(msg.dataLength() + " bytes");
				}
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
			default:
				System.out.println("Unknown lbm_msg_t type " + msg.type() + " [" + msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		System.out.flush();	
		msg.dispose();		
		return 0;
	}

	private void print_summary()
	{
		double total_time_sec, mps, bps; 

		total_time_sec = 0.0;
		mps = 0.0;
		bps = 0.0;

		long bits_received = total_byte_count * 8;
		long total_time = data_end_time - data_start_time;

		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);

		total_time_sec = total_time / 1000.0 ;

		if (total_time_sec > 0) {
			mps = stotal_msg_count / total_time_sec ;
			bps = bits_received / total_time_sec ;
		}

		System.out.println("\nTotal time         : "
				   + nf.format (total_time_sec)
				   + "  sec");
		System.out.println("Messages received  : "
				   + stotal_msg_count);
		System.out.println("Bytes received     : "
				   + total_byte_count);
		System.out.println("Avg. throughput    : "
				   + nf.format (mps / 1000.0)
				   + " Kmsgs/sec, "
				   + nf.format (bps / 1000000.0)
				   + " Mbps\n\n");

	}

	private void end()
	{
		System.err.println("Quitting.... received "
				   + total_msg_count
				   + " messages");
		System.exit(0);
	}

}
