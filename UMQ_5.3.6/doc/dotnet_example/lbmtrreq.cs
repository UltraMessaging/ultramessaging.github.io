using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Threading;
using com.latencybusters.lbm;

namespace LBMApplication
{
	class lbmtrreq
	{

		private static uint duration = 0;
		private static uint interval = 0;
		private static ushort flags = 0;
		private static int linger = 0;
		

		private static string purpose = "Purpose: Request topic resolution for quiescent components.";
		private static string usage =
			"Usage: lbmtrreq [options]\n"
			+ "Available options:\n"
			+ "  -c filename =      Use LBM configuration file filename.\n"
			+ "                     Multiple config files are allowed.\n"
			+ "                     Example:  '-c file1.cfg -c file2.cfg'\n"
			+ "  -a, --adverts      Request Advertisements\n"
			+ "  -q, --queries      Request Queries\n"
			+ "  -w, --wildcard     Request Wildcard Queries\n"
			+ "  -i, --interval=NUM Interval between requests (milliseconds)\n"
			+ "  -d, --duration=NUM Minimum duration of requests (seconds)\n"
			+ "  -L, --linger=NUM   Linger for NUM seconds before closing context\n"
			;

		static void Main(string[] args)
		{
			LBM lbm = new LBM();
			lbm.setLogger(new LBMLogging(logger));
			
			int i;
			int n = args.Length;

			bool error = false;
			bool done = false;
			for (i = 0; i < n; i++)
			{
				try
				{
					switch (args[i])
					{
						case "-a":
							flags |= LBM.TOPIC_RES_REQUEST_ADVERTISEMENT;
							break;
						case "-c":
							if (++i >= n)
							{
								error = true;
								break;
							}						
							try 
							{
								LBM.setConfiguration(args[i]);
							}
							catch (LBMException Ex)
							{
								System.Console.Error.WriteLine("lbmtrreq error: {0}", Ex.Message);
								error = true;
							}
							break;
						case "-d":
							if (++i >= n)
							{
								error = true;
								break;
							}
							duration = UInt32.Parse(args[i]);
							break;
						case "-h":
							print_help_exit(0);
							break;
						case "-i":
							if (++i >= n)
							{
								error = true;
								break;
							}
							interval = UInt32.Parse(args[i]);
							break;
						case "-L":
							if (++i >= n)
							{
								error = true;
								break;
							}
							linger = Int32.Parse(args[i]);
							break;
						case "-q":
							flags |= LBM.TOPIC_RES_REQUEST_QUERY;
							break;
						case "-w":
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
					System.Console.Error.WriteLine("lbmtrreq: error\n{0}\n", e.Message);
					print_help_exit(1);
				}
			}
			if (error)
			{
				/* An error occurred processing the command line - print help and exit */
				print_help_exit(1);
			}

			LBMContext ctx = new LBMContext();

			ctx.requestTopicResolution(flags, interval, duration);

			if(linger > 0) {
				System.Console.WriteLine("Lingering for {0} seconds.", linger);
				Thread.Sleep(1000 * linger);
			}
				
			ctx.close();
		}
		
		private static void print_help_exit(int exit_value)
		{
			System.Console.Error.WriteLine(LBM.version());
			System.Console.Error.WriteLine(purpose);
			System.Console.Error.WriteLine(usage);
			System.Environment.Exit(exit_value);
		}

		private static void logger(int loglevel, string message)
		{
			string level;			
			switch (loglevel)
			{
				case LBM.LOG_ALERT: level = "Alert"; break;
				case LBM.LOG_CRIT: level = "Critical"; break;
				case LBM.LOG_DEBUG: level = "Debug"; break;
				case LBM.LOG_EMERG: level = "Emergency"; break;
				case LBM.LOG_ERR: level = "Error"; break;
				case LBM.LOG_INFO: level = "Info"; break;
				case LBM.LOG_NOTICE: level = "Note"; break;
				case LBM.LOG_WARNING: level = "Warning"; break;
				default: level = "Unknown"; break;
			}
			System.Console.Out.WriteLine("{0} [{1}]: {2}", System.DateTime.Now, level, message);
			System.Console.Out.Flush();
		}

	}
		
}
