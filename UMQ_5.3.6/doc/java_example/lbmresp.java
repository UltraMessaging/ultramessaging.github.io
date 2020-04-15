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

class lbmresp
{
	private static int reap_msgs = 0;
	private static int msgs = 200;
	private static boolean eventq = false;
	private static int verbose = 0;
	private static boolean end_on_eos = false;
	private static boolean sequential = true;
	private static String purpose = "Purpose: Respond to request messages on a single topic.";
	private static String usage =
	"Usage: lbmresp [options] topic\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -E = end after end-of-stream\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -h = help\n"
	+ "  -l len = use len bytes for the length of each response\n"
	+ "  -q = use an LBM event queue\n"
	+ "  -r responses = send responses messages for each request\n"
	+ "  -v = be verbose about each message\n"
	+ "  -v -v = be even more verbose about each message\n"
	;
	private static LBMContextThread ctxthread = null;

	public static void main(String[] args)
	{
		lbmresp respapp = new lbmresp(args);
	}

	LBM lbm = null;
	String topicstr = null;
	int response_len = 25;
	int responses = 1;
	LBMObjectRecycler objRec = new LBMObjectRecycler();	

	private void process_cmdline(String[] args)
	{
		GetOpt gopt = new GetOpt(args, "c:Eehl:r:qv");
		gopt.optErr = true;
		int c = -1;
		while ((c = gopt.getopt()) != gopt.optEOF)
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
				case 'l':
					response_len = gopt.processArg(gopt.optArgGet(), response_len);
					break;
				case 'q':
					eventq = true;
					break;
				case 'r':
					responses = gopt.processArg(gopt.optArgGet(), responses);
					if (responses <= 0){
						/*Negative # of responses not allowed*/
						print_help_exit(1);
					}
					break;
				case 'v':
					verbose++;
					break;
				default:
					print_help_exit(1);
			}
		}
		if (gopt.optIndexGet() >= args.length)
		{
			print_help_exit(1);
		}
		topicstr = args[gopt.optIndexGet()];
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmresp(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmresp");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);
		
		//Lower the defaults for messages since we don't expect to be handling a high rate
	    objRec.setLocalMsgPoolSize(10);
	    objRec.setSharedMsgPoolSize(20);

		process_cmdline(args);

		byte[] response_buffer = new byte[response_len];
		LBMContextAttributes ctx_attr = null;
		try
		{
			ctx_attr = new LBMContextAttributes();
			ctx_attr.setObjectRecycler(objRec, null);
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
		LBMRespEventQueue evq = null;
		LBMTopic topic = null;
		try
		{
			LBMReceiverAttributes rcv_attr = new LBMReceiverAttributes();
			rcv_attr.setObjectRecycler(objRec, null);
			
			topic = new LBMTopic(ctx, topicstr, rcv_attr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error looking up topic: " + ex.toString());
			System.exit(1);
		}
		LBMRespReceiver rcv = null;
		try
		{
			if (sequential)
			{
				// Run the context on a separate thread
				ctxthread = new LBMContextThread(ctx);
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
					evq = new LBMRespEventQueue();
				}
				catch (LBMException ex)
				{
					System.err.println("Error creating event queue: " + ex.toString());
					System.exit(1);
				}
				rcv = new LBMRespReceiver(ctx, topic, evq, verbose, end_on_eos);
				ctx.enableImmediateMessageReceiver(evq);
			}
			else if (sequential)
			{
				System.err.println("No event queue, sequential mode");
				rcv = new LBMRespReceiver(ctx, topic, verbose, end_on_eos);
				ctx.enableImmediateMessageReceiver();
			}
			else
			{
				System.err.println("No event queue, embedded mode");
				rcv = new LBMRespReceiver(ctx, topic, verbose, end_on_eos);
				ctx.enableImmediateMessageReceiver();
			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating receiver: " + ex.toString());
			System.exit(1);
		}

		// This immediate-mode receiver is *only* used for topicless
		// immediate-mode sends.  Immediate sends that use a topic
		// are received with normal receiver objects.
		ctx.addImmediateMessageReceiver(rcv);

		if (ctxthread != null)
		{
			ctxthread.start();
		}
		byte [] ba;
		while (true)
		{
			if (eventq)
			{
				evq.run(100);
			}
			else
			{
				try
				{
					Thread.sleep(100);
				}
				catch (InterruptedException e) { }
			}
			if (rcv.request != null)
			{	
				System.out.printf("Sending response. %d response%s of %d bytes%s (%d total bytes).\n\n",
                                  responses, (responses == 1 ? "" : "s"), response_len, 
								  (responses == 1 ? "" : " each"), responses * response_len);
				System.out.flush();	
				try
				{
					for (int i = 0; i < responses; i++)
					{
						StringBuffer sb = new StringBuffer();
						sb.append("response ").append(i);
						try
						{
							ba = sb.toString().getBytes("US-ASCII");
							for (int j = 0; j < ba.length; j++)
								response_buffer[j] = ba[j];
						}
						catch (Exception x) {}
						rcv.request.respond(response_buffer, response_len, 0);
					}
				}
				catch (LBMException ex)
				{
					System.err.println("Error responding to request: " + ex.toString());
				}
				rcv.request.dispose();
				objRec.doneWithMessage(rcv.request);
				rcv.request = null;
			}
		}
	}
}

class LBMRespEventQueue extends LBMEventQueue implements LBMEventQueueCallback
{
	public LBMRespEventQueue() throws LBMException
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

class LBMRespReceiver extends LBMReceiver implements LBMImmediateMessageCallback
{
	public long imsg_count = 0;
	public long request_count = 0;
	public LBMMessage request = null;

	int _verbose = 0;
	boolean _end_on_eos = false;
	LBMEventQueue _evq = null;

	public LBMRespReceiver(LBMContext ctx, LBMTopic topic, LBMEventQueue evq, int verbose, boolean end_on_eos) throws LBMException
	{
		super(ctx, topic, evq);
		_verbose = verbose;
		_evq = evq;
		_end_on_eos = end_on_eos;
	}

	public LBMRespReceiver(LBMContext ctx, LBMTopic topic, int verbose, boolean end_on_eos) throws LBMException
	{
		super(ctx, topic);
		_verbose = verbose;
		_end_on_eos = end_on_eos;
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
		boolean promoted = false;
		switch (msg.type())
		{
			case LBM.MSG_DATA:
				if (_verbose > 0)
				{
					System.out.print("["
							   + msg.topicName()
							   + "]["
							   + msg.source()
							   + "]["
							   +  msg.sequenceNumber()
							   + "], ");
					System.out.println(msg.data().length + " bytes");
					if (_verbose > 1)
						dump(msg);
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
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS:
				if (_verbose > 0)
				{
					System.out.print("[" + msg.topicName() + "][" + msg.source() + "][" + msg.sequenceNumber() + "],");
					System.out.println(" LOST");
				}
				break;
			case LBM.MSG_UNRECOVERABLE_LOSS_BURST:
				if (_verbose > 0)
				{
					System.out.print("[" + msg.topicName() + "][" + msg.source() + "][" + msg.sequenceNumber() + "],");
					System.out.println(" LOST BURST");
				}
				break;
			case LBM.MSG_REQUEST:
				request_count++;
				boolean skipped = request != null;
				if (_verbose > 0)
				{
					System.out.print("Request ["
							   + msg.topicName()
							   + "]["
							   + msg.source()
							   + "]["
							   + msg.sequenceNumber()
							   + "], ");
					System.out.println(msg.data().length + " bytes" + (skipped ? " (ignored)" : ""));
					if (_verbose > 1)
						dump(msg);
				}
				if (!skipped)
				{
					/* When Zero Object Delivery is enabled, in order to use this
					 * message anywhere outside this onReceive callback, we first
					 * need to promote it to a full, independent LBMMessage object.
					 * We call promote() to do that.  promote() only needs to be
					 * called once on a give message and should only be called within
					 * the onReceive callback itself. */
					msg.promote();
					promoted = true;
					request = msg;
				}
				break;
			default:
				System.out.println("Unknown lbm_msg_t type " + msg.type() + " [" + msg.topicName() + "][" + msg.source() + "]");
				break;
		}
		if(!promoted) {
			msg.dispose();
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
			catch (Exception x) {}
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
			catch (Exception x) {}
			System.out.println(sb.toString());
		}
		System.out.flush();	
	}

	private void end()
	{
		System.exit(0);
	}

}
