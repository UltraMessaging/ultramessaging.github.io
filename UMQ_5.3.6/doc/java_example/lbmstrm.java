import com.latencybusters.lbm.*;
import java.util.Date;
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

class lbmstrm
{
	private static final int default_num_sources = 100;
	private static final int default_num_threads = 1;
	private static final int max_num_sources = 100000;
	private static final int max_num_threads = 16;
	private static final int max_msg_sz = 3000000;
	private static final int default_max_messages = 10000000;
	private static final String default_topic_root =  "29west.example.multi";
	private static final int default_initial_topic_number = 0;

	private static String pcid = "";
	private static int msgs = 10000000;
	private static boolean verbose = false;
	private static boolean sequential = true;
	private static String purpose = "Purpose: Send messages on multiple topics.";
	private static String usage =
	"Usage: lbmstrm [options]\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -i num = initial topic number\n"
	+ "  -l len = send messages of len bytes\n"
	+ "  -L linger = linger for linger seconds before closing context\n"
	+ "  -m NUM = send at NUM messages per second"
	+ "  -M msgs = send maximum of msgs number of messages\n"
	+ "  -r root = use topic names with root of \"root\"\n"
	+ "  -s = print source statistics before exiting\n"
	+ "  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
	+ "                     DATA bits per second, and set retransmit rate limit to\n"
	+ "                     RETR bits per second.  For both limits, the optional\n"
	+ "                     k, m, and g suffixes may be used.  For example,\n"
	+ "                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
	+ "  -S srcs = use srcs sources\n"
	+ "  -t = tight loop (cpu-bound) for even message spacing\n"
	+ "  -T thrds = use thrds threads\n"
	+ "  -v = be verbose\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-src NUM = monitor each source every NUM seconds\n"
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
		lbmstrm srcapp = new lbmstrm(args);
	}

	int send_rate = 0;							//	Used for lbmtrm | lbtru transports
	int retrans_rate = 0;						//
	char protocol = '\0';						//
	int linger = 5;
	int monitor_context_ivl = 0;
	boolean monitor_context = false;
	int monitor_source_ivl = 0;
	boolean monitor_source = false;
	int mon_transport = LBMMonitor.TRANSPORT_LBM;
	int mon_format = LBMMonitor.FORMAT_CSV;
	String mon_format_options = "";
	String mon_transport_options = "";
	String application_id = null;
	LBM lbm = null;
	int msglen = 25;
	long bytes_sent = 0;
	private int msgs_per_sec = 10000;
	boolean do_stats = false;
	int initial_topic_number = default_initial_topic_number;
	String topicroot = default_topic_root;
	int num_srcs = default_num_sources;
	int num_thrds = default_num_threads;
	boolean error = false;
	boolean tightloop = false;

	private void process_cmdline(String[] args)
	{
		LongOpt[] longopts = new LongOpt[8];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_SRC = 3;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;
		final int OPTION_MESSAGE_RATE = 9;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-src", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_SRC);
		longopts[2] = new LongOpt("monitor-transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[3] = new LongOpt("monitor-transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[4] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[5] = new LongOpt("monitor-format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[6] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_APPID);
		longopts[7] = new LongOpt("message-rate", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MESSAGE_RATE);
		Getopt gopt = new Getopt("lbmstrm", args, "+c:ehi:l:M:m:r:R:sS:tT:vL:", longopts);
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
					case OPTION_MONITOR_SRC:
						monitor_source = true;
						monitor_source_ivl = Integer.parseInt(gopt.getOptarg());
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
					case 'e':
						sequential = false;
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						initial_topic_number = Integer.parseInt(gopt.getOptarg());
						break;
					case 'l':
						msglen = Integer.parseInt(gopt.getOptarg());
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
					case 'r':
						topicroot = gopt.getOptarg();
						break;
					case 'R':
						ParseRateVars parseRateVars = lbmExampleUtil.parseRate(gopt.getOptarg());
						if (parseRateVars.error) {
							print_help_exit(1);
						}
						protocol = parseRateVars.protocol;
						send_rate = parseRateVars.rate;
						retrans_rate = parseRateVars.retrans;
						break;
					case 'S':
						num_srcs = Integer.parseInt(gopt.getOptarg());
						if (num_srcs > max_num_sources)
						{
							System.err.println( "Too many sources specified. Max number of sources is " + max_num_sources);
							System.exit(1);
						}
						break;
					case 'T':
						num_thrds = Integer.parseInt(gopt.getOptarg());
						if (num_thrds > max_num_threads)
						{
							System.err.println("Too many threads specified. Max number of threads is " + max_num_threads);
							System.exit(1);
						}
						break;
					case 't':
						tightloop = true;
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
				System.err.println("lbmstrm: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error)
		{
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
	
	private lbmstrm(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmstrm");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		byte [] message = new byte[msglen];
		if (num_thrds > num_srcs)
		{
			System.err.println("Number of threads must be less than or equal to number of sources");
			System.exit(1);
		}

		System.out.println(msgs_per_sec + " msgs/sec");

		LBMSourceAttributes sattr = null;
		LBMContextAttributes cattr = null;
		try
		{
			sattr = new LBMSourceAttributes();
			cattr = new LBMContextAttributes();
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating attributes: " + ex.toString());
			System.exit(1);
		}
		
		/* Check if protocol needs to be set to lbtrm | lbtru */
		if (protocol == 'U')
		{
			try
			{
				sattr.setProperty("transport", "LBTRU");
				cattr.setProperty("transport_lbtru_data_rate_limit", Integer.toString(send_rate));
				cattr.setProperty("transport_lbtru_retransmit_rate_limit", Integer.toString(retrans_rate));
			}
			catch (LBMRuntimeException ex)
			{
				System.err.println("Error setting LBTRU rate: " + ex.toString());
				System.exit(1);
			}														
		}		
		if (protocol == 'M') 
		{
			try
			{
				sattr.setProperty("transport", "LBTRM");
				cattr.setProperty("transport_lbtrm_data_rate_limit", Integer.toString(send_rate));
				cattr.setProperty("transport_lbtrm_retransmit_rate_limit", Integer.toString(retrans_rate));
			}
			catch (LBMRuntimeException ex)
			{
				System.err.println("Error setting LBTRM rates: " + ex.toString());
				System.exit(1);
			}
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
		catch (Exception ex)
		{
			System.err.println("Error setting operational_mode: " + ex.toString());
			System.exit(1);
		}
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
		StrmSrcCB srccb = new StrmSrcCB();
		LBMContextThread ctxthread = null;
		if (sequential)
		{
			System.err.println("Sequential mode");
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		else
		{
			System.err.println("Embedded mode");
		}
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
			if (monitor_context)
			{
				try
				{
					lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
				}
				catch (LBMException ex)
				{
					System.err.println("Error monitoring context: " + ex.toString());
					System.exit(1);
				}
			}
		}
		msgs_per_sec = msgs_per_sec/num_thrds;
		LBMSource [] sources = new LBMSource[num_srcs];
		for (int i = 0; i < num_srcs; i++)
		{
			int topicnum = initial_topic_number + i;
			String topicname = topicroot + "." + topicnum;
			LBMTopic topic =  null;

			try
			{
				topic =  ctx.allocTopic(topicname, sattr);
				sources[i] = ctx.createSource(topic, srccb, null, null);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating source: " + ex.toString());
				System.exit(1);
			}
			if (i > 1 && (i % 1000) == 0)
			{
				System.out.println("Created " + i + " sources");
			}
			if (monitor_source)
			{
				try
				{
					lbmmonsrc.start(sources[i],
							application_id + "(" + i + ")",
							monitor_source_ivl);
				}
				catch (LBMException ex)
				{
					System.err.println("Error monitoring source: " + ex.toString());
					System.exit(1);
				}
			}
		}
		System.out.println("Created " + num_srcs + " sources. Will start sending data now.");
		System.out.println("Using " + num_thrds + " threads to send " +
			msgs + " messages of size " + msglen +
			" bytes (" + (msgs/num_thrds) + " messages per thread).");
		System.out.flush();
		LBMStrmSrcThread [] srcthreads = new LBMStrmSrcThread[num_thrds];
		for (int i = 1; i < num_thrds; i++)
		{
			srcthreads[i] = new LBMStrmSrcThread(i, num_thrds, message, msglen, msgs/num_thrds, sources, num_srcs, msgs_per_sec, tightloop);
			srcthreads[i].start();
		}
		srcthreads[0] = new LBMStrmSrcThread(0, num_thrds, message, msglen, msgs/num_thrds, sources, num_srcs, msgs_per_sec, tightloop);
		srcthreads[0].run();
		System.out.println("Done sending on thread 0. Waiting for any other threads to finish.");
		for (int i = 1; i < num_thrds; i++)
		{
			System.out.println("Joining thread " + i);
			srcthreads[i].join();
			System.out.println("Joined thread " + i);
		}
		System.out.flush();
		if (linger > 0)
		{
			System.out.println("Lingering for "
					     + linger
					     + " seconds...");
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
				System.exit(1);
			}
		}
		if (do_stats)
		{
			try
			{
				print_stats(ctx, num_srcs, sources[0].getAttributeValue("transport"));
			}
			catch (LBMException ex)
			{
				System.err.println("Error getting statistics: " + ex.toString());
				System.exit(1);
			}
		}
		System.out.println("Quitting...");
	}
	private static void print_stats(LBMContext ctx, int nsrcs, String transport_type) throws LBMException
	{
		int n = 0;
		if(transport_type.equalsIgnoreCase("LBT-RM"))
		{
			n = (int)(inet_aton(ctx.getAttributeValue("transport_lbtrm_multicast_address_high"))
					- inet_aton(ctx.getAttributeValue("transport_lbtrm_multicast_address_low"))) + 1;
		}
		else if(transport_type.equalsIgnoreCase("LBT-IPC"))
		{
			try{
				n = Integer.parseInt(ctx.getAttributeValue("transport_lbtipc_id_high"))
				  - Integer.parseInt(ctx.getAttributeValue("transport_lbtipc_id_low")) + 1;
			} catch (Exception e){
				System.err.println(e);
			}
		}
		else
		{
			try{				
				n = Integer.parseInt(ctx.getAttributeValue("transport_tcp_maximum_ports"));
			}catch (Exception e){
				System.err.println(e);
			}
		}
		if (nsrcs < n)
			n = nsrcs;
		LBMSourceStatistics stats = new LBMSourceStatistics(ctx, n);
		for (int i = 0; i < stats.size(); i++)
		{
			switch (stats.type(i))
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.out.println("TCP, source " + stats.source(i)
									+ " buffered " + stats.bytesBuffered(i)
									+ ", clients " + stats.numberOfClients(i));
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
					System.out.println("LBT-RU, source " + stats.source(i)
									+ " sent " + stats.messagesSent(i) + "/" + stats.bytesSent(i)
									+ ", naks " + stats.naksReceived(i) + "/" + stats.nakPacketsReceived(i)
									+ ", ignored " + stats.naksIgnored(i) + "/" + stats.naksIgnoredRetransmitDelay(i)
									+ ", shed " + stats.naksShed(i)
									+ ", rxs " + stats.retransmissionsSent(i)
									+ ", clients " + stats.numberOfClients(i));
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.out.println("LBT-RM, source " + stats.source(i)
									+ " sent " + stats.messagesSent(i) + "/" + stats.bytesSent(i)
									+ ", txw " + stats.transmissionWindowMessages(i) + "/" + stats.transmissionWindowBytes(i)
									+ ", naks " + stats.naksReceived(i) + "/" + stats.nakPacketsReceived(i)
									+ ", ignored " + stats.naksIgnored(i) + "/" + stats.naksIgnoredRetransmitDelay(i)
									+ ", shed " + stats.naksShed(i)
									+ ", rxs " + stats.retransmissionsSent(i)
									+ ", rctl " + stats.messagesQueued(i) + "/" + stats.retransmissionsQueued(i));
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.out.println("LBT-IPC, clients " + stats.numberOfClients()
									+ ", sent "  + stats.messagesSent() +"/" + stats.bytesSent());
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.out.println("LBT-RDMA, clients " + stats.numberOfClients()
									+ ", sent "  + stats.messagesSent() + "/" + stats.bytesSent());
					break;
			}
		}
		System.out.flush();
	}

	public static long inet_aton(String addr)
	{
		int i;
		String [] arrDec;
		long num = 0;
		if (addr.equals(""))
		{
			return 0;
		}
		else
		{
			arrDec = addr.split("\\.");
			for(i = arrDec.length - 1; i >= 0 ; i --)
			{
				try {
					num += Integer.parseInt(arrDec[i]) << (8*(3-i));
				}
				catch (Exception e)
				{
					System.err.println(e);
				}
			}
			return num;
		}
	}
}

class StrmSrcCB implements LBMSourceEventCallback
{
	public boolean blocked = false;

	public int onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
	{
		String clientname;

		switch (sourceEvent.type())
		{
		case LBM.SRC_EVENT_CONNECT:
			clientname = sourceEvent.dataString();
			System.out.println("Receiver connect " + clientname);
			break;
		case LBM.SRC_EVENT_DISCONNECT:
			clientname = sourceEvent.dataString();
			System.out.println("Receiver disconnect " + clientname);
			break;
		case LBM.SRC_EVENT_WAKEUP:
			blocked = false;
			break;
		case LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
		case LBM.SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
		case LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX:
		case LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX:
			break;
		default:
			System.out.println("Unknown source event " + sourceEvent.type());
			break;
		}
		System.out.flush();
		return 0;
	}
}

class LBMStrmSrcThread implements Runnable
{
	private int _threadId;
	private int _numThreads;
	private int _numSrcs;
	private int _nmsgs;
	private int _msgs_per_sec;
	private int _msgs_per_ivl;
	private int _interval = 20;
	private boolean _tightloop;
	private byte[] _message;
	private int _msglen;
	private LBMSource _sources[];
	private Thread myThread;

	public LBMStrmSrcThread(int threadId, int numThreads, byte[] message, int msglen, int nmsgs, LBMSource sources[], int numSrcs,
						int msgs_per_sec, boolean tightloop) 
	{
		_threadId = threadId;
		_numThreads = numThreads;
		_numSrcs = numSrcs;
		_nmsgs = nmsgs;
		_sources = sources;
		_message = message;
		_msglen = msglen;
		_msgs_per_sec = msgs_per_sec;
		_tightloop = tightloop;
		calc_rate_vals();
	}

	public void start()
	{
		myThread = new Thread(this);
		myThread.start();
	}

	public void join()
	{
		if (myThread != null)
		{
			try {
				myThread.join();
			}
			catch (Exception e) {}
		}
	}

	public void run()
	{
		int block_cntr = 0;
		long nxtmsgms;
		int msg_num = 1;

		int imsgs = _msgs_per_ivl;

		long curr_time;
		long start_time = System.currentTimeMillis();
		int i = _threadId;
		while (_nmsgs > 0)
		{
			curr_time = System.currentTimeMillis();

			nxtmsgms = start_time + ((((long)msg_num * 1000000) / _msgs_per_sec) / 1000);
			if(_tightloop) {
				while(curr_time < nxtmsgms)
					curr_time = System.currentTimeMillis();
			}
			else
			{
				if(curr_time < nxtmsgms) try
				{
					Thread.sleep(nxtmsgms - curr_time);
				}
				catch (InterruptedException e) { }
			}

			try
			{
				_sources[i].send(_message, _msglen, LBM.SRC_NONBLOCK);
			}
			catch (LBMEWouldBlockException ex)
			{
				block_cntr++;
				if(block_cntr % 1000 == 0)
					System.out.println("LBM send blocked 1000 times");
			}
			catch (LBMException ex)
			{
				System.err.println("Error sending message: " + ex.toString());
			}

			++msg_num;
			i += _numThreads;
			if(i >= _numSrcs) i = _threadId;

			if(--_nmsgs <= 0) break;
		}
	}

	/*
	 * Function that determines how to pace sending of messages to obtain a given
	 * rate.  Given messages per second, calculates number of messages to send in 
	 * a particular interval and the number of milliseconds to pause between 
	 * intervals.
	 */
	private void calc_rate_vals()
	{
		int intervals_per_sec = 1000;

		intervals_per_sec = 1000/(_interval);

		while(_interval <= 1000 && _msgs_per_sec % intervals_per_sec != 0)
		{
			_interval++;
			while(((1000 % _interval) != 0) && _interval <= 1000)
				_interval++;
			intervals_per_sec = 1000/_interval;
		}
		_msgs_per_ivl = _msgs_per_sec/intervals_per_sec;
	}

}

