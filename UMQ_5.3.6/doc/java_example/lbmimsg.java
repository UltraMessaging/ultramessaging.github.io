import com.latencybusters.lbm.*;
import java.util.Date;
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

class lbmimsg
{
	private static String pcid = "";
	private static int msgs = 10000000;
	private static boolean eventq = false;
	private static boolean verbose = false;
	private static String purpose = "Purpose: Send immediate messages on a single topic or send topic-less messages.";
	private static String usage =
     "Usage: lbmimsg [options] topic\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n" 
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -d delay = delay sending for delay seconds after source creation\n"
	+ "  -h = help\n"
	+ "  -l len = send messages of len bytes\n"
	+ "  -L linger = linger for linger seconds before closing context\n"
	+ "  -M msgs = send msgs number of messages\n"
	+ "  -o = send immediate topic-less messages\n"
	+ "  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
	+ "                     DATA bits per second, and set retransmit rate limit to\n"
	+ "                     RETR bits per second.  For both limits, the optional\n"
	+ "                     k, m, and g suffixes may be used.  For example,\n"
	+ "                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
	+ "  -T target = target for unicast immediate messages\n"
	;

	public static void main(String[] args)
	{
		lbmimsg imsgapp = new lbmimsg(args);
	}

	int send_rate = 0;							//	Used for lbmtrm | lbtru transports
	int retrans_rate = 0;						//
	char protocol = '\0';						//
	int linger = 5;
	int delay = 1;
	String target = null;
	String topic = null;
	boolean topic_less = false;
	LBM lbm = null;
	int msglen = 25;
	long bytes_sent = 0;

	private void process_cmdline(String[] args)
	{
		GetOpt gopt = new GetOpt(args, "+c:d:hl:L:M:oR:T:");
		gopt.optErr = true;
		boolean error = false;
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
						delay = Integer.parseInt(gopt.optArgGet());
						break;
					case 'h':
						print_help_exit(0);
						break;
					case 'l':
						msglen = Integer.parseInt(gopt.optArgGet());
						break;
					case 'L':
						linger = Integer.parseInt(gopt.optArgGet());
						break;
					case 'M':
						msgs = Integer.parseInt(gopt.optArgGet());	
						break;
					case 'o':
						topic_less = true;
						break;
					case 'R':
						ParseRateVars parseRateVars = lbmExampleUtil.parseRate(gopt.optArgGet());
						if (parseRateVars.error) 
						{
							print_help_exit(1);
						}
						protocol = parseRateVars.protocol;
						send_rate = parseRateVars.rate;
						retrans_rate = parseRateVars.retrans;
						break;
					case 'T':
						target = gopt.optArgGet();
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
				System.err.println("lbmimsg: error\n" + e);
				print_help_exit(1);
			}
		}
				
		if (error || (gopt.optIndexGet() >= args.length && !topic_less))
		{	
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		
		if (args.length > gopt.optIndexGet() && topic_less) {
			/* User chose topic-less and yet specified a topic - print help and exit */
			System.out.println("lbmimsg: error--selected topic-less option and still specified topic");
			print_help_exit(1);
		}
		
		if (gopt.optIndexGet() < args.length)
		{
			topic = args[gopt.optIndexGet()];
		}
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}
	
	private lbmimsg(String[] args)
	{
		org.apache.log4j.Logger logger;
		try
		{
			lbm = new LBM();
		}
		catch (LBMException ex)
		{
			System.err.println("Error initializing LBM: " + ex.toString());
			System.exit(1);
		}
		logger = org.apache.log4j.Logger.getLogger("lbmimsg");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		byte [] message = new byte[msglen];

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
		
		if (delay > 0) 
		{
			System.out.printf("Will start sending in %d second%s...\n", delay, ((delay > 1) ? "s" : ""));
			try
			{
				Thread.sleep(delay * 1000);
			}
			catch (InterruptedException e)
			{
				System.err.println("lbmimsg: error\n" + e);
			}
		}
		
		System.out.printf("Sending %d%s immediate messages of size %d bytes to target <%s> topic <%s>\n", 
		                  msgs, (topic_less == true ? " topic-less" : ""), msglen, 
						  (target == null ? "" : target), (topic == null ? "" : topic));
		System.out.flush();				  
		long start_time = System.currentTimeMillis();
		for (int count = 0; count < msgs; count++)
		{
			try
			{
				if (target == null)
				{
					ctx.send(topic, message, msglen, 0);
				}
				else
				{
					ctx.send(target, topic, message, msglen, 0);
				}
				bytes_sent += msglen;
			}
			catch (LBMException ex)
			{
				if (target != null && ex.errorNumber() == lbm.EOP) {
					System.err.println("LBM send() error: no connection to target while sending unicast immediate message");
				}
				else {
					System.err.println("LBM send() error: " + ex.toString());
				}
			}
		}
		long end_time = System.currentTimeMillis();
		double secs = (end_time - start_time) / 1000.;
		
		System.out.printf("Sent %d%s immediate messages of size %d bytes in %.03f seconds.\n", 
		                   msgs, (topic_less == true ? " topic-less" : ""), msglen, secs);
						   
		print_bw(secs, msgs, bytes_sent);
		
		if (linger > 0)
		{
			System.out.printf("Lingering for %d second%s...\n",
			                  linger, ((linger > 1) ? "s" : ""));
			System.out.flush();	
			try
			{
				Thread.sleep(linger * 1000);
			}
			catch (InterruptedException e)
			{ 
				System.err.println("lbmimsg: error\n" + e);
			}
		}
		
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
	}
}
