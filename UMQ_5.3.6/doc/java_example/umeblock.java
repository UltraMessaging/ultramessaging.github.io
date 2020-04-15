import com.latencybusters.lbm.*;
import com.latencybusters.auxapi.*;

import java.util.*;
import java.text.NumberFormat;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;
import java.util.concurrent.Semaphore;

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

import java.io.*;

class umeblock
{
	private static String Purpose     = "Purpose: Send messages on a single topic using blocking send.";
	private static String Usage       = "Usage: umeblock [options] topic\n"
									  + "Available options:\n"
									  + "  -c, --config=FILE         use LBM configuration file FILE\n"
									  + "  -l, --length=NUM          send messages of NUM bytes\n"
									  + "  -M, --messages=NUM        send NUM messages\n"
									  + "  -v, --verbose             print additional info in verbose form\n";
	public static void main(String[] args)
	{
		LBMSourceSendExInfo exinfo = null;
		UMEBlockSrc blksrc = null;
		LBM lbm = null;
		byte [] message;
		BlockCB appcb;
		boolean verbose = false;
		long bytes_sent = 0;
		int msglen  = 25;
		int msgs    = 10000000;
		int c = -1;

		try
		{
			lbm = new LBM();
		}
		catch(LBMException ex)
		{
			System.err.println("Error initializing LBM: " + ex.toString());
			System.exit(1);
		}

		org.apache.log4j.Logger logger;
		logger = org.apache.log4j.Logger.getLogger("umeblock");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);
		String confname = null;

		LongOpt[] longopts = new LongOpt[5];

		final int OPTION_CONFIG  = 'c';
		final int OPTION_LENGTH  = 'l';
		final int OPTION_MESSAGE = 'M';
		final int OPTION_VERBOSE = 'v';
		final int OPTION_HELP    = 'h';
		
		longopts[0] = new LongOpt("config", LongOpt.REQUIRED_ARGUMENT, null, OPTION_CONFIG);
		longopts[1] = new LongOpt("length", LongOpt.REQUIRED_ARGUMENT, null, OPTION_LENGTH);
		longopts[2] = new LongOpt("message", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MESSAGE);
		longopts[3] = new LongOpt("verbose", LongOpt.REQUIRED_ARGUMENT, null, OPTION_VERBOSE);
		longopts[4] = new LongOpt("help", LongOpt.REQUIRED_ARGUMENT, null, OPTION_HELP);

		Getopt gopt = new Getopt("umeblock", args, "c:l:M:vh", longopts);

		while((c = gopt.getopt()) != -1)
		{
			try
			{
				switch(c)
				{
					case OPTION_CONFIG:
						confname = gopt.getOptarg();
						break;

					case OPTION_LENGTH:
						msglen = Integer.parseInt(gopt.getOptarg());
						break;

					case OPTION_MESSAGE:
						msgs = Integer.parseInt(gopt.getOptarg());
						break;

					case OPTION_VERBOSE:
						verbose = true;
						break;

					case OPTION_HELP:
						print_help_exit(0);
						break;
				}
			}
			catch (Exception e)
			{
				/* type conversion exception */
				System.err.println("umeblock: error\n" + e);
				print_help_exit(1);
			}
		}

		if(confname == null)
		{
			print_help_exit(1);
		}

		/* Setup configuration file */	
		try
		{
			LBM.setConfiguration(confname);
		}
		catch(LBMException ex)
		{
			System.err.println("Error setting LBM configuration: " + ex.toString());
			System.exit(1);
		}

		/* Setup context attributes */
		LBMContextAttributes cattr = null;
		try
		{
			cattr = new LBMContextAttributes();
		}
		catch(LBMException ex)
		{
			System.err.println("Error creating conxtext attributes: " + ex.toString());
			System.exit(1);
		}

		/* Setup source attributes */
		LBMSourceAttributes sattr = null;
		try
		{
			sattr = new LBMSourceAttributes();
		}
		catch(LBMException ex)
		{
			System.err.println("Error creating source attributes: " + ex.toString());
			System.exit(1);
		}

		/* Setup context */
		LBMContext ctx = null;
		try
		{
			ctx = new LBMContext(cattr);
		}
		catch(LBMException ex)
		{
			System.err.println("Error creating context: " + ex.toString());
			System.exit(1);
		}

		/* Setup topic */
		LBMTopic topic = null;
		try
		{
			topic = ctx.allocTopic(args[gopt.getOptind()], sattr);
		}
		catch(LBMException ex)
		{
			System.err.println("Error allocating topic: " + ex.toString());
			System.exit(1);
		}

		/* Create Source */
		appcb = new BlockCB(verbose);
		try
		{
			blksrc = new UMEBlockSrc();
			blksrc.createSource(ctx, topic, sattr, appcb, null, null);
		}
		catch(LBMException ex)
		{
			System.err.println("Error creating source: " + ex.toString());
			System.exit(1);
		}
		catch(InterruptedException ex)
		{
			System.err.println("Error acquiring lock!");
			System.exit(1);
		}

		//Sleep 1 second
		try
		{
			Thread.sleep(1000);
		}
		catch(InterruptedException e) { }

		message = new byte[msglen];
		exinfo  = new LBMSourceSendExInfo();
		long start_time = System.currentTimeMillis();
		for(int i=0; i<msgs; i++)
		{
			exinfo.setClientObject(i);
			exinfo.setFlags(LBM.SRC_SEND_EX_FLAG_SEQUENCE_NUMBER_INFO);

			try
			{
				blksrc.send(message, msglen, 0, exinfo);
			}
			catch(LBMException ex)
			{
				System.err.println("Error sending: " + ex.toString());
				System.exit(1);
			}
			catch(InterruptedException ex)
			{
				System.err.println("Interrupted exception.");
				System.exit(1);
			}
			catch(Exception ex)
			{
				System.err.println("General exception ocurred: " + ex.toString());
				System.exit(1);
			}

			bytes_sent += msglen;
		}

		long end_time = System.currentTimeMillis();
		double secs   = (end_time - start_time) / 1000.;
		print_bw(secs, msgs, bytes_sent);

		try
		{
			System.out.println("Lingering for 5 seconds...");
			Thread.sleep(5000);
		}
		catch(InterruptedException e) { }

		try
		{
			blksrc.close();
			//src.close();
		}
		catch(LBMException ex)
		{
			System.err.println("Error closing source: " + ex.toString());
		}

		ctx.close();
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(Purpose);
		System.err.println(Usage);
		System.exit(exit_value);
	}

	private static void print_bw(double sec, int msgs, long bytes)
	{
		double mps = 0;
		double bps = 0;
		double kscale = 1000;
		double mscale = 1000000;
		char mgscale = 'K';
		char bscale = 'K';
		
		mps = msgs/sec;
		bps = bytes*8/sec;
		if (mps <= mscale)
		{
			mgscale = 'K';
			mps /= kscale;
		}
		else
		{
			mgscale = 'M';
			mps /= mscale;
		}
		if (bps <= mscale)
		{
			bscale = 'K';
			bps /= kscale;
		}
		else
		{
			bscale = 'M';
			bps /= mscale;
		}

		NumberFormat nf = NumberFormat.getInstance();
		nf.setMaximumFractionDigits(3);
		System.out.println(sec
				   + " secs. "
				   + nf.format(mps)
				   + " " + mgscale + "msgs/sec. "
				   + nf.format(bps)
				   + " " + bscale + "bps");
	}
}

class BlockCB implements LBMSourceEventCallback
{
	boolean _verbose;

	public BlockCB(boolean v)
	{
		_verbose = v;
	}

	private void print(String err)
	{
		if(!_verbose)
			return;
	
		print(err, true);
	}

	private void print(String err, boolean newline)
	{
		if(!_verbose)
			return;

		if(newline)
			System.err.println(err);
		else
			System.err.print(err);
	}

	public int onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
	{
		switch(sourceEvent.type())
		{
			case LBM.SRC_EVENT_UME_REGISTRATION_ERROR:
				print("Registration error: " + sourceEvent.dataString());
				break;

			case LBM.SRC_EVENT_UME_STORE_UNRESPONSIVE:
				print("Store unresponsive: " + sourceEvent.dataString());
				break;

			case LBM.SRC_EVENT_UME_MESSAGE_STABLE:
				print("Stable ACK arrived");
				break;
				
			case LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX:
				UMESourceEventAckInfo ack = sourceEvent.ackInfo();

				print("Stable EX ACK. UME store " + String.valueOf(ack.storeIndex()) + ": " + ack.store() + " message stable. SQN " + String.valueOf(ack.sequenceNumber()) + ". Flags " + String.valueOf(ack.flags()) + " ", false);

				if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) == 1)
					print("IA ", false);

				if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) == 1)
					print("IR ", false);

				if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE) == 1)
					print("STABLE ", false);

				if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STORE) == 1)
					print("STORE ", false);

				print(" ");
				
				break;

			case LBM.SRC_EVENT_SEQUENCE_NUMBER_INFO:
				LBMSourceEventSequenceNumberInfo info = sourceEvent.sequenceNumberInfo();
				print("Sequence number info. first: " + String.valueOf(info.firstSequenceNumber()) + " last: " + String.valueOf(info.lastSequenceNumber()));

				break;

			default:
				print("callback event: " + String.valueOf(sourceEvent.type()));
				break;
		}

		return 0;
	}
}

