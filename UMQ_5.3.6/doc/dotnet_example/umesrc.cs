using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Net;
using System.Threading;
using com.latencybusters.lbm;

namespace LBMApplication
{
    class umesrc
    {
        [DllImport("Kernel32.dll")]
        public static extern int SetEnvironmentVariable(string name, string value);

        private static int force_reclaim_total = 0;
        private static int msgs = 10000000;
        private static int stats_sec = 0;
        public static int flightsz = 0;
        public static int appsent = 0;
        public static int stablerecv = 0;
        public static int store_behaviour = LBM.SRC_TOPIC_ATTR_UME_STORE_BEHAVIOR_RR;
        private static int pause_ivl = 0;
        private static int msgs_per_ivl = 1;
        private static int msgs_per_sec = 0;
        public static bool compat10 = false;
        public static Semaphore flightlock;
        public static int flightlock_value = 0;
        public static uint last_clientd_sent = 0;
        public static uint last_clientd_stable = 0;
        public static int sleep_before_sending = 0;
        private static string purpose = "Purpose: Send messages on a single topic.";
        private static string usage =
            "Usage: umesrc [options] topic\n"
            + "Available options:\n"
            + "  -1 = act as a UME 1.2 and earlier source would act\n"
            + "  -c filename = read config parameters from filename\n"
            + "  -D Send Deregistration after 100 messages\n"
            + "  -f NUM = allow NUM unstabilized messages in flight (determines message rate)"
            + "  -h = help\n"
            + "  -I id = use Registration ID of id\n"
            + "  -j = turn on UME late join\n"
            + "  -l len = send messages of len bytes\n"
            + "  -L linger = linger for linger seconds before closing context\n"
            + "  -m NUM = send at NUM messages per second (trumped by -f)"
            + "  -M msgs = send msgs number of messages\n"
            + "  -N = display sequence number information source events\n"
            + "  -n = used non-blocking I/O\n"
            + "  -P msec = pause after each send msec milliseconds\n"
            + "  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
			+ "                     DATA bits per second, and set retransmit rate limit to\n"
			+ "                     RETR bits per second.  For both limits, the optional\n"
			+ "                     k, m, and g suffixes may be used.  For example,\n"
			+ "                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
            + "  -S ip:port = use UME store at the specified address and port\n"
            + "  -s sec = print stats every sec seconds\n"
            + "  -t storename = use UME store with name storename\n"
            + "  -v = verbose\n"
            + "\nMonitoring options:\n"
            + "  --monitor-ctx NUM = monitor context every NUM seconds\n"
            + "  --monitor-src NUM = monitor source every NUM seconds\n"
            + "  --monitor-transport TRANS = use monitor transport module TRANS\n"
            + "                              TRANS may be `lbm', `udp', or `lbmsnmp', " 
            + 								"default is `lbm'\n"
            + "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
            + "  --monitor-format FMT = use monitor format module FMT\n"
            + "                         FMT may be `csv'\n"
            + "  --monitor-format-opts OPTS = use OPTS as format module options\n"
            + "  --monitor-appid ID = use ID as application ID string\n"
            + "  --flight-size = See -f above"
            + "  --message-rate = See -m above"
            ;

		/* Command line options */
		public static string application_id = null;
		public static bool block = true;
		public static string conffname = null;
		public static bool done = false;
		public static bool latejoin = false;
		public static int linger = 5;
		public static int dereg = 0;
		public static bool monitor_context = false;
        public static int monitor_context_ivl = 0;
        public static int mon_format = LBMMonitor.FORMAT_CSV;
		public static string mon_format_options = null;
      	public static bool monitor_source = false;
      	public static int monitor_source_ivl = 0;
      	public static int mon_transport = LBMMonitor.TRANSPORT_LBM;
     	public static string mon_transport_options = null; 
		public static int msglen = 25;
      	public static char protocol = '\0';
      	public static string regid = null;
     	public static int retrans_rate = 0;
      	public static int send_rate = 0;
      	public static bool seqnum_info = false;
      	public static string storeip = null;
        public static string storename = null;
        public static string storeport = "4567";        
		public static int verbose = 0;
		public static string topic_name = null;
		
        static void Main(string[] args)
        {
        	ulong bytes_sent = 0;
            bool stability = false;
            LBMObjectRecycler objRec = new LBMObjectRecycler();
        
            if (System.Environment.GetEnvironmentVariable("LBM_LICENSE_FILENAME") == null
                && System.Environment.GetEnvironmentVariable("LBM_LICENSE_INFO") == null)
            {
                SetEnvironmentVariable("LBM_LICENSE_FILENAME", "lbm_license.txt");
            }
            LBM lbm = new LBM();
            lbm.setLogger(new LBMLogging(logger));

            process_cmdline(args);
            
            byte[] message = new byte[msglen];
            if (conffname != null)
            {
                LBM.setConfiguration(conffname);
            }
            LBMContextAttributes cattr = new LBMContextAttributes();
            cattr.setFromXml(cattr.getValue("context_name"));
            cattr.setObjectRecycler(objRec, null);
            LBMSourceAttributes sattr = new LBMSourceAttributes();
            sattr.setFromXml(cattr.getValue("context_name"), topic_name);
            sattr.setObjectRecycler(objRec, null);
			// Set UMP liveness detection callbacks
			cattr.setUMEReceiverLivenessNotificationCallbacks(UMERcvrLivenessCreationCb.onNewReceiver, UMERcvrLivenessDeletionCb.onReceiverDelete, null);
            if (send_rate != 0)
            {
                sattr.setValue("transport", "LBTR" + protocol);
                cattr.setValue("transport_lbtrm_data_rate_limit", 
                			   send_rate.ToString());
                cattr.setValue("transport_lbtrm_retransmit_rate_limit", 
                			   retrans_rate.ToString());
            }
            /* Set the command line store IP and port to the config */
            if (storeip != null)
            {
                if(compat10) {
                    sattr.setValue("ume_primary_store_address", storeip);
                    sattr.setValue("ume_primary_store_port", storeport);
                }
                else
                {
                    sattr.setValue("ume_store", storeip + ":" + storeport);
                }
            } 
            else if (storename != null) 
            {
				sattr.setValue("ume_store_name", storename);
			}

            /* Get the store address and port from the current config.
             * If the command line specified the address/port it will
             * have set the config above overriding the config file
             */
            if(compat10)
            {
                storeip = sattr.getValue("ume_primary_store_address");
            }
            else
            { 
                string store = sattr.getValue("ume_store");
                string[] storeParts = store.Split(":".ToCharArray());
                if (storeParts.Length > 1)
                {
                    storeip = storeParts[0];
                    storeport = storeParts[1];
                }
            }
           
            if (storeip == "0.0.0.0")
                storeip = null;
            else
            {
                if(compat10)
                    storeport = sattr.getValue("ume_primary_store_port");
            }

			if (storeip == null) {
				flightsz = 0;
			}
			
            /* Override the flightsz if a message rate is set */
            if (msgs_per_sec > 0) 
            	flightsz = 0;
            if (flightsz > 0)
            {
                /* Create the flight time semaphore */
                flightlock = new Semaphore(0, flightsz);
            }
            /* Calculate the approriate message rate */
            if (msgs_per_sec > 0)
                calc_rate_vals();

            System.Console.Out.WriteLine(msgs_per_sec + " msgs/sec -> " + msgs_per_ivl + 
            							 " msgs/ivl, " + pause_ivl + " msec ivl " + 
            							 flightsz + " inflight");

            if (latejoin)
                sattr.setValue("ume_late_join", "1");

            if (regid != null && compat10)
                sattr.setValue("ume_registration_id", regid);
            else if (regid != null)
            {
                System.Console.Out.WriteLine("WARNING: -I is deprecated when compat10 is " + 
                							 "not set. Will be ignored.");
            }
            print_ume_store_config(sattr);
            if (sattr.getValue("ume_late_join") == "1")
                System.Console.Out.WriteLine("Using UME Late Join.");
            else
                System.Console.Out.WriteLine("Not using UME Late Join.");
            if (sattr.getValue("ume_confirmed_delivery_notification") == "1")
            {
                System.Console.Out.WriteLine("Using Confirmed Delivery Notification.");
                if (verbose == 1)
                    System.Console.Out.WriteLine("Will display only confirmed delivery events.");
                else if (verbose > 1)
                    System.Console.Out.WriteLine("Will display confirmed delivery events " + 
                    							 "and message stability events.");
                else
                    System.Console.Out.WriteLine("Will not display events.");
            }
            else
                System.Console.Out.WriteLine("Not using UME Confirmed Delivery Notification.");
            if (sattr.getValue("ume_message_stability_notification") == "1")
            {
                Console.Out.Write("Using UME Message Stability Notification. ");
                if (verbose >= 1)
                    Console.Out.WriteLine("Will display message stability events. ");
                else
                    Console.Out.WriteLine(" Will not display events. ");
                stability = true;
            }
	    if (sattr.getValue("ume_message_stability_notification") == "2")
	    {
		Console.Out.Write("Using UME Message Stability Notification. ");
		if (verbose >= 1)
		    Console.Out.WriteLine("Will display message stability events. ");
		else
		    Console.Out.WriteLine("Will not display events. ");
		stability = true;
	    }
	    if (sattr.getValue("ume_message_stability_notification") == "3")
	    {
		Console.Out.Write("Using UME Message Stability Notification. ");
		if (verbose >= 1)
		    Console.Out.WriteLine("Will display message stability events. ");
		else
		    Console.Out.WriteLine("Will not display events. ");
		stability = true;
	    }
	    
            if (!stability && flightsz > 0)
            {
                Console.Out.WriteLine("Enabling message stability notification to control" + 
                					  " unstablized message backlog");
                sattr.setValue("ume_message_stability_notification", "1");
                stability = true;
            }
            else
                Console.Out.WriteLine("Not using UME Message Stability Notification.");
            regid = sattr.getValue("ume_registration_id");
            if (regid != "0")
                System.Console.Out.WriteLine("Using UME Registration ID of " + regid);
            LBMContext ctx = new LBMContext(cattr);
            LongObject cd = new LongObject();
            sattr.setMessageReclamationCallback(
            						new LBMMessageReclamationCallback(onMessageReclaim), cd);
            LBMTopic topic = ctx.allocTopic(topic_name, sattr);
            LBMSource src;
            UMESrcCB srccb = new UMESrcCB(verbose);
            src = ctx.createSource(topic, new LBMSourceEventCallback(srccb.onSourceEvent), 
            														 null, null);
            LBMSrcStatsTimer stats;
            if (stats_sec > 0)
            {
                stats = new LBMSrcStatsTimer(ctx, src, stats_sec * 1000, null, objRec);
            }
            LBMMonitorSource lbmmonsrc = null;
            if (monitor_context || monitor_source)
            {
                lbmmonsrc = new LBMMonitorSource(mon_format, mon_format_options, mon_transport,
                								 mon_transport_options);
                if (monitor_context)
                    lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
                else
                    lbmmonsrc.start(src, application_id, monitor_source_ivl);
            }
            System.Threading.Thread.Sleep(1000);
            System.Console.Out.WriteLine("Sending " + msgs + " messages of size "
                       					 + msglen + " bytes to topic [" + topic_name
                       					 + "]");
			System.Console.Out.Flush();
            long start_time = System.DateTime.Now.Ticks;
            bool regProblem = false;
            LBMSourceSendExInfo exinfo = new LBMSourceSendExInfo();
            for (uint count = 0; count < msgs; )
            {
				if (( dereg == 1) && (count == 100))
				{
					System.Console.Out.WriteLine("Sending DEREGISTRATION\n");
					src.umederegister();
                    dereg = 0;
				}
                for (int ivlcount = 0; ivlcount < msgs_per_ivl; ivlcount++)
                {
                    if (seqnum_info || stability)
                    {
                        exinfo.setClientObject(count + 1);
                        last_clientd_sent = (uint)count + 1;
                    }
                    try
                    {
                        int xflag = 0;

                        srccb.blocked = true;
                        if (seqnum_info)
                        {
                            exinfo.setFlags(LBM.SRC_SEND_EX_FLAG_SEQUENCE_NUMBER_INFO);
                        }
                        if (flightsz > 0)
                        {
                            flightlock_decrement();
                            if(flightlock_getvalue() <= 1)
                                xflag = LBM.MSG_FLUSH;
                            if (sleep_before_sending > 0)
                            {
                                /* If we just finished registration with
                                 * a store (or stores), let's sleep a bit
                                 * to allow topic resolution to take place. */
                                Thread.Sleep(sleep_before_sending);
                                sleep_before_sending = 0;
                            }
                        }
                        umesrc.appsent++;
                        src.send(message, msglen,
                                (block ? 0 : LBM.SRC_NONBLOCK) | xflag, exinfo);
                        srccb.blocked = false;
                        count++;
                    }
                    catch (LBMEWouldBlockException)
                    {
                        while (srccb.blocked)
                            System.Threading.Thread.Sleep(100);
                        continue;
                    }
                    catch (UMENoRegException e)
                    {
			if(verbose > 0)
			{
				System.Console.Out.WriteLine("UMENoRegException: "+e.Message);
			}
                        if (!regProblem)
                        {
                            regProblem = true;
                            System.Console.Out.WriteLine("Send unsuccessful. Waiting...");
                        }
                        System.Threading.Thread.Sleep(1000);
                        umesrc.appsent--;
                        continue;
                    }
                    catch (UMENoQueueException e)
                    {
			if(verbose > 0)
			{
				System.Console.Out.WriteLine("UMENoQueueException: "+e.Message);
			}
                        if (!regProblem)
                        {
                            regProblem = true;
                            System.Console.Out.WriteLine("Send unsuccessful. Waiting...");
                        }
                        System.Threading.Thread.Sleep(1000);
                        umesrc.appsent--;
                        continue;
		    }
                    catch (UMENoStoreException e)
                    {
			if(verbose > 0)
			{
				System.Console.Out.WriteLine("UMENoStoreException: "+e.Message);
			}
                        if (!regProblem)
                        {
                            regProblem = true;
                            System.Console.Out.WriteLine("Send unsuccessful. Waiting...");
                        }
                        System.Threading.Thread.Sleep(1000);
                        umesrc.appsent--;
                        continue;
		    }
                    if (regProblem)
                    {
                        regProblem = false;
                        System.Console.Out.WriteLine("Send OK. Continuing.");
                    }
                    bytes_sent += (ulong) msglen;
                }
                if (pause_ivl > 0)
                {
                    System.Threading.Thread.Sleep(pause_ivl);
                }
            }
            long end_time = System.DateTime.Now.Ticks;
            double secs = (end_time - start_time) / 10000000.0;
            System.Console.Out.WriteLine("Sent " + msgs
                       				  + " messages of size " + msglen
                       				  + " bytes in " + secs + " seconds.");
            print_bw(secs, msgs, bytes_sent);
			System.Console.Out.Flush();
            if (linger > 0)
            {
                System.Console.Out.WriteLine("Lingering for " + linger + " seconds...");
                System.Threading.Thread.Sleep(linger * 1000);
            }
            stats = new LBMSrcStatsTimer(ctx, src, 0, null, objRec);
            objRec.close();
            src.close();
            ctx.close();
            cd.done();
        }
        
        private static void process_cmdline(String[] args)
        {
        	const string OPTION_MONITOR_APPID = "--monitor-appid";
			const string OPTION_MONITOR_CTX = "--monitor-ctx";
			const string OPTION_FLIGHT_SIZE = "--flight-size";
			const string OPTION_MESSAGE_RATE = "--message-rate";
			const string OPTION_MONITOR_FORMAT = "--monitor-format";
			const string OPTION_MONITOR_FORMAT_OPTS = "--monitor-format-opts";
			const string OPTION_MONITOR_SRC = "--monitor-src";
			const string OPTION_MONITOR_TRANSPORT = "--monitor-transport";
			const string OPTION_MONITOR_TRANSPORT_OPTS = "--monitor-transport-opts";
        
        	char[] delim;
        	bool error = false;
        	int i;
        	int n = args.Length;
        	string[] tokens;
            
            for (i = 0; i < n; i++)
            {
				try
				{
					switch (args[i])
					{
						case OPTION_MONITOR_APPID:
							if (++i >= n)
							{
								error = true;
								break;
							}
							application_id = args[i];
							break;

						case OPTION_MONITOR_CTX:
							if (++i >= n)
							{
								error = true;
								break;
							}
							monitor_context = true;
							monitor_context_ivl = Convert.ToInt32(args[i]);
							break;

						case OPTION_MONITOR_SRC:
							if (++i >= n)
							{
								error = true;
								break;
							}
							monitor_source = true;
							monitor_source_ivl = Convert.ToInt32(args[i]);
							break;

						case OPTION_MONITOR_FORMAT:
							if (++i >= n)
							{
								error = true;
								break;
							}
							if (args[i].ToLower().CompareTo("csv") == 0)
								mon_format = LBMMonitor.FORMAT_CSV;
							else
							{
								error = true;
								break;
							}
							break;

						case OPTION_MONITOR_TRANSPORT:
							if (++i >= n)
							{
								error = true;
								break;
							}
							if (args[i].ToLower().CompareTo("lbm") == 0)
								mon_transport = LBMMonitor.TRANSPORT_LBM;
							else if (args[i].ToLower().CompareTo("udp") == 0)
								mon_transport = LBMMonitor.TRANSPORT_UDP;
							else if (args[i].ToLower().CompareTo("lbmsnmp") == 0)
								mon_transport = LBMMonitor.TRANSPORT_LBMSNMP;
							else
							{
								error = true;
								break;
							}
							break;

						case OPTION_MONITOR_TRANSPORT_OPTS:
							if (++i >= n)
							{
								error = true;
								break;
							}
							mon_transport_options += args[i];
							break;

						case OPTION_MONITOR_FORMAT_OPTS:
							if (++i >= n)
							{
								error = true;
								break;
							}
							mon_format_options += args[i];
							break;

						case "-1":
							compat10 = true;
							break;

						case "-c":
							if (++i >= n)
							{
								error = true;
								break;
							}
							conffname = args[i];
							break;
						case "-D":
							dereg = 1;
							break;


						case OPTION_FLIGHT_SIZE:
						case "-f":
							if (++i >= n)
							{
								error = true;
								break;
							}
							flightsz = Convert.ToInt32(args[i]);
							break;

						case "-h":
							print_help_exit(0);
							break;
						case "-I":
							if (++i >= n)
							{
								error = true;
								break;
							}
							regid = args[i];
							if (!compat10)
							{
								System.Console.Out.WriteLine("WARNING: -I is deprecated " + 
												"when compat10 is not set. Will be ignored.");
							}
							break;

						case "-j":
							latejoin = true;
							break;

						case "-l":
							if (++i >= n)
							{
								error = true;
								break;
							}
							msglen = Convert.ToInt32(args[i]);
							break;

						case "-n":
							block = false;
							break;

						case "-N":
							seqnum_info = true;
							break;

						case "-L":
							if (++i >= n)
							{
								error = true;
								break;
							}
							linger = Convert.ToInt32(args[i]);
							break;

						case OPTION_MESSAGE_RATE:
						case "-m":
							if (++i >= n)
							{
								error = true;
								break;
							}
							msgs_per_sec = Convert.ToInt32(args[i]);
							break;

						case "-M":
							if (++i >= n)
							{
								error = true;
								break;
							}
							msgs = Convert.ToInt32(args[i]);
							break;

						case "-P":
							if (++i >= n)
							{
								error = true;
								break;
							}
							pause_ivl = Convert.ToInt32(args[i]);
							break;

						case "-R":
							if (++i >= n)
							{
								error = true;
								break;
							}
							ParseRateVars parseRateVars = lbmExampleUtil.parseRate(args[i]);
							if (parseRateVars.error)
							{
								print_help_exit(1);
							}
							send_rate = parseRateVars.rate;
							retrans_rate = parseRateVars.retrans;
							protocol = parseRateVars.protocol;
							break;

						case "-S":
							if (++i >= n)
							{
								error = true;
								break;
							}
							delim = ":".ToCharArray();
							tokens = args[i].Split(delim);
							if (tokens.Length > 2)
							{
								print_help_exit(1);
							}
							storeip = tokens[0];
							IPHostEntry he = Dns.Resolve(storeip);
							if (he.AddressList.Length > 0)
							{
								storeip = he.AddressList[0].ToString();
							}
							if (tokens.Length == 2)
							{
								storeport = tokens[1];
							}
							break;

						case "-s":
							if (++i >= n)
							{
								error = true;
								break;
							}
							stats_sec = Convert.ToInt32(args[i]);
							break;
						
						case "-t":
							if (++i >= n)
							{
								error = true;
								break;
							}
							storename = args[i];
							break;
							
						case "-v":
							verbose++;
							break;

						default:
							if (args[i].StartsWith("-"))
							{
								error = true;
							}
							else
							{
								done = true;
							}
							break;
					}
					if (error || done)
						break;
				}
				catch (Exception e) 
				{
					/* type conversion exception */
					System.Console.Error.WriteLine("umesrc: error\n" + e.Message);
					print_help_exit(1);
				}
            }
            if (error || i >= n)
            {
                /* An error occurred processing the command line - print help and exit */
				print_help_exit(1);
            }
            
            topic_name = args[i];
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
            System.Console.Out.WriteLine(System.DateTime.Now.ToString() + " [" + 
            							 level + "]: " + message);
			System.Console.Out.Flush();
		}

        /* Semaphore methods for flight size semaphore. */

        public static void flightlock_decrement()
        {
            flightlock.WaitOne();
            Interlocked.Decrement(ref flightlock_value);
        }

        public static void flightlock_increment()
        {
            flightlock.Release();
            Interlocked.Increment(ref flightlock_value);
        }

        public static int flightlock_getvalue()
        {
            return flightlock_value;
        }

        /*
         * Function that determines how to pace sending of messages to obtain a given
         * rate.  Given messages per second, calculates number of messages to send in 
         * a particular interval and the number of milliseconds to pause between 
         * intervals.
         */
        private static void calc_rate_vals()
        {
            int intervals_per_sec = 1000;

            pause_ivl = 20;
            intervals_per_sec = 1000 / (pause_ivl);

            while (pause_ivl <= 1000 && msgs_per_sec % intervals_per_sec != 0)
            {
                pause_ivl++;
                while (((1000 % pause_ivl) != 0) && pause_ivl <= 1000)
                    pause_ivl++;
                intervals_per_sec = 1000 / pause_ivl;
            }
            msgs_per_ivl = msgs_per_sec / intervals_per_sec;
        }

        private static void print_bw(double sec, int msgs, ulong bytes)
        {
            double mps = 0;
            double bps = 0;
            double kscale = 1000;
            double mscale = 1000000;
            char mgscale = 'K';
            char bscale = 'K';

			if (sec == 0) return; /* avoid division by zero */
            mps = msgs / sec;
            bps = bytes * 8 / sec;
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
            System.Console.Out.WriteLine(sec
                       + " secs. "
                       + mps.ToString("0.000")
                       + " " + mgscale + "msgs/sec. "
                       + bps.ToString("0.000")
                       + " " + bscale + "bps");
        }

        private static void print_ume_store_config(LBMSourceAttributes sattr)
        {
        
            if (compat10)
            {
                string addr;
                string port;

                addr = sattr.getValue("ume_primary_store_address");
                port = sattr.getValue("ume_primary_store_port");
                if (addr == "0.0.0.0")
                    return;
                System.Console.Out.WriteLine("Primary UME store: " +
                        addr + ":" + port);

                addr = sattr.getValue("ume_secondary_store_address");
                port = sattr.getValue("ume_secondary_store_port");
                if (addr == "0.0.0.0")
                    return;
                System.Console.Out.WriteLine("Secondary UME store: " +
                        addr + ":" + port);

                addr = sattr.getValue("ume_tertiary_store_address");
                port = sattr.getValue("ume_tertiary_store_port");
                if (addr == "0.0.0.0")
                    return;
                System.Console.Out.WriteLine("Tertiary UME store: " +
                        addr + ":" + port);
            }
            else
            {
                UMEStoreEntry[] stores = sattr.getStores();
                UMEStoreGroupEntry[] groups = sattr.getStoreGroups();
                IPEndPoint addr = null;
                try
                {
                    string storeBehavior = sattr.getValue("ume_store_behavior");

                    if (storeBehavior == "quorum-consensus")
                    {
						umesrc.store_behaviour = LBM.SRC_TOPIC_ATTR_UME_STORE_BEHAVIOR_QC;

                        for (int j = 0; j < groups.Length; j++)
                        {
                            System.Console.Out.WriteLine("Group " + j + ": Size " + 
                            							 groups[j].groupSize());
                            for (int i = 0; i < stores.Length; i++)
                            {
                                if (stores[i].groupIndex() == j)
                                {
                                    addr = stores[i].address();
                                    System.Console.Out.Write(" Store " + i + " " + 
                                    						 addr.ToString() + " ");
                                    if (stores[i].registrationId() != 0)
                                    {
                                        System.Console.Out.Write("RegID " + 
                                        						 stores[i].registrationId());
                                    }
                                    System.Console.Out.WriteLine();
                                }
                            }
                        }
                    }
                    else
                    {
					/* umesrc defaults to RR */
                        for (int i = 0; i < stores.Length; i++)
                        {
                            if (stores[i].groupIndex() == 0)
                            {
                                addr = stores[i].address();
                                System.Console.Out.Write("Store " + i + ": " + 
                                						 addr.ToString() + " ");
                                if (stores[i].registrationId() != 0)
                                {
                                    System.Console.Out.Write("RegID " + stores[i].registrationId());
                                }
                                System.Console.Out.WriteLine();
                            }
                        }
                    }
					System.Console.Out.Flush();
                }
                catch (LBMException ex)
                {
                    System.Console.Error.WriteLine("Error getting source attributes: " +
                    							   ex.Message);
                }
            }

        }

        private static void onMessageReclaim(object clientd, string topic, long sqn)
        {
            LongObject t = (LongObject)clientd;
            if (t == null)
            {
                System.Console.Error.WriteLine("WARNING: source for topic \"" + topic + 
                							   "\" forced reclaim 0x" + sqn.ToString("x"));
            }
            else
            {
                long endt = System.DateTime.Now.Ticks / 10000000;
                endt -= t.value;
                force_reclaim_total++;
                if (endt > 5)
                {
                    System.Console.Error.WriteLine("WARNING: source for topic \"" + topic + 
                    						"\" forced_reclaim. Total " + force_reclaim_total);
                    t.value = System.DateTime.Now.Ticks / 10000000;
                }
            }
        }

    }
	
	// Handle UMP liveness receiver detection 
	class UMERcvrLivenessCreationCb
    {
        public static Object onNewReceiver(UMEReceiverLivenessCallbackInfo info, Object cbArg)
        {
            Object source_clientd = null;
            System.Console.Out.WriteLine("Receiver detected: regid " + info.getUserRcvRegId() + ", session_id " + info.getSessionId());
			System.Console.Out.Flush();
            return source_clientd;
        }
    }
	
	// Handle UMP liveness receiver lost 
	class UMERcvrLivenessDeletionCb
    {
        public static int onReceiverDelete(UMEReceiverLivenessCallbackInfo info, Object cbArg, Object sourceCbArg)
        {
            System.Console.Out.Write("Receiver declared dead: regid " + info.getUserRcvRegId() + ", session_id " + info.getSessionId() + ", reason ");
            if ((info.getFlags() & LBM.LBM_UME_LIVENESS_RECEIVER_UNRESPONSIVE_FLAG_EOF) != 0)
            {
                System.Console.Out.WriteLine("EOF");
            }
            else if ((info.getFlags() & LBM.LBM_UME_LIVENESS_RECEIVER_UNRESPONSIVE_FLAG_TMO) != 0)
            {
                System.Console.Out.WriteLine("TIMEOUT");
            }
			System.Console.Out.Flush();
            return 0;
        }
    }

    class LongObject
    {
        public long value = 0;

        public void done()
        {
        }
    }

    class UMESrcCB
    {
        public bool blocked = false;
        private int _verbose;

        public UMESrcCB(int verbose)
        {
            _verbose = verbose;
        }

        public void onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
        {
            int i, semval;
            uint count;
            switch (sourceEvent.type())
            {
                case LBM.SRC_EVENT_CONNECT:
                    System.Console.Out.WriteLine("Receiver connect " + sourceEvent.dataString());
                    break;
                case LBM.SRC_EVENT_DISCONNECT:
                    System.Console.Out.WriteLine("Receiver disconnect " + sourceEvent.dataString());
                    break;
                case LBM.SRC_EVENT_WAKEUP:
                    blocked = false;
                    break;
                case LBM.SRC_EVENT_UME_REGISTRATION_ERROR:
                    System.Console.Out.WriteLine("Error registering source with UME store: "
                               + sourceEvent.dataString());
                    break;
                case LBM.SRC_EVENT_UME_DEREGISTRATION_SUCCESS_EX:
					System.Console.Out.WriteLine("Received SRC_EVENT_UME_DEREGISTRATION_SUCCESS_EX\n");
					break;
				
                case LBM.SRC_EVENT_UME_DEREGISTRATION_COMPLETE_EX:
					System.Console.Out.WriteLine("Received SRC_EVENT_UME_DEREGISTRATION_COMPLETE_EX\n");
					break;
					
                case LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS:
                    if (umesrc.compat10 && umesrc.flightsz > 0)
                    {
                        umesrc.sleep_before_sending = 1000;
                        semval = umesrc.flightlock_getvalue();
                        for (i = (int)(umesrc.flightsz - semval - (umesrc.last_clientd_sent - umesrc.last_clientd_stable)); i > 0; i--)
                        {
                            umesrc.flightlock_increment();
                        }
                    }

                    System.Console.Out.WriteLine("UME store registration success. RegID "
                               + sourceEvent.registrationId());
                    break;
                case LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
                    UMESourceEventRegistrationSuccessInfo reg = 
                    										sourceEvent.registrationSuccessInfo();
                    System.Console.Out.Write("UME store " + reg.storeIndex() + ": " + reg.store()
                            + " registration success. RegID " + reg.registrationId() + ". Flags "
                            + reg.flags() + " ");
                    if (((reg.flags() & LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS_EX_FLAG_OLD)) != 0)
                    {
                        System.Console.Out.Write("OLD[SQN " + reg.sequenceNumber() + "] ");
                    }
                    if (((reg.flags() & LBM.SRC_EVENT_UME_REGISTRATION_SUCCESS_EX_FLAG_NOACKS)) != 0)
                    {
                        System.Console.Out.Write("NOACKS ");
                    }
                    System.Console.Out.WriteLine();
                    break;
                case LBM.SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
                    UMESourceEventRegistrationCompleteInfo regcomp =
                    									 sourceEvent.registrationCompleteInfo();

                    umesrc.sleep_before_sending = 1000;

                    if (umesrc.flightsz > 0)
                    {
                        semval = umesrc.flightlock_getvalue();
                        for (i = (int)(umesrc.flightsz - semval - (umesrc.last_clientd_sent - umesrc.last_clientd_stable)); i > 0; i--)
                        {
                            umesrc.flightlock_increment();
                        }
                    }

                    System.Console.Out.Write("UME registration complete. SQN " +
                    						 regcomp.sequenceNumber() + ". Flags " + 
                    						 regcomp.flags() + " ");
                    if ((regcomp.flags() & LBM.SRC_EVENT_UME_REGISTRATION_COMPLETE_EX_FLAG_QUORUM) != 0)
                    {
                        System.Console.Out.Write("QUORUM ");
                    }
                    System.Console.Out.WriteLine();
                    break;
                case LBM.SRC_EVENT_UME_MESSAGE_STABLE:
                    count = (uint)sourceEvent.clientObject();

                    if (_verbose >= 2)
                        System.Console.Out.WriteLine("UME message stable - sequence number "
                                       + sourceEvent.sequenceNumber().ToString("x")
                                       + " (cd "
                                       + ((int)sourceEvent.clientObject()).ToString("x")
                                       + ")");

                    umesrc.stablerecv++;

                    if (umesrc.flightsz > 0)
                    {
                        semval = umesrc.flightlock_getvalue();
                        for (i = ((int)(count - umesrc.last_clientd_stable)) > (umesrc.flightsz - semval) ? (umesrc.flightsz - semval) : ((int)(count - umesrc.last_clientd_stable)); i > 0; i--)
                        {
                            umesrc.flightlock_increment();
                        }
                        umesrc.last_clientd_stable = count;
                    }

                    break;
                case LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX:
                    UMESourceEventAckInfo staInfo = sourceEvent.ackInfo();
                    if (_verbose >= 2)
                    {
                        System.Console.Out.Write("UME store " + staInfo.storeIndex() + ": "
                                + staInfo.store() + " message stable. SQN " 
                                + staInfo.sequenceNumber()
                                + " (cd " + ((uint)staInfo.clientObject()).ToString("x") 
                                + "). Flags " + staInfo.flags() + " ");
                        if ((staInfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) != 0)
                        {
                            System.Console.Out.Write("IA ");
                        }
                        if ((staInfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTERGROUP_STABLE) != 0) {
                            System.Console.Out.Write("IR ");
                        }
                        if ((staInfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE) != 0)
                        {
                            System.Console.Out.Write("STABLE ");
                        }
                        if ((staInfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STORE) != 0)
                        {
                            System.Console.Out.Write("STORE ");
                        }
                        System.Console.Out.WriteLine();

                    }

                    if(umesrc.store_behaviour == LBM.SRC_TOPIC_ATTR_UME_STORE_BEHAVIOR_RR ||
                        ((staInfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE) == LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE)) {

                        /* Peg the counter for the received stable message */
                        umesrc.stablerecv++;



                        count = (uint)(staInfo.clientObject());
                        if (umesrc.flightsz > 0)
                        {
                            semval = umesrc.flightlock_getvalue();
                            for (i = ((int)(count - umesrc.last_clientd_stable)) > (umesrc.flightsz - semval) ? (umesrc.flightsz - semval) : ((int)(count - umesrc.last_clientd_stable)); i > 0; i--)
                            {
                                umesrc.flightlock_increment();
                            }
                            umesrc.last_clientd_stable = count;
                        }
                    }


                    break;
                case LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION:
                    if (_verbose > 0)
                        System.Console.Out.WriteLine("UME delivery confirmation - sequence number "
                                       + sourceEvent.sequenceNumber().ToString("x")
                                       + " Rcv RegID "
                                       + sourceEvent.registrationId()
                                       + " (cd "
                                       + ((int)sourceEvent.clientObject()).ToString("x")
                                       + ")");
                    break;
                case LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX:
                    UMESourceEventAckInfo cdelvinfo = sourceEvent.ackInfo();
                    if (_verbose > 0)
                    {
                        System.Console.Out.Write("UME delivery confirmation. SQN " +
                        						 cdelvinfo.sequenceNumber() + ", RcvRegID " +
                        						 cdelvinfo.receiverRegistrationId() + " (cd " +
                                				 ((uint)cdelvinfo.clientObject()).ToString("x") + 
                                				 "). Flags " + cdelvinfo.flags() + " ");
                        if ((cdelvinfo.flags() & LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX_FLAG_UNIQUEACKS) != 0)
                        {
                            System.Console.Out.Write("UNIQUEACKS ");
                        }
                        if ((cdelvinfo.flags() & LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX_FLAG_UREGID) != 0)
                        {
                            System.Console.Out.Write("UREGID ");
                        }
                        if ((cdelvinfo.flags() & LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX_FLAG_OOD) != 0)
                        {
                            System.Console.Out.Write("OOD ");
                        }
                        if ((cdelvinfo.flags() & LBM.SRC_EVENT_UME_DELIVERY_CONFIRMATION_EX_FLAG_EXACK) != 0)
                        {
                            System.Console.Out.Write("EXACK ");
                        }
                        System.Console.Out.WriteLine();
                    }
                    break;
                case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED:
                    if (_verbose > 0)
                        System.Console.Out.WriteLine("UME message reclaimed - sequence number "
                                       + sourceEvent.sequenceNumber().ToString("x")
                                       + " (cd "
                                       + ((uint)sourceEvent.clientObject()).ToString("x")
                                       + ")");
                    break;
				case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
					UMESourceEventAckInfo reclaiminfo = sourceEvent.ackInfo();
					if (_verbose > 0) {
						System.Console.Out.Write("UME message reclaimed (ex) - sequence number "
								+ reclaiminfo.sequenceNumber()
								+ " (cd "
								+ ((uint)reclaiminfo.clientObject()).ToString("x")
								+ "). Flags "
								+ reclaiminfo.flags());
						if ((reclaiminfo.flags() & LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED_EX_FLAG_FORCED) != 0) {
							System.Console.Out.Write(" FORCED");
						}
						System.Console.Out.WriteLine();
					}
					break;
                case LBM.SRC_EVENT_UME_STORE_UNRESPONSIVE:
                    System.Console.Out.WriteLine("UME store: "
                               + sourceEvent.dataString());
                    break;

                case LBM.SRC_EVENT_SEQUENCE_NUMBER_INFO:
                    LBMSourceEventSequenceNumberInfo info = sourceEvent.sequenceNumberInfo();
                    if (info.firstSequenceNumber() != info.lastSequenceNumber())
                    {
                        System.Console.Out.WriteLine("SQN [" + info.firstSequenceNumber()
                                + "," + info.lastSequenceNumber() + "] (cd "
                                + ((uint)info.clientObject()).ToString("x") + ")");
                    }
                    else
                    {
                        System.Console.Out.WriteLine("SQN " + info.lastSequenceNumber()
                                + " (cd " + ((uint)info.clientObject()).ToString("x") + ")");
                    }
                    break;

                case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION:
                    if (_verbose > 0)
                    {
                        LBMSourceEventFlightSizeNotification note = sourceEvent.flightSizeNotification();
                        System.Console.Out.Write("Flight Size Notification. Type ");
                        switch (note.type()) {
                            case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UME:
                                System.Console.Out.Write("UME");
                                break;
                            case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_ULB:
                                System.Console.Out.Write("ULB");
                                break;
                            case LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UMQ:
                                System.Console.Out.Write("UMQ");
                                break;
                            default:
                                System.Console.Out.Write("unknown");
                                break;
                        }
                        System.Console.Out.WriteLine(". Inflight is "
                            + (note.state() == LBM.SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_STATE_OVER ? "OVER" : "UNDER")
                            + " specified flight size");
                    }
                    break;

                default:
                    System.Console.Out.WriteLine("Unknown source event "
                               + sourceEvent.type());
                    break;
            }
			System.Console.Out.Flush();
            sourceEvent.dispose();
        }
    }

    class LBMSrcStatsTimer : LBMTimer
    {
        private LBMSource _src;
        private bool _done = false;
        private long _tmo;
        private LBMObjectRecyclerBase _recycler;

        public LBMSrcStatsTimer(LBMContext ctx, LBMSource src, long tmo, LBMEventQueue evq, LBMObjectRecyclerBase recycler)
            : base( ctx, tmo, evq)
        {
            _recycler = recycler;
            _src = src;
            _tmo = tmo;
            if (tmo == 0)
                print_stats();
            else
                this.addTimerCallback(new LBMTimerCallback(onExpiration));
        }

        public void done()
        {
            _done = true;
        }

        private void onExpiration(object arg)
        {
            print_stats();
            if (!_done)
            {
                this.reschedule(_tmo);
            }
        }

        private void print_stats()
        {
            LBMSourceStatistics stats = _src.getStatistics();

            switch (stats.type())
            {
                case LBM.TRANSPORT_STAT_TCP:
                    System.Console.Out.WriteLine("TCP, buffered "
                               + stats.bytesBuffered()
                               + ", clients "
                               + stats.numberOfClients()
                               + ", app sent "
                               + umesrc.appsent
                               + ", stable "
                               + umesrc.stablerecv
                               + ", inflight "
                               + (umesrc.stablerecv > umesrc.appsent ?
                                       umesrc.stablerecv - umesrc.appsent :
                                       umesrc.appsent - umesrc.stablerecv));

                    break;
                case LBM.TRANSPORT_STAT_LBTRU:
                    System.Console.Out.WriteLine("LBT-RU, sent "
                               + stats.messagesSent()
                               + "/"
                               + stats.bytesSent()
                               + ", naks "
                               + stats.naksReceived()
                               + "/"
                               + stats.nakPacketsReceived()
                               + ", ignored "
                               + stats.naksIgnored()
                               + "/"
                               + stats.naksIgnoredRetransmitDelay()
                               + ", shed "
                               + stats.naksShed()
                               + ", rxs "
                               + stats.retransmissionsSent()
                               + ", clients "
                               + stats.numberOfClients()
                               + ", app sent "
                               + umesrc.appsent
                               + ", stable "
                               + umesrc.stablerecv
                               + ", inflight "
                               + (umesrc.stablerecv > umesrc.appsent ?
                                       umesrc.stablerecv - umesrc.appsent :
                                       umesrc.appsent - umesrc.stablerecv));
                    break;
                case LBM.TRANSPORT_STAT_LBTRM:
                    System.Console.Out.WriteLine("LBT-RM, sent "
                               + stats.messagesSent()
                               + "/"
                               + stats.bytesSent()
                               + ", txw "
                               + stats.transmissionWindowMessages()
                               + "/"
                               + stats.transmissionWindowBytes()
                               + ", naks "
                               + stats.naksReceived()
                               + "/"
                               + stats.nakPacketsReceived()
                               + ", ignored "
                               + stats.naksIgnored()
                               + "/"
                               + stats.naksIgnoredRetransmitDelay()
                               + ", shed "
                               + stats.naksShed()
                               + ", rxs "
                               + stats.retransmissionsSent()
                               + ", rctl "
                               + stats.messagesQueued()
                               + "/"
                               + stats.retransmissionsQueued()
                               + ", app sent "
                               + umesrc.appsent
                               + ", stable "
                               + umesrc.stablerecv
                               + ", inflight "
                               + (umesrc.stablerecv > umesrc.appsent ?
                                       umesrc.stablerecv - umesrc.appsent :
                                       umesrc.appsent - umesrc.stablerecv));
                    break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.Console.Out.WriteLine("LBT-IPC, source " + stats.source()
						+ " clients "
						+ stats.numberOfClients()
						+ ", sent "
						+ stats.messagesSent()
						+ "/"
						+ stats.bytesSent()
						+ ", app sent "
						+ umesrc.appsent
						+ ", stable "
						+ umesrc.stablerecv
						+ ", inflight "
						+ (umesrc.stablerecv > umesrc.appsent ?
						   umesrc.stablerecv - umesrc.appsent :
						   umesrc.appsent - umesrc.stablerecv));
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.Console.Out.WriteLine("LBT-RDMA, source " + stats.source()
						+ " clients "
						+ stats.numberOfClients()
						+ ", sent "
						+ stats.messagesSent()
						+ "/"
						+ stats.bytesSent()
						+ ", app sent "
						+ umesrc.appsent
						+ ", stable "
						+ umesrc.stablerecv
						+ ", inflight "
						+ (umesrc.stablerecv > umesrc.appsent ?
						   umesrc.stablerecv - umesrc.appsent :
						   umesrc.appsent - umesrc.stablerecv));
					break;
            }
			System.Console.Out.Flush();
            _recycler.doneWithSourceStatistics(stats);
        }
    }
}
