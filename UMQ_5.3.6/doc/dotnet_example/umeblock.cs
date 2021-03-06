﻿using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Net;
using System.Threading;
using System.Text;
using com.latencybusters.lbm;
using com.latencybusters.auxapi;

namespace LBMApplication
{
    public static class umeblock
    {
        [DllImport("Kernel32.dll")]
        public static extern int SetEnvironmentVariable(string name, string value);

        public static string purpose = "Purpose: Send messages on a single topic by using a blocking send call.";
        private static string usage = "Usage umeblock [options] topic\n"
                                    + "Avaible options\n"
                                    + "  -c filname   use LBM configuration file FILE\n"
                                    + "  -l num       send messages of NUM bytes\n"
                                    + "  -M num       send NUM messages\n"
                                    + "  -v           print additional info in verbose form\n";

        private static int msgs   = 10000000;
        private static int msglen = 10;
        private static string confname;
        static bool verbose = false;

        static void Main(string[] args)
        {
            int arglength = args.Length;
            LBMContextAttributes cattr;
            LBMSourceAttributes sattr;
            ulong bytes_sent = 0;
            byte []messages;
            UMEBlockSrc blksrc;
            UMESrcCb srccb;
            LBMTopic topic;
            LBMContext ctx;
            LBM lbm;
            int nargs = 0;
            int i;

            //Create LBM
            lbm = new LBM();
            lbm.setLogger(new LBMLogging(logger));
            for (i = 0; i < arglength; i++)
            {
				try
				{
					switch (args[i])
					{
						case "-c":
							confname = args[++i];
							nargs += 2;

							break;

						case "-l":
							msglen = Convert.ToInt32(args[++i]);
							nargs += 2;

							break;

						case "-M":
							msgs = Convert.ToInt32(args[++i]);
							nargs += 2;

							break;

						case "-v":
							verbose = true;
							nargs++;

							break;

						case "-h":
							print_help_exit(0);
							break;
					}
				}
				catch (Exception e) 
				{
					/* type conversion exception */
					System.Console.Error.WriteLine("umeblock: error\n" + e.Message);
					print_help_exit(1);
				}
            }

            if (arglength <= 0 || nargs == arglength)
            {
                /* An error occurred processing the command line - print help and exit */
				print_help_exit(1);
            }

            messages = new byte[msglen];

            //Load up the config file
            if (confname != null)
                LBM.setConfiguration(confname);

            //Create attributes
            cattr = new LBMContextAttributes();
            sattr = new LBMSourceAttributes();

            //Create context
            ctx = new LBMContext(cattr);

            //Create topic
            topic = ctx.allocTopic(args[i-1]);

            //Create block source callback
            srccb = new UMESrcCb(verbose);
            blksrc = new UMEBlockSrc();
            if (blksrc.createSource(ctx, topic, sattr, new LBMSourceEventCallback(srccb.onSourceEvent), null, null) == false)
            {
                Console.Write("Error creating blocking source!");
                System.Environment.Exit(1);
            }
            
            System.Threading.Thread.Sleep(1000);

            long start_time = System.DateTime.Now.Ticks;
            for (uint count = 0; count < msgs; count++)
            {
                try
                {
                    blksrc.send(messages, msglen, 0, null);
                    bytes_sent += (ulong)msglen;
                }
                catch (LBMException ex)
                {
                    Console.WriteLine("Error sending: " + ex.ToString());
                    System.Environment.Exit(1);
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.ToString());
                    System.Environment.Exit(1);
                }
            }

            long end_time = System.DateTime.Now.Ticks;
            double secs = (end_time - start_time) / 10000000.0;
            print_bw(secs, msgs, bytes_sent);

            Console.WriteLine("Lingering for 5 seconds...");
            System.Threading.Thread.Sleep(5000);

            blksrc.close();
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
            string level = string.Empty;

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

            System.Console.Out.WriteLine(System.DateTime.Now.ToString() + " [" + level + "]: " + message);
			System.Console.Out.Flush();
        }

        private static void print_bw(double sec, int msgs, ulong bytes)
        {
            double mps = 0;
            double bps = 0;
            double kscale = 1000;
            double mscale = 1000000;
            char mgscale = 'K';
            char bscale = 'K';

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
			System.Console.Out.Flush();
        }
    }

    public class UMESrcCb
    {
        bool _verbose = false;

        public UMESrcCb(bool v)
        {
            _verbose = v;
        }

        private void print(string msg)
        {
            print(msg, true);
        }

        private void print(string msg, bool newline)
        {
            if (!_verbose)
                return;

            if (newline)
                Console.Error.WriteLine(msg);
            else
                Console.Error.Write(msg);
        }

        public void onSourceEvent(Object arg, LBMSourceEvent sourceEvent)
        {
            switch (sourceEvent.type())
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

                    print("Stable EX ACK. UME store " + Convert.ToString(ack.storeIndex()) + ": " + Convert.ToString(ack.store()) + " message stable. SQN " + Convert.ToString(ack.sequenceNumber()) + ". Flags " + Convert.ToString(ack.flags()) + " ", false);

                    if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) != 0)
                      print("IA ", false);

                    if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_INTRAGROUP_STABLE) != 0)
                      print("IR ", false);

                    if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STABLE) != 0)
                      print("STABLE ", false);

                    if((ack.flags() & LBM.SRC_EVENT_UME_MESSAGE_STABLE_EX_FLAG_STORE) != 0)
                      print("STORE ", false);

                    print(" ");

                    break;

                case LBM.SRC_EVENT_SEQUENCE_NUMBER_INFO:
                    LBMSourceEventSequenceNumberInfo info = sourceEvent.sequenceNumberInfo();
                    print("Sequence number info. first: " + Convert.ToString(info.firstSequenceNumber()) + " last: " + Convert.ToString(info.lastSequenceNumber()));

                    break;

                case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED:
                    print("Reclaimed");
                    break;

                case LBM.SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
                    print("Reclaimed");
                    break;

                default:
                    print("callback event: " + Convert.ToString(sourceEvent.type()));
                    break;
            }
			System.Console.Out.Flush();
        }
    }
}
