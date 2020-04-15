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

class lbmmrcv
{
	private static final int DEFAULT_MAX_NUM_SRCS = 10000;
	private static int nstats = 10;
	private static final int max_num_rcvs = 300000;
	private static final int default_num_rcvs = 100;
	private static final int max_num_ctxs = 5;
	private static final int default_num_ctxs = 1;
	private static final String default_topic_root =  "29west.example.multi";
	private static final int default_initial_topic_number = 0;

	private static int reap_msgs = 0;
	private static int msgs = 200;
	private static boolean verbose = false;
	private static boolean sequential = true;
	private static boolean print_stats_flag = false;
	static boolean exit_on_eos = false;
	static boolean running = true;
	private static String purpose = "Purpose: Receive messages on multiple topics.";
	private static String usage =
	"Usage: lbmrcv [options]\n"
	 + "Available options:\n"
	+ "  -B # = Set receive socket buffer size to # (in MB)\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -C ctxs = use ctxs number of context objects\n"
	+ "  -E = Exit on receiving EOS\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -i num = initial topic number num\n"
	+ "  -o offset = use offset to calculate Registration ID\n"
	+ "              (as source registration ID + offset)\n"
	+ "              offset of 0 forces creation of regid by store\n"
	+ "  -r root = use topic names with root of \"root\"\n"
	+ "  -R rcvs = create rcvs receivers\n"
	+ "  -s = print statistics along with bandwidth\n"
	+ "  -v = be verbose about each message\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-rcv NUM = monitor all receivers every NUM seconds\n"
	+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
	+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
	+ "  --monitor-format FMT = use monitor format module FMT\n"
	+ "                         FMT may be `csv'\n"
	+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
	+ "  --monitor-appid ID = use ID as application ID string\n"
	;

	public static void main(String[] args)
	{
		lbmmrcv rcvapp = new lbmmrcv(args);
	}

	int monitor_context_ivl = 0;
	boolean monitor_context = false;
	int monitor_receiver_ivl = 0;
	long bufsize = 8;
	boolean monitor_receiver = false;
	int mon_transport = LBMMonitor.TRANSPORT_LBM;
	int mon_format = LBMMonitor.FORMAT_CSV;
	String mon_format_options = "";
	String mon_transport_options = "";
	String application_id = null;
	LBM lbm = null;
	int num_ctxs = default_num_ctxs;
	int num_rcvs = default_num_rcvs;
	int initial_topic_number = default_initial_topic_number;
	String topicroot = default_topic_root;
	String regid_offset;
	boolean error = false;

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
                
		Getopt gopt = new Getopt("lbmmrcv", args, "+B:c:C:Eehi:o:r:R:sv", longopts);
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
					case 'B':
						bufsize = Integer.parseInt(gopt.getOptarg());
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
					case 'C':
						num_ctxs = Integer.parseInt(gopt.getOptarg());
					case 'e':
						sequential = false;
						break;
					case 'E':
						exit_on_eos = true;
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						initial_topic_number = Integer.parseInt(gopt.getOptarg());
						break;
					case 'o':
						regid_offset = gopt.getOptarg();
						break;
					case 'r':
						topicroot = gopt.getOptarg();
						break;
					case 'R':
						num_rcvs = Integer.parseInt(gopt.getOptarg());
						if (num_rcvs > max_num_rcvs)
						{
							System.err.println("Too many receivers specified.  Max number of receivers is " + max_num_rcvs);
							System.exit(1);
						}
						break;
					case 's':
						print_stats_flag = true;
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
				System.err.println("lbmmrcv: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	public lbmmrcv(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmmrcv");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		System.out.println("Using " + num_ctxs + " context(s)");
		LBMContextAttributes ctx_attr = null;
		try
		{
			ctx_attr = new LBMContextAttributes();
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating attributes: " + ex.toString());
			System.exit(1);
		}
		if(bufsize > 0) {
			bufsize *= 1024 * 1024;
			ctx_attr.setProperty("transport_tcp_receiver_socket_buffer","" + bufsize);
			ctx_attr.setProperty("transport_lbtrm_receiver_socket_buffer","" + bufsize);
			ctx_attr.setProperty("transport_lbtru_receiver_socket_buffer","" + bufsize);
		}
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
		LBMContext [] ctxs = new LBMContext[num_ctxs];
		LBMContextThread [] ctxthreads = null;
		if (sequential)
		{
			ctxthreads = new LBMContextThread[num_ctxs];
		}
		for (int i = 0; i < num_ctxs; i++)
		{
			try
			{
				ctxs[i] = new LBMContext(ctx_attr);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating context: " + ex.toString());
				System.exit(1);
			}
			if (sequential)
			{
				ctxthreads[i] = new LBMContextThread(ctxs[i]);
				ctxthreads[i].start();
			}
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
			if (monitor_context)
			{
				try
				{
					for (int i = 0; i < num_ctxs; i++)
					{
						lbmmonsrc.start(ctxs[i],
										application_id,
										monitor_context_ivl);
					}
				}
				catch (LBMException ex)
				{
					System.err.println("Error monitoring context: " + ex.toString());
					System.exit(1);
				}
			}
		}
		LBMReceiverAttributes rcv_attr = null;
		try {
			rcv_attr = new LBMReceiverAttributes();
		}
		catch (LBMException ex) {
			System.err.println("Error creating receiver attributes: " + ex.toString());
			System.exit(1);
		}

		if(regid_offset != null)
		{
			MRcvRegistrationId umeregid;
			umeregid = new MRcvRegistrationId(regid_offset);
			rcv_attr.setRegistrationIdCallback(umeregid, null);
			System.out.println("Will use RegID offset " + regid_offset + ".");
		}

		LBMMRcvReceiver [] rcvs = new LBMMRcvReceiver[num_rcvs];
		System.out.println("Creating " + num_rcvs + " receivers");
		
		int ctxidx = 0;
		for (int i = 0; i < num_rcvs; i++)
		{
			int topicnum = initial_topic_number + i;
			String topicname = topicroot + "." + topicnum;

			LBMTopic topic = null;
			try
			{
				topic = ctxs[ctxidx].lookupTopic(topicname, rcv_attr);
				rcvs[i] = new LBMMRcvReceiver(ctxs[ctxidx], topic, verbose);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
			if (i > 1 && (i % 1000) == 0)
			{
				System.out.println("Created " + i + " receivers");
			}

			// This immediate-mode receiver is *only* used for topicless
			// immediate-mode sends.  Immediate sends that use a topic
			// are received with normal receiver objects.
			try
			{
				ctxs[ctxidx].enableImmediateMessageReceiver();
				ctxs[ctxidx].addImmediateMessageReceiver(rcvs[ctxidx]);
			}
			catch (LBMException ex)
			{
				System.err.println("Error enabling immediate message receiver: " + ex.toString());
				System.exit(1);
			}

			if (++ctxidx >= num_ctxs)
				ctxidx = 0;
			if (monitor_receiver)
			{
				try
				{
					lbmmonsrc.start(rcvs[i],
									application_id + "(" + i + ")",
									monitor_context_ivl);
				}
				catch (LBMException ex)
				{
					System.err.println("Error monitoring receiver: " + ex.toString());
					System.exit(1);
				}
			}
		}
		System.out.println("Created " + num_rcvs + " receivers. Will start calculating aggregate throughput.");
		System.out.flush();	
		long start_time;
		long end_time;
		long total_msg_count = 0;
		long last_lost = 0, lost_tmp = 0, lost;
		while (true)
		{
			start_time = System.currentTimeMillis();
			try{
				Thread.sleep(1000);
			}
			catch (InterruptedException e) { }                  
                        
			end_time = System.currentTimeMillis();
			long msg_count = 0;
			long byte_count = 0;
			long unrec_count = 0;
			long burst_loss = 0;
			long rx_msgs = 0;
			long otr_msgs = 0;
			for (int i = 0; i < num_rcvs; i++)
			{
				msg_count += rcvs[i].msg_count;
				total_msg_count += rcvs[i].msg_count;
				byte_count += rcvs[i].byte_count;
				unrec_count += rcvs[i].unrec_count;
				burst_loss += rcvs[i].burst_loss;
				rx_msgs += rcvs[i].rx_msgs;
				otr_msgs += rcvs[i].otr_msgs;
				rcvs[i].msg_count = 0;
				rcvs[i].byte_count = 0;
				rcvs[i].unrec_count = 0;
				rcvs[i].burst_loss = 0;
				rcvs[i].rx_msgs = 0;
				rcvs[i].otr_msgs = 0;
			}
                        
			/* Calculate aggregate transport level loss */
			/* Pass 0 for the print flag indicating interested in retrieving loss stats */
			lost = get_loss_or_print_stats(ctxs, false);
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
					 msg_count,
					 byte_count,
					 unrec_count,
					 lost,
					 burst_loss,
					 rx_msgs,
					 otr_msgs);
                        
			if (print_stats_flag){
				/* Display transport level statistics */
				/* Pass print_stats_flag for the print flag indicating interested in displaying stats */
				get_loss_or_print_stats(ctxs, print_stats_flag);
			}
			/*
			Fool compiler into thinking that we might
			actually break out here, so that we can create
			a reference to lbmmonsrc to throw the sometimes
			flakey Java GC off the scent
			*/
			if (total_msg_count < msg_count)
				break;
			// Look to see if its time to exit
			if (running == false)
				break;
		}
		if (lbmmonsrc != null)
		{
			try
			{
				lbmmonsrc.close();
			}
			catch (LBMException ex)
			{
				System.err.println("Error closing monitoring: " + ex.toString());
				System.exit(1);
			}
		}
                
		System.err.println("Quitting.... received " + total_msg_count + " messages");
                
		try
		{
			for (int i = 0; i < num_rcvs; i++)
				rcvs[i].close();
			for (int i = 0; i < num_ctxs; i++)
			{
				if (sequential)
				{
					ctxthreads[i].terminate();
				}
				ctxs[i].close();
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error closing receivers or contexts: " + ex.toString());
			System.exit(1);
		}
	}
    
	/*
	* function to retrieve transport level loss or display transport level stats
	* @ctxs -- contexts to retrieve transport stats for
	* @print_flag -- if 1, display stats, retrieve loss otherwise
	*/
	private static long get_loss_or_print_stats(LBMContext [] ctxs, boolean print_flag){
		long lost = 0;
		LBMReceiverStatistics stats = null;
		boolean have_stats;
		for (int ctx = 0; ctx < ctxs.length; ctx++){
			have_stats = false;
			while(!have_stats){
				try{				
					stats = ctxs[ctx].getReceiverStatistics(nstats); 
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
				if (print_flag){
					/* Display transport level stats */
					print_stats(stats);
				}
				else{
					/* Accumlate transport level loss */
					for (int stat = 0; stat < stats.size(); stat++){                             
						switch(stats.type(stat)){
							case LBM.TRANSPORT_STAT_LBTRU:
							case LBM.TRANSPORT_STAT_LBTRM:
								lost += stats.lost(stat);
								break;
						}
					}
				}
			}
			catch(LBMException ex){
				System.err.println("Error manipulating receiver statistics: " + ex.toString());
				System.exit(1);
			}
		}    
		return lost;
	}
        
	private static void print_stats(LBMReceiverStatistics stats) throws LBMException
	{
		
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
					if (stats.type(i) == LBM.TRANSPORT_STAT_LBTRU)
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

	private static void print_bw(long msec, long msgs, long bytes, long unrec, long lost, long burst_loss, long rx_msgs, long otr_msgs)
	{
		char scale[] = {'\0', 'K', 'M', 'G'};
		double mps = 0.0, bps = 0.0, sec = 0.0;
		double kscale = 1000.0;
		int msg_scale_index = 0, bit_scale_index = 0;

		sec = msec/1000.;
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

class LBMMRcvReceiver extends LBMReceiver implements LBMImmediateMessageCallback
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

	boolean _verbose = false;
	LBMEventQueue _evq = null;
	//LBMRcvTimer timer;
	public String saved_source = null;

	public LBMMRcvReceiver(LBMContext ctx, LBMTopic topic, LBMEventQueue evq, boolean verbose) throws LBMException
	{
		super(ctx, topic, evq);
		_verbose = verbose;
		_evq = evq;
	}

	public LBMMRcvReceiver(LBMContext ctx, LBMTopic topic, boolean verbose) throws LBMException
	{
		super(ctx, topic);
		_verbose = verbose;
	}
 
	// This immediate-mode receiver is *only* used for topicless
	// immediate-mode sends.  Immediate sends that use a topic
	// are received with normal receiver objects.
	public int onReceiveImmediate(Object cbArg, LBMMessage msg)
	{
		imsg_count++;
		return onReceive(msg);
	}

	protected int onReceive(LBMMessage msg)
	{
		switch (msg.type())
		{
		case LBM.MSG_DATA:
			if (msg_count == 0)
				saved_source = msg.source();
			msg_count++;
			total_msg_count++;
			subtotal_msg_count++;
			byte_count += msg.dataLength();
			if ((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0) {
				rx_msgs++;
			}
			if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0) {
				otr_msgs++;
			}
			if (_verbose)
			{
				System.out.print("["
						   + msg.topicName()
						   + "]["
						   + msg.source()
						   + "]["
						   +  msg.sequenceNumber()
						   + "], ");
                                
				if ((msg.flags() & LBM.MSG_FLAG_RETRANSMIT) != 0) {
					System.out.print("-RX- ");
				}
				if ((msg.flags() & LBM.MSG_FLAG_OTR) != 0) {
					System.out.print("-OTR- ");
				}
				System.out.println(msg.dataLength() + " bytes");
			}
			break;
		case LBM.MSG_BOS:
			System.out.println("[" + msg.topicName() + "][" + msg.source() + "], Beginning of Transport Session");
			break;
		case LBM.MSG_EOS:
			System.out.println("[" + msg.topicName() + "][" + msg.source() + "], End of Transport Session");
			subtotal_msg_count = 0;
			if(lbmmrcv.exit_on_eos)
				lbmmrcv.running = false;
			break;
		case LBM.MSG_NO_SOURCE_NOTIFICATION:
			if (_verbose)
			{
				System.out.println("[" + msg.topicName() + "], no sources found for topic");
			}
			break;
		case LBM.MSG_UNRECOVERABLE_LOSS:
			unrec_count++;
			total_unrec_count++;
			if (_verbose)
			{
				System.out.print("[" + msg.topicName() + "][" + msg.source() + "][" + msg.sequenceNumber() + "],");
				System.out.println(" LOST");
			}
			break;
		case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
			burst_loss++;
			if (_verbose)
			{
				System.out.print("[" + msg.topicName() + "][" + msg.source() + "][" + msg.sequenceNumber() + "],");
				System.out.println(" LOST BURST");
			}
			break;
		case LBM.MSG_REQUEST:
			msg_count++;
			total_msg_count++;
			subtotal_msg_count++;
			byte_count += msg.dataLength();
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
		case LBM.MSG_UME_REGISTRATION_SUCCESS_EX:
		case LBM.MSG_UME_REGISTRATION_COMPLETE_EX:
			/* Provided to enable quiet usage of lbmstrm with UME */
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
		System.exit(0);
	}

}

class MRcvRegistrationId implements UMERegistrationIdExCallback {
	private long _regid_offset;

	public MRcvRegistrationId(String regid_offset) {
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

