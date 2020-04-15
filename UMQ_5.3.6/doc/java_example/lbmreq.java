import com.latencybusters.lbm.*;
import java.util.*;
import java.text.NumberFormat;
import Utilities.GetOpt;  // See https://communities.informatica.com/infakb/faq/5/Pages/80008.aspx

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

class LBMReqEvqDispThread extends Thread
{        
	private LBMEventQueue _evq;
	private long _msec = 1000;
	private boolean _continue_thread = true;
	private boolean _running = false;
	private final static long DefaultMSec = 1000;
	/**
	Instantiate an event queue run thread object.
	@param ctx LBMContext to run on this thread.
	The thread will run for 1000ms at a time.
	*/
	public LBMReqEvqDispThread(LBMEventQueue evq)  
	{  
		this(evq, DefaultMSec);
	}
	/**
	Instantiate an LBM Context Thread object.
	@param ctx LBMContext to run on this thread.
	@param msec Milliseconds at a time to run the thread.
	*/
	public LBMReqEvqDispThread(LBMEventQueue evq, long msec)
	{
		super();
		_evq = evq;
		_msec = msec;
	}
	/**
	Stop the context thread.
	*/
	public void terminate()
	{
		_continue_thread = false;
		while (_running)
		{
			try
			{
				_evq.stop();
				Thread.sleep(250);
			}
			catch (Exception e)
			{ 
				System.out.println("lbmreq: error\n" + e);
			}
		}
	}
	public void run()
	{
		_running = true;
		while (_continue_thread)
		{
			try
			{
				_evq.run(_msec);
			}
			catch (Exception e)
			{
				System.out.println("lbmreq: error\n" + e);
			}
		}
		_running = false;
	}
}  // LBMReqEvqDispThread


class lbmreq
{
	private static final int MIN_ALLOC_MSGLEN = 25;
	private static String pcid = "";
	private static int requests = 10000000;
	private static boolean eventq = false;
	private static boolean send_immediate = false;
	private static int stats_sec = 0;
	private static int verbose = 0;
	private static boolean sequential = true;
	private static String purpose = "Purpose: Send request messages from a single source with settable interval between messages.";
	private static String usage =
	  "Usage: [options] topic\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -d delay = delay sending for delay seconds after source creation\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -i = send immediate requests\n"
	+ "  -l len = send messages of len bytes\n"
	+ "  -L linger = linger for linger seconds before closing context\n"
	+ "  -P sec = pause sec seconds after sending request (for responses to arrive)\n"
	+ "  -q use event queue\n"
	+ "  -r [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
	+ "                     DATA bits per second, and set retransmit rate limit to\n"
	+ "                     RETR bits per second.  For both limits, the optional\n"
	+ "                     k, m, and g suffixes may be used.  For example,\n"
	+ "                     '-r 1m/500k' is the same as '-r 1000000/500000'\n"
	+ "  -R requests = send request number of requests\n"
	+ "  -T target = target for unicast immediate requests\n"
	+ "  -v = be verbose\n"
	+ "  -v -v = be even more verbose\n"
	;

	public static void main(String[] args)
	{
		lbmreq reqapp = new lbmreq(args);
	}

	String target = null;
	int send_rate = 0;							//	Used for lbmtrm | lbtru transports
	int retrans_rate = 0;						//
	char protocol = '\0';						//
	int linger = 5;
	LBM lbm = null;
	int msglen = MIN_ALLOC_MSGLEN;
	long bytes_sent = 0;
	int pause_sec = 5;
	int delay = 1;
	String topic_str = null;

	private void process_cmdline(String[] args)
	{
		GetOpt gopt = new GetOpt(args, "c:d:ehil:L:P:qR:r:T:v");
		gopt.optErr = true;
		int c = -1;
		while ((c = gopt.getopt()) != gopt.optEOF)
		{
			try
			{
				switch (c)
				{
					case 'c':
						try 
						{
							LBM.setConfiguration(gopt.optArgGet());
						}
						catch (LBMException ex) 
						{
							System.err.println("Error setting LBM configuration: " + ex.toString());
							System.exit(1);
						}
						break;
					case 'd':
						delay = gopt.processArg(gopt.optArgGet(), delay);
						break;
					case 'e':
						sequential = false;
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						send_immediate = true;
						break;
					case 'l':
						msglen = Integer.parseInt(gopt.optArgGet());
						break;
					case 'L':
						linger = Integer.parseInt(gopt.optArgGet());
						break;
					case 'P':
						pause_sec = Integer.parseInt(gopt.optArgGet());
						break;
					case 'R':
						requests = Integer.parseInt(gopt.optArgGet());
						break;
					case 'r':
						ParseRateVars parseRateVars = lbmExampleUtil.parseRate(gopt.optArgGet());
						if (parseRateVars.error) {
							print_help_exit(1);
						}
						protocol = parseRateVars.protocol;
						send_rate = parseRateVars.rate;
						retrans_rate = parseRateVars.retrans;
						break;
					case 'q':
						eventq = true;
						break;
					case 's':
						stats_sec = Integer.parseInt(gopt.optArgGet());
						break;
					case 'T':
						target = gopt.optArgGet();
						break;
					case 'v':
						verbose++;
						break;
					default:
						print_help_exit(1);
				}
			}
			catch (Exception e)
			{
				/* type conversion exception */
				System.err.println("lbmreq: error\n" + e);
				print_help_exit(1);
			}
		}
		if (gopt.optIndexGet() >= args.length)
		{
			if (!send_immediate)
			{
				print_help_exit(1);
			}
		}
		else
		{
			topic_str  = args[gopt.optIndexGet()];
		}
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmreq(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmreq");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);
		
		byte [] message = null;
		/* if message buffer is too small, then the message[i] will cause issues. 
		Therefore, allocate with a MIN_ALLOC_MSGLEN */
		if (msglen < MIN_ALLOC_MSGLEN) 
		{
			message = new byte[MIN_ALLOC_MSGLEN];
		} 
		else 
		{
			message = new byte[msglen];
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
		LBMTopic topic = null;
		LBMSource src = null;
		LBMReqEvqDispThread evqthread = null;
		LBMEventQueue evq = null;
		
		if (eventq)
		{
			try
			{
				evq = new LBMEventQueue();
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating event queue: " + ex.toString());
				System.exit(1);
			}
			System.err.println("Using EventQueue");
		} else {
			System.err.println("Not using event queue");
		}
		LBMreqCB srccb = new LBMreqCB(verbose,evqthread,evq);
		LBMContextThread ctxthread = null;

		if (sequential)
		{
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		if (!send_immediate)
		{
			try
			{
				topic =  ctx.allocTopic(topic_str, sattr);
				src = ctx.createSource(topic, srccb, evq);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating source: " + ex.toString());
				System.exit(1);
			}
			
			if (delay > 0)
			{
				System.out.printf("Delaying requests for %d second%s...\n", delay, ((delay > 1) ? "s" : ""));
				
				try
				{
					Thread.sleep(delay * 1000);
				}
				catch (InterruptedException e)
				{ 
					System.err.println("lbmreq: error\n" + e);
				}
			}
		}
		byte [] ba;
		if (requests > 0)
			System.out.printf("Will send %d request%s\n", requests, (requests == 1 ? "" : "s"));
		for (int count = 0; count < requests; count++)
		{
			EQTimer timer = null;
			StringBuffer sb = new StringBuffer();
			sb.append("Request data ").append(count);
			try
			{
				ba = sb.toString().getBytes("US-ASCII");
				for (int i = 0; i < ba.length; i++){
					message[i] = ba[i];
				}
			}
			catch (Exception x) 
			{
				System.err.println("lbmreq: error\n" + x);
			}
			LBMRequest req = new LBMRequest(message, msglen);
			req.addResponseCallback(srccb);
			System.out.println("Sending request " + count);
			if (send_immediate)
			{
				if (target == null)
				{
					try
					{
						if (eventq)
						{
							ctx.send(topic_str, req, evq, 0);
						}
						else
						{
							ctx.send(topic_str, req, 0);
						}
					}
					catch (LBMException ex)
					{
						System.err.println("Error sending request: " + ex.toString());
						System.exit(1);
					}
				}
				else
				{
					try
					{
						if (eventq)
						{
							ctx.send(target, topic_str, req, evq, 0);
						}
						else
						{
							ctx.send(target, topic_str, req, 0);
						}
					}
					catch (LBMException ex)
					{
						System.err.println("Error sending request: " + ex.toString());
						System.exit(1);
					}
				}
			}
			else
			{
				try
				{
					if (eventq)
					{
						src.send(req, evq, 0);
					}
					else
					{
						src.send(req, 0);
					}
				}
				catch (LBMException ex)
				{
					System.err.println("Error sending request: " + ex.toString());
					System.exit(1);
				}
			}
		
			if ( !eventq ) {
				if (verbose > 0)
				{
					System.out.println("Sent request "
							   + count
							   +". Pausing "
							   + pause_sec
							   + " seconds.");
				}
				try
				{
					Thread.sleep(pause_sec * 1000);
				}
				catch (InterruptedException e) 
				{
					System.err.println("lbmreq: error\n" + e);
				}
			} else {
				if (verbose > 0)
				{ 
					System.out.println("Sent request "
							   + count
							   +". Starting event pump " );
				}
				try 
				{
					timer = new EQTimer (ctx, pause_sec * 1000, evq);
				} 
				catch (Exception e ) 
				{
					System.out.println("lbmreq: error--" + e);
				}
				evq.run(LBM.EVENT_QUEUE_BLOCK);
			}
			
			System.out.printf("Done waiting for responses, %d response%s (%d total bytes) received. Deleting request.\n\n",
                               srccb.response_count, (srccb.response_count == 1 ? "" : "s"), srccb.response_byte_count);
		
			srccb.response_count = 0;
			srccb.response_byte_count = 0;
			try
			{    
				req.close();  // ignore late responses
			}
			catch (Exception x)
			{
				System.err.println("Request close exception: " + x.toString());
			}
		}
		if (linger > 0)
		{
			System.out.printf("Lingering for %d second%s...\n",
			                  linger, ((linger > 1) ? "s" : ""));
			try
			{
				Thread.sleep(linger * 1000);
			}
			catch (InterruptedException e) 
			{
				System.err.println("lbmreq: error\n" + e);
			}
		}
		if (sequential)
		{
			ctxthread.terminate();
		}
		System.out.println("Quitting...");
	}

	private static void print_bw(double sec, int msgs, long bytes)
	{
		char scale[] = {'\0', 'K', 'M', 'G'};
		double mps = 0.0, bps = 0.0;
		double kscale = 1000.0;
		int msg_scale_index = 0, bit_scale_index = 0;
		
		if (sec == 0) return; /* avoid division by zero */
		mps = msgs/sec;
		bps = bytes*8/sec;
		
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
		System.out.println(sec
				   + " secs. "
				   + nf.format(mps)
				   + " " + scale[msg_scale_index] + "msgs/sec. "
				   + nf.format(bps)
				   + " " + scale[bit_scale_index] + "bps");
		System.out.flush();	
	}
}

class LBMreqCB implements LBMSourceEventCallback, LBMResponseCallback
{
	public int response_count = 0;
	public int response_byte_count = 0;
	int _verbose;
	LBMEventQueue _evq= null;
	LBMReqEvqDispThread _th = null;

	LBMreqCB(int verbose, LBMEventQueue eq)
	{
		_verbose = verbose;
		_evq = eq;
	}
	LBMreqCB(int verbose, LBMReqEvqDispThread th ) {
		_verbose = verbose;
		_th = th;
	}
	LBMreqCB(int verbose, LBMReqEvqDispThread th, LBMEventQueue eq ) {
		_verbose = verbose;
		_th = th;
		_evq = eq;
	}

	public int onResponse(Object cbArg, LBMRequest req, LBMMessage msg)
	{
		switch (msg.type())
		{
			case LBM.MSG_RESPONSE:
				response_count++;
				response_byte_count += msg.dataLength();
				if (_verbose > 0)
				{
					System.out.println("Response ["
							   + msg.source()
							   + "]["
							   + msg.sequenceNumber()
							   + "], "
							   + msg.dataLength()
							   + " bytes");
					if (_verbose > 1)
						dump(msg);
				}
				break;
			default:
				System.out.println("Unknown message type "
						   + msg.type()
						   + "["
						   + msg.source()
						   +"]");
				break;
		}
		System.out.flush();	
		msg.dispose();
		return 0;
	}

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
			default:
				break;
		}
		System.out.flush();	
		return 0;
	}

	private void dump(LBMMessage msg)
	{
		int i, j;
		byte [] data = msg.data();
		int size = msg.data().length;
		StringBuffer sb;
		int b;

		sb = new StringBuffer();
		for (i=0; i < (size >> 4); i++)
		{
        	for (j=0; j < 16; j++)
			{
				b = ((int)data[(i<<4)+j]) & 0xff;
				if (b < 0x10)
				{
					sb.append("0");
				}
				sb.append(Integer.toHexString(b));
				sb.append(" ");
        	}
			sb.append("\t");
			try
			{
				sb.append(new String(data, i<<4, 16, "US-ASCII"));
			}
			catch (Exception x) 
			{
				System.out.println("lbmreq: error--" + x);
			}
			System.out.println(sb.toString());
    	}
		j = size % 16;
		if (j > 0)
		{
			sb = new StringBuffer();
			for (i=0; i < j; i++)
			{
				b = ((int)data[size-j+i]) & 0xff;
				if (b < 0x10)
				{
					sb.append("0");
				}
				sb.append(Integer.toHexString(b));
				sb.append(" ");
			}
			for (i = j; i < 16; i++)
			{
				sb.append("   ");
			}
			sb.append("\t");
			try
			{
				sb.append(new String(data, size-j, j, "US-ASCII"));
			}
			catch (Exception x) 
			{
				System.out.println("lbmreq: error--" + x);
			}
			System.out.println(sb.toString());
		}
		System.out.flush();	
	}
}

class EQTimer extends LBMTimer 
{
	
	LBMEventQueue _evq = null;
	
	public EQTimer(LBMContext ctx, long delay, LBMEventQueue evq) throws LBMException
	{
		super(ctx, delay, evq);
		_evq = evq;
	}
 
	private void onExpiration()
	{
		_evq.stop();
	}
}
