import com.latencybusters.lbm.*;

import java.text.NumberFormat;
import java.util.*;
import java.lang.Math.*;
import java.nio.ByteBuffer;

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

class lbmpong
{
	private static int msecpause = 0;
	private static int msgs = 200;
	private static int msglen = 10;
	private static boolean eventq = false;
	private static boolean sequential = true;
	private static boolean ping = false;
	private static int run_secs = 300;
	private static boolean verbose = false;
	private static boolean end_on_eos = false;
	private static boolean rtt_collect = false;
	private static boolean use_mim = false;
	private static int rtt_ignore = 0;
	private static String regid_offset = null;
	private static String purpose = "Purpose: Message round trip processor.";
	private static String usage =
	  "Usage: lbmpong [options] id\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -C = collect RTT data\n"
	+ "  -E = exit after source ends\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -i msgs = send and ignore msgs messages to warm up\n"
	+ "  -I = Use MIM\n"
	+ "  -l len = use len length messages\n"
	+ "  -M msgs = stop after receiving msgs messages\n"
	+ "  -o offset = use offset to calculate Registration ID\n"
	+ "              (as source registration ID + offset)\n"
	+ "              offset of 0 forces creation of regid by store\n"
	+ "  -P msec = pause after each send msec milliseconds\n"
	+ "  -q = use an LBM event queue\n"
	+ "  -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
	+ "                     DATA bits per second, and set retransmit rate limit to\n"
	+ "                     RETR bits per second.  For both limits, the optional\n"
	+ "                     k, m, and g suffixes may be used.  For example,\n"
	+ "                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
	+ "  -t secs = run for secs seconds\n"
	+ "  -v = be verbose about each message (for RTT only)\n"
	+ "  id = either \"ping\" or \"pong\"\n"
	;
	private static LBMContextThread ctxthread = null;

	public static void main(String[] args)
	{
		lbmpong pongapp = new lbmpong(args);
	}

	int send_rate = 0;							//	Used for lbmtrm | lbtru transports
	int retrans_rate = 0;						//
	char protocol = '\0';						//
	LBM lbm = null;

	private void process_cmdline(String[] args)
	{
		Getopt gopt = new Getopt("lbmpong", args, "Cc:EehIi:l:o:M:P:qr:t:v");
		boolean error = false;
		int c = -1;
		while ((c = gopt.getopt()) != -1)
		{
			try
			{
				switch (c)
				{
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
						rtt_collect = true;
						break;
					case 'E':
						end_on_eos = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						rtt_ignore = Integer.parseInt(gopt.getOptarg());
						break;
					case 'I':
						use_mim = true;
						break;
					case 'l':
						msglen = Integer.parseInt(gopt.getOptarg());
						break;
					case 'M':
						msgs = Integer.parseInt(gopt.getOptarg());
						break;
					case 'o':
						regid_offset = gopt.getOptarg();
						break;
					case 'P':
						msecpause = Integer.parseInt(gopt.getOptarg());
						break;
					case 'q':
						eventq = true;
						break;
					case 'r':
						ParseRateVars parseRateVars = lbmExampleUtil.parseRate(gopt.getOptarg());
						if (parseRateVars.error) {
							print_help_exit(1);
						}
						protocol = parseRateVars.protocol;
						send_rate = parseRateVars.rate;
						retrans_rate = parseRateVars.retrans;
						break;
					case 't':
						run_secs = Integer.parseInt(gopt.getOptarg());
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
				System.err.println("lbmpong: error\n" + e);
				print_help_exit(1);
			}
		}
		if (error || gopt.getOptind() >= args.length)
		{
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		if (args[gopt.getOptind()].equals("ping"))
		{
			ping = true;
		}
		else if (!args[gopt.getOptind()].equals("pong"))
		{
			System.err.println("else if (!args[gopt.getOptind()].equals(\"pong\"))");
			System.err.println(LBM.version());
			System.err.println(usage);
			System.exit(1);
		}
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmpong(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmpong");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		if(use_mim && !eventq) {
			System.out.println("Using mim requires event queue to send from receive callback - forcing use");
			eventq = true;
		}
		if (msecpause > 0 && !eventq) {
			System.out.println("Setting pause value requires event queue - forcing use");
			eventq = true;
		}
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
		PongLBMEventQueue evq = null;
		if (sequential)
		{
			// Run the context on a separate thread
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
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
				evq = new PongLBMEventQueue();
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating event queue: " + ex.toString());
				System.exit(1);
			}
		}
		else if (sequential)
		{
			System.err.println("Sequential mode, no event queue");
		}
		else
		{
			System.err.println("Embedded mode, no event queue");
		}
		LBMSource src = null;
		PongLBMReceiver rcv = null;
		LBMTopic src_topic = null;
		LBMTopic rcv_topic = null;
		if(msglen < 8) msglen = 8;
		try
		{
			LBMReceiverAttributes rcv_attr = new LBMReceiverAttributes();
			if (regid_offset != null) {
				/* This is relevant for UME only, but enables this example to be used with persistent streams.
				 * There is no effect by doing this on non persistent streams or if an LBM only license is used
				 */
				PongRegistrationId umeregid;
				umeregid = new PongRegistrationId(regid_offset);
				rcv_attr.setRegistrationIdCallback(umeregid, null);
				System.out.println("Will use RegID offset " + regid_offset + ".");
			}
			if (ping)
			{
				System.err.println("Sending " + msgs + " " + msglen
								   + " byte messages to topic lbmpong/ping pausing "
								   + msecpause + " msec between");
				if(!use_mim)
					src_topic = ctx.allocTopic("lbmpong/ping", sattr);
				rcv_topic = ctx.lookupTopic("lbmpong/pong",rcv_attr);
			}
			else
			{
				rcv_topic =  ctx.lookupTopic("lbmpong/ping",rcv_attr);
				if(!use_mim)
					src_topic =  ctx.allocTopic("lbmpong/pong", sattr);
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error setting up topics: " + ex.toString());
			System.exit(1);
		}
		PongSrcCB srccb = new PongSrcCB();
		try
		{
			if(!use_mim)
				src = ctx.createSource(src_topic, srccb, null);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating source: " + ex.toString());
			System.exit(1);
		}
		try
		{
			rcv = new PongLBMReceiver(ctx,
						  rcv_topic,
						  evq,
						  src,
						  ping,
						  msecpause,
						  msgs,
						  verbose,
						  end_on_eos,rtt_collect,rtt_ignore,use_mim);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating pong receiver: " + ex.toString());
			System.exit(1);
		}
		try
		{
			Thread.sleep(5000);
		}
		catch (InterruptedException e) { }
		if (ping)
		{
			ByteBuffer message = ByteBuffer.allocateDirect(msglen);
			rcv.start();
			
			try
			{
				format(message,0,System.nanoTime());
				if(use_mim)
					ctx.send("lbmpong/ping",message.array(), msglen, LBM.MSG_FLUSH);
				else
					src.send(message, 0, msglen, LBM.MSG_FLUSH);
			}
			catch (LBMException ex)
			{
				System.err.println("Error sending message: " + ex.toString());
				System.exit(1);
			}
		}
		if (eventq)
		{
			evq.run(run_secs * 1000);
		}
		else
		{			
			try
			{
				Thread.sleep(run_secs * 1000);
			}
			catch (InterruptedException e) { }
		}
		if (ctxthread != null)
		{
			ctxthread.terminate();
		}
		if (ping)
		{
			rcv.print_latency(System.out);
			
			// print transport statistics
			try {
				print_stats(rcv, src);
			} catch (LBMException ex) {
				System.out.println("Error printing transport stats");
			}
				
			System.exit(0);
		}
						
		System.err.println("Quitting....");
	}

	/* Format the long in to the byte array */
		public static void format(ByteBuffer buf,int offset,long v) 
		throws LBMException {
			buf.putLong(offset, v);
		}

	/* Convert a signed byte to an unsigned int so it can be or'd */
	private static int convert(byte b) {

		if(b > 0) return (int) b;

		if(b < 0) return 128 + (b & 0x7f);

		return 0;
	}

	/* Parse a byte array containing a long */
	public static long parse_s(ByteBuffer buf, int offset)
	{
		return buf.getLong(offset);
	}	
	
	public static int print_stats(LBMReceiver rcv, LBMSource src) throws LBMException
	{
		LBMReceiverStatistics rcv_stats = null;
		LBMSourceStatistics src_stats = null;
		String source_type = "";
		int nstats = 1;
		
		// Get receiver stats
		try {
			rcv_stats = rcv.getStatistics(nstats);
		} catch (LBMException ex) {
			System.err.println("Error getting receiver statistics: " + ex.toString());
			return -1;
		}
		
		if (rcv_stats == null) {
			System.err.println("Cannot print stats, because receiver stats are null");
			return -1;
		}
		
		// Get source stats
		try {
			src_stats = src.getStatistics();
		} catch (LBMException ex) {
			System.err.println("Error getting source statistics: " + ex.toString());
			return -1;
		}
		
		if (src_stats == null) {
			System.err.println("Cannot print stats, because source stats are null");
			return -1;
		}
				
		// Print transport stats
		switch(src_stats.type())
		{
			case LBM.TRANSPORT_STAT_TCP:
				break;
			case LBM.TRANSPORT_STAT_LBTRU:
			case LBM.TRANSPORT_STAT_LBTRM:
				if (rcv_stats.lost() != 0 || src_stats.retransmissionsSent() != 0) {
					source_type = (src_stats.type() == LBM.TRANSPORT_STAT_LBTRM) ? "LBT-RM" : "LBT-RU";
					System.out.println("The latency for this " + source_type + " session of lbmpong might be skewed by loss");
					System.out.println("Source loss: " + src_stats.retransmissionsSent() + "    " +
										  	 "Receiver loss: " + rcv_stats.lost());
				}
				break;
			case LBM.TRANSPORT_STAT_LBTIPC:
				break;
			case LBM.TRANSPORT_STAT_LBTRDMA:
				break;
		}
		System.out.flush();	
		rcv_stats.dispose();
		src_stats.dispose();
				
		return 0;
	}				
}

class PongLBMEventQueue extends LBMEventQueue implements LBMEventQueueCallback
{
	public PongLBMEventQueue() throws LBMException
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

class PongLBMReceiver extends LBMReceiver
{
	LBMContext _ctx;
	int _msg_count = 0;
	int _msgs = 200;
	int _msecpause = 0;
	boolean _ping = false;
	boolean _verbose = false;
	boolean _end_on_eos = false;
	LBMEventQueue _evq = null;
	LBMSource _src = null;
	private long _start_time = 0;
	int rtt_ignore = 0;
	boolean use_mim = false;

	public PongLBMReceiver(LBMContext ctx, LBMTopic topic, LBMEventQueue evq, LBMSource src, 
						   boolean ping, int msecpause, int msgs, boolean verbose, boolean end_on_eos, 
						   boolean rtt_collect, int ignore, boolean mim) throws LBMException
	{
		super(ctx, topic, evq);
		_ctx = ctx;
		_msgs = msgs;
		_verbose = verbose;
		_msecpause = msecpause;
		_ping = ping;
		_evq = evq;
		_end_on_eos = end_on_eos;
		_src = src;
		//timer = new PongLBMTimer(this, ctx, 1000, evq);
		if(rtt_collect) rtt_data = new double[msgs];
			rtt_ignore = ignore;
		use_mim = mim;
	}

	public void start()
	{
		_start_time = System.currentTimeMillis();
	}
 
	protected int onReceive(LBMMessage msg)
	{
		long t;
		if (_ping)
			t = System.nanoTime();
		else
			t = 0;
		switch (msg.type())
		{
			case LBM.MSG_DATA:
				if(rtt_ignore == 0) {
					_msg_count++;
				}
				ByteBuffer message = msg.dataBuffer();
				long s = 0;
				if (_ping)
				{
					s = lbmpong.parse_s(message,0);
					calc_latency(t,s);
					if (rtt_ignore == 0 && _msg_count == _msgs)
					{
						rtt_avg = (double)curr_usec/(double)_msg_count;

						print_rtt_data();

						print_latency(System.out);
						try {
							lbmpong.print_stats(this, _src);
						}
						catch (LBMException ex) {
							System.out.println("Error printing transport stats");
						}
						
						System.exit(0);
					}
					if (_msecpause > 0)
					{
						// This would not normally be a good
						// thing in a callback on the context
						// thread.
						try
						{
							Thread.sleep(_msecpause);
						}
						catch (InterruptedException e) { }
					}

					try {
					lbmpong.format(message,0,System.nanoTime());
					} catch(LBMException ex) {
						System.err.println("Error formatting message: " + ex.toString());
						System.exit(1);
					}
				}
				try
				{
					if(use_mim)
						_ctx.send(_ping ? "lbmpong/ping" : "lbmpong/pong",message.array(), message.array().length, LBM.MSG_FLUSH | LBM.SRC_NONBLOCK);
					else
						_src.send(message, 0, (int)msg.dataLength(), LBM.MSG_FLUSH | LBM.SRC_NONBLOCK);
				}
				catch (LBMException ex)
				{
					System.err.println("Error sending message: " + ex.toString());
				}
				if(_ping) {
					if(_verbose) {
						System.out.println(_msg_count + " curr " + t + " sent " + s + " latency " + (t - s) + " ns");
					}
				}
				if(rtt_ignore > 0) rtt_ignore--;
				break;
			case LBM.MSG_BOS:
				System.err.println("[" + msg.topicName() + "][" + msg.source() + "], Beginning of Transport Session");
				break;
			case LBM.MSG_EOS:
				System.err.println("[" + msg.topicName() + "][" + msg.source() + "], End of Transport Session");
				if (_end_on_eos)
				{
					end();
				}
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS:
				if (_verbose)
					System.err.println("[" + msg.topicName() + "][" + msg.source() + "][" + msg.sequenceNumber() + "], LOST");
				/* Any kind of loss makes this test invalid */
				System.out.println("Unrecoverable loss occurred.  Quitting...");	
				System.exit(1);
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
				System.err.println("[" + msg.topicName() + "][" + msg.source() + "], LOST BURST");
				/* Any kind of loss makes this test invalid */
				System.out.println("Unrecoverable loss occurred.  Quitting...");	
				System.exit(1);
				break;
			case LBM.MSG_UME_REGISTRATION_SUCCESS_EX:
			case LBM.MSG_UME_REGISTRATION_COMPLETE_EX:
				/* Provided to enable quiet usage of lbmstrm with UME */
				break;
			default:
				System.err.println("Unknown lbm_msg_t type " + msg.type() + " [" + msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		System.out.flush();	
		msg.dispose();
		return 0;
	}

	long curr_usec;
	long min_usec,max_usec;
	long min_usec_idx,max_usec_idx;
	int datanum = 0;
	double rtt_data[];
	double rtt_median,rtt_avg,rtt_stddev;

	public void calc_latency(long curr,long sent)
	{
		long diff = curr - sent;
		if (rtt_ignore == 0) {
			curr_usec += (diff/1000);
			if (diff < min_usec || min_usec == 0) { min_usec = diff; min_usec_idx = datanum; }
			if (diff > max_usec || max_usec == 0) { max_usec = diff; max_usec_idx = datanum; }
			if (rtt_data != null) rtt_data[datanum] = (double)diff/1000000000.0;
			datanum++;
		}
	}
	
	double calc_med() {
		int r;
		boolean changed;
		double t;
		long msgs = _msg_count;

		/* sort the result set */
		do {
			changed = false;
		
			for(r = 0;r < msgs - 1;r++) {
				if(rtt_data[r] > rtt_data[r + 1]) {
					t = rtt_data[r];
					rtt_data[r] = rtt_data[r + 1];
					rtt_data[r + 1] = t;
					changed = true;
				}
			}
		} while(changed);

		if((msgs & 1) == 1) {
			/* Odd number of data elements - take middle */
			return rtt_data[(int)(msgs / 2) + 1];
		} else {
			/* Even number of data element avg the two middle ones */
			return (rtt_data[(int)(msgs / 2)] + rtt_data[((int)msgs / 2) + 1]) / 2;
		}
	}

	double calc_stddev(double mean) {
        	int r;
        	double sum;

        	/* Subtract the mean from the data points, square them and sum them */
        	sum = 0.0;
        	for(r = 0;r < _msg_count;r++) {
                	rtt_data[r] -= mean;
                	rtt_data[r] *= rtt_data[r];
                	sum += rtt_data[r];
        	}

        	sum /= (_msg_count - 1);

        	return Math.sqrt(sum);
	}

	public void print_rtt_data() {
		if(rtt_data != null) {
			int r;
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMaximumFractionDigits(4);

			for(r = 0;r < _msg_count;r++)
				System.err.println("RTT " + nf.format(rtt_data[r] * 1000.0) + " msec, msg " + r);

			/* Calculate median and stddev */
			rtt_median = calc_med() * 1000.0;
			rtt_stddev = calc_stddev(rtt_avg/1000.0);

			print_latency(System.err);
		}
	}

	public void print_latency(java.io.PrintStream fstr)
	{
		double latency = 0;

		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(4);
		if(rtt_data != null) {
			fstr.println("min/max msg = " + nf.format(min_usec_idx) + "/"
					+ nf.format(max_usec_idx)
					+ " median/stddev "
					+ nf.format(rtt_median) + "/"
					+ nf.format(rtt_stddev) +" msec"); 
		}
		latency = rtt_avg / 2.0;
		fstr.println("Elapsed time " + curr_usec + " usecs "
				   + _msg_count + " messages (RTTs). "
				   + "min/avg/max " + nf.format((double)(min_usec/1000000.0))
				   + "/" + nf.format(rtt_avg/1000.0) 
				   + "/" + nf.format((double)(max_usec/1000000.0))
				   + " msec RTT");
		fstr.println("        " + nf.format(latency) + " usec latency");
	}
	
	private void end()
	{
		if (_evq != null)
		{
			_evq.stop();
		}
		else
		{
			System.exit(0);
		}
	}

	private long ba2l(byte[] b, int offset)
	{
		long value = 0;
		for (int i = 0; i < 4; i++)
		{
			int shift = (3-i) * 8;
			value += (b[i+offset] & 0x000000ff) << shift;
		}
		return value;
	}
}

class PongRegistrationId implements UMERegistrationIdExCallback {
	private long _regid_offset;

	public PongRegistrationId(String regid_offset) {
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

class PongSrcCB implements LBMSourceEventCallback
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
		case LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX:
		case LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
		case LBM.SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
		case LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX:
			/* Provided to enable quiet usage of lbmstrm with UME */
			break;
		default:
			break;
		}
		System.out.flush();	
		return 0;
	}
}
