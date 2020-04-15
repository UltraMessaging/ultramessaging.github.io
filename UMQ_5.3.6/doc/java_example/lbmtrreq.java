import com.latencybusters.lbm.*;

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


class lbmtrreq
{
	private static String purpose = "Purpose: Request topic resolution for quiescent components.";
	private static String usage =
	  "Usage: lbmtrreq [options]\n"
	+ "Available options:\n"
	+ "  -c filename =      Use LBM configuration file filename.\n"
	+ "                     Multiple config files are allowed.\n"
	+ "                     Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -C filename =      read Java Properties file for context config\n"
	+ "  -a, --adverts      Request Advertisements\n"
	+ "  -q, --queries      Request Queries\n"
	+ "  -w, --wildcard     Request Wildcard Queries\n"
	+ "  -i, --interval=NUM Interval between requests (milliseconds)\n"
	+ "  -d, --duration=NUM Minimum duration of requests (seconds)\n"
	+ "  -L, --linger=NUM   Linger for NUM seconds before closing context\n"
	;

	public static void main(String[] args)
	{
		lbmtrreq trreqapp = new lbmtrreq(args);
	}
	
	int linger = 5;
	int duration = -1;
	int interval = -1;
	short flags = 0;
	String cconffname = null;

	private void process_cmdline(String[] args)
	{
		LongOpt[] longopts = new LongOpt[6];

		longopts[0] = new LongOpt("adverts", LongOpt.NO_ARGUMENT, null, 'a');
		longopts[1] = new LongOpt("queries", LongOpt.NO_ARGUMENT, null, 'q');
		longopts[2] = new LongOpt("wildcard", LongOpt.NO_ARGUMENT, null, 'w');
		longopts[3] = new LongOpt("interval", LongOpt.REQUIRED_ARGUMENT, null, 'i');
		longopts[4] = new LongOpt("duration", LongOpt.REQUIRED_ARGUMENT, null, 'd');
		longopts[5] = new LongOpt("linger", LongOpt.REQUIRED_ARGUMENT, null, 'L');
		Getopt gopt = new Getopt("lbmtrreq", args, "ac:C:d:hi:L:qw", longopts);
		int c = -1;
		boolean error = false;

		while ((c = gopt.getopt()) != -1)
		{
			try
			{
				switch (c)
				{
					case 'a':
						flags |= LBM.TOPIC_RES_REQUEST_ADVERTISEMENT;
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
						cconffname = gopt.getOptarg();
						break;
					case 'd':
						duration = Integer.parseInt(gopt.getOptarg());
						break;
					case 'h':
						print_help_exit(0);
					case 'i':
						interval = Integer.parseInt(gopt.getOptarg());
						break;
					case 'L':
						linger = Integer.parseInt(gopt.getOptarg());
						break;
					case 'q':
						flags |= LBM.TOPIC_RES_REQUEST_QUERY;
						break;
					case 'w':
						flags |= LBM.TOPIC_RES_REQUEST_WILDCARD_QUERY;
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
				System.err.println("lbmtrreq: error\n" + e);
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

	private lbmtrreq(String[] args)
	{
		LBM lbm = null;
		LBMContextAttributes cattr = null;
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
		logger = org.apache.log4j.Logger.getLogger("lbmtrreq");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		try 
		{
			cattr = new LBMContextAttributes();
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating context attributes: " + ex.toString());
			System.exit(1);	
		}

		if (cconffname != null)
		{
			try 
			{
				FileInputStream f = new FileInputStream(cconffname);
				cattr.load(f);
			}
			catch (IOException e)
			{
				System.err.println(e.toString());
				System.exit(1);
			}
		}
		if (cattr.size() > 0)
			cattr.list(System.out);
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

		try {
			ctx.requestTopicResolution(flags, interval, duration);
		}
		catch (LBMException ex)
		{
			System.err.println("Error sending TR request: " + ex.toString());
			System.exit(1);
		}

		/* Linger */
		if(linger > 0)
		{
			try 
			{
				Thread.sleep(1000*linger);
			}
			catch (InterruptedException ex)
			{
			}
		}
		ctx.close();
	}
}
