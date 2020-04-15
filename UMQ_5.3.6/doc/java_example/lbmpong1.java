import com.latencybusters.lbm.*;

import java.util.Date;
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

class lbmpong1
{
	private static String pcid = "";
	private static int msgs = 200;
	private static boolean eventq = false;
	private static boolean sequential = true;
	private static int run_secs = 300;
	private static boolean verbose = false;
	private static boolean end_on_eos = false;
	private static String purpose = "Purpose: One-way message trip processor.";
	private static String usage =
	"Usage: lbmpong1 [options] id\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -E = exit after source ends\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -i ID = producer/consumer IDentifier\n"
	+ "  -M msgs = stop after receiving msgs messages\n"
	+ "  -q = use an LBM event queue\n"
	+ "  -t secs = run for secs seconds\n"
	+ "  -v = be verbose about each message (for RTT only)\n"
	;
	private static LBMContextThread ctxthread = null;

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
		logger = org.apache.log4j.Logger.getLogger("lbmpong1");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);
		GetOpt gopt = new GetOpt(args, "c:Eehi:M:qt:v");
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
					case 'E':
						end_on_eos = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						pcid = gopt.optArgGet();
						break;
					case 'M':
						msgs = Integer.parseInt(gopt.optArgGet());
						break;
					case 'q':
						eventq = true;
						break;
					case 't':
						run_secs = Integer.parseInt(gopt.optArgGet());
						break;
					case 'v':
						verbose = true;
						break;
					default:
						print_help_exit(1);
				}
			} 
			catch (Exception e)
			{
				/* type conversion exception */
				System.err.println("lbmpong1: error\n" + e);
				print_help_exit(1);
			}
		}
		LBMContextAttributes ctx_attr = null;
		try
		{
			ctx_attr = new LBMContextAttributes();
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
			System.err.println("Error setting operational_mode: " + ex.toString());
			System.exit(1);
		}
		LBMContext ctx = null;
		try
		{
			ctx = new LBMContext(ctx_attr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}
		Pong1LBMEventQueue evq = null;
		LBMTopic topic = null;
		try
		{
			topic = ctx.lookupTopic("lbmpong/ping");
		}
		catch (LBMException ex)
		{
			System.err.println("Error looking up topic: " + ex.toString());
			System.exit(1);
		}
		Pong1LBMReceiver rcv = null;
		if (sequential)
		{
			// create thread to handle event processing
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
				evq = new Pong1LBMEventQueue();
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating event queue: " + ex.toString());
				System.exit(1);
			}
			try
			{
				rcv = new Pong1LBMReceiver(ctx, topic, evq, pcid, msgs, verbose, end_on_eos);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
			evq.run(run_secs * 1000);
			if (sequential)
			{
				ctxthread.terminate();
			}
		}
		else if (sequential)
		{
			System.err.println("No event queue, sequential mode");
			try
			{
				rcv = new Pong1LBMReceiver(ctx, topic, pcid, msgs, verbose, end_on_eos);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
			try
			{
				Thread.sleep(run_secs * 1000);
			}
			catch (InterruptedException e) { }
		}
		else
		{
			System.err.println("No event queue, embedded mode");
			try
			{
				rcv = new Pong1LBMReceiver(ctx, topic, pcid, msgs, verbose, end_on_eos);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating receiver: " + ex.toString());
				System.exit(1);
			}
			try
			{
				Thread.sleep(run_secs * 1000);
			}
			catch (InterruptedException e) { }
		}
		System.err.println("Quitting....");
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}
}

class Pong1LBMEventQueue extends LBMEventQueue implements LBMEventQueueCallback
{
	public Pong1LBMEventQueue() throws LBMException
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

class Pong1LBMReceiver extends LBMReceiver
{
	int _msg_count = 0;
	int _msgs = 200;
	boolean _verbose = false;
	boolean _end_on_eos = false;
	String _pcId = "";
	LBMEventQueue _evq = null;
	public String saved_source = null;

	public Pong1LBMReceiver(LBMContext ctx, LBMTopic topic, LBMEventQueue evq, String pcId, int msgs, boolean verbose, boolean end_on_eos) throws LBMException
	{
		super(ctx, topic, evq);
		_msgs = msgs;
		_verbose = verbose;
		_pcId = pcId;
		_evq = evq;
		_end_on_eos = end_on_eos;
		//timer = new Pong1LBMTimer(this, ctx, 1000, evq);
	}

	public Pong1LBMReceiver(LBMContext ctx, LBMTopic topic, String pcId, int msgs, boolean verbose, boolean end_on_eos) throws LBMException
	{
		super(ctx, topic);
		_msgs = msgs;
		_verbose = verbose;
		_pcId = pcId;
		_end_on_eos = end_on_eos;
		//timer = new Pong1LBMTimer(this, ctx, 1000, null);
	}
 
	protected int onReceive(LBMMessage msg)
	{
		switch (msg.type())
		{
			case LBM.MSG_DATA:
				if (_msg_count == 0)
					saved_source = msg.source();
				_msg_count++;
				long arrive_usecs = System.currentTimeMillis() * 1000;
				long depart_usecs = ba2l(msg.data(), 0) * 1000000 +
							ba2l(msg.data(), 4);
				String prodId = new String(); 
				try {
					int n;
					for (n = 8; n < msg.data().length; n++)
					{
						if (msg.data()[n] == 0)
							break;
					}
					prodId = new String(msg.data(), 8, n-8);
				}
				catch (Exception e) {}
				print_rtt(depart_usecs, arrive_usecs, msg.sequenceNumber(), prodId);
				if (_msg_count == _msgs)
				{
					end();
				}
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
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
					System.err.println("[" + msg.topicName() + "][" + msg.source() + "], LOST BURST");
				break;
			default:
				System.err.println("Unknown lbm_msg_t type " + msg.type() + " [" + msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		return 0;
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

	private void print_rtt(long t1, long t2, long seq, String prodId)
	{
		System.out.println(_pcId + ";" + prodId + ":" + (seq+10000000) + ";" + (t2-t1));
	}
}

class Pong1LBMTimer extends LBMTimer
{
	Pong1LBMReceiver _rcv;

	public Pong1LBMTimer(Pong1LBMReceiver rcv, LBMContext ctx, long delay, LBMEventQueue evq) throws LBMException
	{
		super(ctx, delay, evq);
		_rcv = rcv;
	}
 
	private void onExpiration()
	{
		LBMReceiverStatistics stats;

		if (_rcv.saved_source != null)
		{
			try
			{
				stats = _rcv.getStatistics(_rcv.saved_source);	
				System.err.println("bytes rcved:" +  stats.bytesReceived());
			}
			catch (LBMException ex)
			{
				System.err.println("Error getting receiver statistics: " + ex.toString());
			}
		}
		try
		{
			this.reschedule(1000);
		}
		catch (LBMException ex)
		{
			System.err.println("Error rescheduing timer: " + ex.toString());
		}
	}
}

