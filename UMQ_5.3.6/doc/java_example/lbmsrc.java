import com.latencybusters.lbm.*;
import com.latencybusters.lbm.sdm.*;
import java.util.*;
import java.text.NumberFormat;
import java.math.BigInteger;
// See https://communities.informatica.com/infakb/faq/5/Pages/80008.aspx
import org.openmdx.uses.gnu.getopt.*;
import verifiablemsg.*;

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
import java.nio.ByteBuffer;

class lbmsrc
{
	private static String pcid = "";
	private static long msgs = 10000000;
	private static int stats_sec = 0;
	private static boolean verbose = false;
	private static boolean sdm = false;
	private static boolean sequential = true;
	private static boolean verifiable = false;
	private static String purpose = "Purpose: Send messages on a single topic.";
	private static String usage =
	  "Usage: lbmsrc [options] topic\n"
	+ "Available options:\n"
	+ "  -c filename = Use LBM configuration file filename.\n"
	+ "                Multiple config files are allowed.\n"
	+ "                Example:  '-c file1.cfg -c file2.cfg'\n"
	+ "  -C filename = read context config parameters from filename\n"
	+ "  -d delay = delay sending for delay seconds after source creation\n"
	+ "  -D = Use SDM Messages\n"
	+ "  -e = use LBM embedded mode\n"
	+ "  -f = use hot-failover\n"
	+ "  -i init =  hot-failover: start with this initial sequence number\n" 
	+ "  -h = help\n"
	+ "  -l len = send messages of len bytes\n"
	+ "  -L linger = linger for linger seconds before closing context\n"
	+ "  -M msgs = send msgs number of messages\n"
	+ "  -N chn = send messages on channel chn\n"
	+ "  -n = use non-blocking I/O\n"
	+ "  -P msec = pause after each send msec milliseconds\n"
	+ "  -R [UM]DATA/RETR = Set transport type to LBT-R[UM], set data rate limit to\n"
	+ "                     DATA bits per second, and set retransmit rate limit to\n"
	+ "                     RETR bits per second.  For both limits, the optional\n"
	+ "                     k, m, and g suffixes may be used.  For example,\n"
	+ "                     '-R 1m/500k' is the same as '-R 1000000/500000'\n"
	+ "  -t filename = use filename contents as a recording of message sequence numbers (HF only!)\n"
	+ "  -s sec = print stats every sec seconds\n"
	+ "  -V = construct verifiable messages\n"
	+ "  -x bits = Use 32 or 64 bits for hot-failover sequence numbers\n"
	+ "\nMonitoring options:\n"
	+ "  --monitor-ctx NUM = monitor context every NUM seconds\n"
	+ "  --monitor-src NUM = monitor source every NUM seconds\n"
	+ "  --monitor-transport TRANS = use monitor transport module TRANS\n"
	+ "                              TRANS may be `lbm', `udp', or `lbmsnmp', default is `lbm'\n"
	+ "  --monitor-transport-opts OPTS = use OPTS as transport module options\n"
	+ "  --monitor-format FMT = use monitor format module FMT\n"
	+ "                         FMT may be `csv'\n"
	+ "  --monitor-format-opts OPTS = use OPTS as format module options\n"
	+ "  --monitor-appid ID = use ID as application ID string\n"
	;

	public static void main(String[] args)
	{
		lbmsrc srcapp = new lbmsrc(args);
	}
	
	int send_rate = 0;							//	Used for lbmtrm | lbtru transports
	int retrans_rate = 0;						//
	char protocol = '\0';						//
	int linger = 5;
	int delay = 1;
	int monitor_context_ivl = 0;
	int monitor_source_ivl = 0;
	boolean monitor_context = false;
	boolean monitor_source = false; 
	int mon_transport = LBMMonitor.TRANSPORT_LBM;
	int mon_format = LBMMonitor.FORMAT_CSV;
	String mon_format_options = "";
	String mon_transport_options = "";
	String application_id = null;
	String cconffname = null;
	int msglen = 25;
	long bytes_sent = 0;
	int pause = 0;
	boolean block = true;
	boolean use_hf = false;
	long seq_counter = 0;
	long channel = -1;
	ByteBuffer message = null;
	String topicname = null;
	File tape_file = null;
	Scanner tape_scanner = null;
	int tape_msgs_sent = 0;
	int hfbits = 32;
	LBMObjectRecycler objRec = new LBMObjectRecycler();

	private void process_cmdline(String[] args)
	{
		LongOpt[] longopts = new LongOpt[7];
		final int OPTION_MONITOR_CTX = 2;
		final int OPTION_MONITOR_SRC = 3;
		final int OPTION_MONITOR_TRANSPORT = 4;
		final int OPTION_MONITOR_TRANSPORT_OPTS = 5; 
		final int OPTION_MONITOR_FORMAT = 6;
		final int OPTION_MONITOR_FORMAT_OPTS = 7;
		final int OPTION_MONITOR_APPID = 8;

		longopts[0] = new LongOpt("monitor-ctx", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_CTX);
		longopts[1] = new LongOpt("monitor-src", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_SRC);
		longopts[2] = new LongOpt("monitor-transport", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT);
		longopts[3] = new LongOpt("monitor-transport-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_TRANSPORT_OPTS);
		longopts[4] = new LongOpt("monitor-format", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT);
		longopts[5] = new LongOpt("monitor-format-opts", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_FORMAT_OPTS);
		longopts[6] = new LongOpt("monitor-appid", LongOpt.REQUIRED_ARGUMENT, null, OPTION_MONITOR_APPID);
		Getopt gopt = new Getopt("lbmsrc", args, "+C:d:Defc:hl:M:N:i:nP:R:s:t:L:Vx", longopts);
		int c = -1;
		boolean error = false;

		while ((c = gopt.getopt()) != -1)
		{
			try
			{
				switch (c)
				{
					case OPTION_MONITOR_APPID:
						application_id = gopt.getOptarg();
						break;
					case OPTION_MONITOR_CTX:
						monitor_context = true;
						monitor_context_ivl = Integer.parseInt(gopt.getOptarg());
						break;
					case OPTION_MONITOR_SRC:
						monitor_source = true;
						monitor_source_ivl = Integer.parseInt(gopt.getOptarg());
						break;
					case OPTION_MONITOR_TRANSPORT:
						if (gopt.getOptarg().compareToIgnoreCase("lbm") == 0)
						{
							mon_transport = LBMMonitor.TRANSPORT_LBM;
						}
						else
						{
							if (gopt.getOptarg().compareToIgnoreCase("udp") == 0)
							{
								mon_transport = LBMMonitor.TRANSPORT_UDP;
							}
							else
							{
								if (gopt.getOptarg().compareToIgnoreCase("lbmsnmp") == 0)
								{
									mon_transport = LBMMonitor.TRANSPORT_LBMSNMP;
								}
								else
								{
									error = true;
								}
							}
						}
						break;
					case OPTION_MONITOR_TRANSPORT_OPTS:
						mon_transport_options += gopt.getOptarg();
						break;
					case OPTION_MONITOR_FORMAT:
						if (gopt.getOptarg().compareToIgnoreCase("csv") == 0)
							mon_format = LBMMonitor.FORMAT_CSV;
						else
							error = true;
						break;
					case OPTION_MONITOR_FORMAT_OPTS:
						mon_format_options += gopt.getOptarg();
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
						delay = Integer.parseInt(gopt.getOptarg());
						break;
					case 'D':
						if(verifiable)
						{
							System.err.println("Unable to use SDM because verifiable messages are on. Turn off verifiable messages (-V).");
							System.exit(1);
						}
						sdm = true;
						break;
					case 'e':
						sequential = false;
						break;
					case 'f':
						use_hf = true;
						break;
					case 'h':
						print_help_exit(0);
					case 'l':
						msglen = Integer.parseInt(gopt.getOptarg());
						break;
					case 'n':
						block = false;
						break;
					case 'L':
						linger = Integer.parseInt(gopt.getOptarg());
						break;
					case 'M':
						msgs = Long.parseLong(gopt.getOptarg());
						break;
					case 'N':
						channel = Long.parseLong(gopt.getOptarg());
						break;
					case 'i':
						seq_counter = (new BigInteger(gopt.getOptarg())).longValue();
						break;
					case 'P':
						pause = Integer.parseInt(gopt.getOptarg());
						break;
					case 'R':
						ParseRateVars parseRateVars = lbmExampleUtil.parseRate(gopt.getOptarg());
						if (parseRateVars.error) {
							print_help_exit(1);
						}
						protocol = parseRateVars.protocol;
						send_rate = parseRateVars.rate;
						retrans_rate = parseRateVars.retrans;
						break;
					case 's':
						stats_sec = Integer.parseInt(gopt.getOptarg());
						break;
				    case 't':
						tape_file = new File(gopt.getOptarg());
						tape_scanner = new Scanner(new FileReader(tape_file));
						break;
					case 'V':
						if(sdm)
						{
							System.err.println("Unable to use verifiable messages because sdm is on. Turn off sdm (-D).");
							System.exit(1);
						}
						verifiable = true;	
						break;
					case 'x':
						hfbits = Integer.parseInt(gopt.getOptarg());
						if (hfbits != 32 && hfbits != 64) {
							System.err.println("-x " + hfbits + " invalid, HF sequence numbers must be 32 or 64 bit");
							System.exit(1);
						}
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
				System.err.println("lbmsrc: error\n" + e);
				print_help_exit(1);
			}
		}
		
		if (error || gopt.getOptind() >= args.length)
		{	
			/* An error occurred processing the command line - print help and exit */
			print_help_exit(1);
		}
		if (tape_scanner != null && !use_hf)
		{
			print_help_exit(1);
		}
		message = ByteBuffer.allocateDirect(msglen);
		topicname = args[gopt.getOptind()];
	}
	
	private static void print_help_exit(int exit_value)
	{
		System.err.println(LBM.version());
		System.err.println(purpose);
		System.err.println(usage);
		System.exit(exit_value);
	}

	private lbmsrc(String[] args)
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
		logger = org.apache.log4j.Logger.getLogger("lbmsrc");
		org.apache.log4j.BasicConfigurator.configure();
		log4jLogger lbmlogger = new log4jLogger(logger);
		lbm.setLogger(lbmlogger);

		process_cmdline(args);

		/* If set, check the requested message length is not too small */
		if(verifiable == true)
		{
			int min_msglen = VerifiableMessage.MINIMUM_VERIFIABLE_MSG_LEN;
			if(msglen < min_msglen)
			{
				System.out.println("Specified message length " + msglen + " is too small for verifiable message.");
				System.out.println("Setting message length to minimum (" + min_msglen + ").");
				msglen = min_msglen;
			}
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
		
		sattr.setObjectRecycler(objRec, null);
		
		if (sattr.size() > 0)
			sattr.list(System.out);
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
		LBMTopic topic = null;
		try
		{
			topic =  ctx.allocTopic(topicname, sattr);
		}
		catch (LBMException ex)
		{
			System.err.println("Error allocating topic: " + ex.toString());
			System.exit(1);
		}
		LBMSource src = null;
		SrcCB srccb = new SrcCB();
		LBMContextThread ctxthread = null;
		if (sequential)
		{
			// create thread to handle event processing
			ctxthread = new LBMContextThread(ctx);
			ctxthread.start();
		}
		if (sequential)
		{
			System.err.println("Sequential mode");
		}
		else
		{
			System.err.println("Embedded mode");
		}
		try
		{
			if (use_hf)
				src = ctx.createHotFailoverSource(topic, srccb, null, null);
			else
				src = ctx.createSource(topic, srccb, null, null);
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating source: " + ex.toString());
			System.exit(1);
		}
		
		LBMSourceChannelInfo channel_info = null;
		LBMSourceSendExInfo exInfo = null;
		try
		{
			if(channel != -1)
			{
				if(use_hf) {
					System.err.println("Error creating channel: cannot send on channels with hot failover.");
					System.exit(1);
				}
				channel_info = src.createChannel(channel);
				exInfo = new LBMSourceSendExInfo(LBM.SRC_SEND_EX_FLAG_CHANNEL, null, channel_info);

			}
		}
		catch (LBMException ex)
		{
			System.err.println("Error creating channel: " + ex.toString());
			System.exit(1);
		}

		SrcStatsTimer stats = null;
		if (stats_sec > 0)
		{
			try
			{
				stats = new SrcStatsTimer(ctx, src, stats_sec * 1000, objRec);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating timer: " + ex.toString());
				System.exit(1);
			}
		}
		try
		{
			Thread.sleep(1000);
		}
		catch (InterruptedException e) 
		{
			System.err.println("lbmsrc: error--" + e);
		}
		LBMMonitorSource lbmmonsrc = null;
		if (monitor_context || monitor_source)
		{
			try
			{
				lbmmonsrc = new LBMMonitorSource(mon_format, mon_format_options, mon_transport, mon_transport_options);
			}
			catch (LBMException ex)
			{
				System.err.println("Error creating monitor source: " + ex.toString());
				System.exit(1);
			}
			try
			{
				if (monitor_context)
					lbmmonsrc.start(ctx, application_id, monitor_context_ivl);
				else
					lbmmonsrc.start(src, application_id, monitor_source_ivl);
			}
			catch (LBMException ex)
			{
				System.err.println("Error starting monitoring: " + ex.toString());
				System.exit(1);
			}
		}
		// If using SDM messages, create the message now
		if(sdm) {
			CreateSDMessage();
			byte [] m = UpdateSDMessage(seq_counter);
			msglen = m.length;
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
				System.err.println("lbmsrc: error\n" + e);
			}
		}
		
		System.out.printf("Sending %d messages of size %d bytes to topic[%s]\n", 
		                   msgs, msglen, topicname);
						   
		System.out.flush();	
		long start_time = System.currentTimeMillis();
		long totmsgs;

		LBMSourceSendExInfo hfexinfo = null;
		if (use_hf) {
			int hfflags = (hfbits == 64) ? LBM.SRC_SEND_EX_FLAG_HF_64 : LBM.SRC_SEND_EX_FLAG_HF_32;
			// The hf exinfo contains whether the sqn is 32 or 64 bit
			hfexinfo = new LBMSourceSendExInfo(hfflags, null);
		}
		for ( totmsgs = 0 ; totmsgs < msgs ; )
		{

			try
			{
				if(verifiable)
				{
					message.position(0);
					message.put(VerifiableMessage.constructVerifiableMessage(msglen));
				}
				else if(sdm)  // If using SDM messages, Update the sequence number
				{
					byte [] m = UpdateSDMessage(seq_counter);
					if(m != null)
					{
						message.position(0);
						message.put(m);
						msglen = m.length;
					}
				}
				// Look for HF tape file
				boolean sendReset = false;
				if (tape_scanner != null) {
					String tape_str, tape_line = null;
					BigInteger sqn = null;
					
					if (!tape_scanner.hasNextLine())
						break;
						
					tape_line = tape_scanner.nextLine();
					// Make sure the optional flag is off
					hfexinfo.setFlags(hfexinfo.flags() & ~LBM.SRC_SEND_EX_FLAG_HF_OPTIONAL);
					
					if (tape_line.endsWith("o")) {
						// Set the optional flag
						hfexinfo.setFlags(hfexinfo.flags() | LBM.SRC_SEND_EX_FLAG_HF_OPTIONAL);
						tape_str = tape_line.substring(0, tape_line.length() - 1);
					} 
					else if (tape_line.endsWith("r")) {
						sendReset = true;
						tape_str = tape_line.substring(0, tape_line.length() - 1);
					}
					else {
						tape_str = tape_line;
						
					}
					// Parse the string using BigInteger to support unsigned 64 bit values
					sqn = new BigInteger(tape_str);
					hfexinfo.setHfSequenceNumber(sqn.longValue());
				}

				srccb.blocked = true;
				if (use_hf) {
					if (tape_scanner == null) {
						hfexinfo.setHfSequenceNumber(seq_counter);
					}
					// Hot Failover Sequence number is in hfexinfo
					if (sendReset) {
						((LBMHotFailoverSource)src).sendReceiverReset(LBM.MSG_FLUSH, hfexinfo);
					}
					else {
						((LBMHotFailoverSource)src).send(message, 0, msglen, 0, block ? 0 : LBM.SRC_NONBLOCK, hfexinfo);
					}
					if (tape_scanner != null) {
						tape_msgs_sent++;
					}
				} else if (exInfo != null) {
					src.send(message, 0, msglen, block ? 0 : LBM.SRC_NONBLOCK, exInfo);
				} else {
					src.send(message, 0, msglen, block ? 0 : LBM.SRC_NONBLOCK);
				}
				srccb.blocked = false;
				if (tape_scanner == null)
				{
					seq_counter++;
					totmsgs++;
				}
			}
			catch (LBMEWouldBlockException ex)
			{
				while (srccb.blocked)
				{
					try
					{
						Thread.sleep(100);
					}
					catch (InterruptedException e) 
					{ 
						System.err.println("lbmsrc: error\n" + e);
					}
				}
				continue;
			}
			catch (LBMException ex)
			{
				System.err.println("Error sending message: " + ex.toString());
			}
			bytes_sent += msglen;
			if (pause >0)
			{
				try
				{
					Thread.sleep(pause);
				}
				catch (InterruptedException e) 
				{
					System.err.println("lbmsrc: error\n" + e);
				}
			}
		}
		long end_time = System.currentTimeMillis();
		double secs = (end_time - start_time) / 1000.;
		
		System.out.printf("Sent %d messages of size %d bytes in %.03f second%s.\n", 
						  (tape_scanner == null) ? msgs : tape_msgs_sent, msglen, secs, ((secs > 1) ? "s" : ""));
		
		print_bw(secs, (tape_scanner == null) ? msgs : tape_msgs_sent, bytes_sent);
		System.out.flush();	
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
				System.err.println("lbmsrc: error\n" + e);
			}
		}
		if (sequential)
		{
			ctxthread.terminate();
		}
		if (lbmmonsrc != null)
		{
			try
			{
				lbmmonsrc.close();
			}
			catch (LBMException ex)
			{
				System.err.println("Error closing monitor source: " + ex.toString());
			}
		}
		
		if(channel_info != null)
		{
			try
			{
				src.deleteChannel(channel_info);
			}
			catch (LBMException ex)
			{
				System.err.println("Error closing channel: " + ex.toString());
			}
		}

		
		objRec.close();
		
		try
		{
			src.close();
		}
		catch (LBMException ex)
		{
			System.err.println("Error closing source: " + ex.toString());
		}
		if (tape_scanner != null)
			tape_scanner.close();
		ctx.close();
	}

	private static LBMSDMessage SDMsg;
	private static void CreateSDMessage()
	{
		try {
			// Create an SDM message and add several fields to the message
			SDMsg = new LBMSDMessage();

			// The sequence number is a specific fields which is updated as the message is sent
			LBMSDMFieldInt64 seqfield = new LBMSDMFieldInt64("Sequence Number",0L);
			SDMsg.add(seqfield);
		
			// Some other field types demonstrating SDM Fields.
			LBMSDMFieldUint16 int16bitfield = new LBMSDMFieldUint16("16 Bit Unsigned Int",16<<8);
			SDMsg.add(int16bitfield);

			LBMSDMFieldString stringfield = new LBMSDMFieldString("Application Name","lbmsrc");
			SDMsg.add(stringfield);

			LBMSDMFieldBlob blobfield = new LBMSDMFieldBlob("Blob Field", new String("blob field").getBytes());
			SDMsg.add(blobfield);

			LBMSDMFieldBool booleanfield = new LBMSDMFieldBool("Boolean Field", true);
			SDMsg.add(booleanfield);

			LBMSDMFieldDecimal decimalfield = new LBMSDMFieldDecimal("Decimal Field", 1000, -23);
			SDMsg.add(decimalfield);

			LBMSDMFieldDouble doublefield = new LBMSDMFieldDouble("Double Field", 100.0056);
			SDMsg.add(doublefield);

			LBMSDMFieldFloat floatfield = new LBMSDMFieldFloat("Float Field", 100.023f);
			SDMsg.add(floatfield);

			LBMSDMFieldInt16 int16field = new LBMSDMFieldInt16("Int 16 Field", (short)250);
			SDMsg.add(int16field);

			LBMSDMFieldInt32 int32field = new LBMSDMFieldInt32("Int 32 Field", 25000);
			SDMsg.add(int32field);

			long val = 2500000000L;
			LBMSDMFieldInt64 int64field = new LBMSDMFieldInt64("Int 64 Field", val);
			SDMsg.add(int64field);

			LBMSDMFieldInt8 int8field = new LBMSDMFieldInt8("Int 8 Field", (byte)25);
			SDMsg.add(int8field);

			// Add an array of Int 32 types, with two elements
			LBMSDMArrayInt32 int32array = new LBMSDMArrayInt32("Int 32 Array");
			SDMsg.add(int32array);
			int32array.append(1);
			int32array.append(1<<16);

			// Now create a message to be nested inside SDMsg as a field
			LBMSDMFieldMessage msgfield = new LBMSDMFieldMessage("Field Message");
			LBMSDMessage msg2 = new LBMSDMessage();

			// Add some fields to the nested message
			LBMSDMFieldInt16 int16field2 = new LBMSDMFieldInt16("Int 16 Field2", (short)-250);
			msg2.add(int16field2);
			
			LBMSDMFieldInt32 int32field2 = new LBMSDMFieldInt32("Int 32 Field2", 25000);
			msg2.add(int32field2);

			LBMSDMFieldInt64 int64field2 = new LBMSDMFieldInt64("Int 64 Field2");
			int64field.set(val);
			msg2.add(int64field2);

			LBMSDMFieldInt8 int8field2 = new LBMSDMFieldInt8("Int 8 Field2", (byte)25);
			msg2.add(int8field2);

			// Set the nested message in the message field
			msgfield.set(msg2);

			// Add the message field to SDMsg
			SDMsg.add(msgfield);
		}
		catch (LBMSDMException sdme)
		{
			System.out.println("Failed to create the SDM message:" + sdme);
			System.out.flush();	
		}
	}
	private static byte [] UpdateSDMessage(long seq_num)
	{
		if(SDMsg == null) {
			// Should not be possible since CreateSDMessage is called before the message loop
			System.out.println("No SDM message available");
			System.out.flush();	
			return null;
		}
		LBMSDMField f = SDMsg.locate("Sequence Number");
		if(f == null) {
			System.out.println("Could not find field 'Sequence Number'");
			System.out.flush();	
			return null;
		}
		
		((LBMSDMFieldInt64)f).set(seq_num);

		try {
			return SDMsg.data();
		}
		catch (LBMSDMException sdme) {
			System.out.println("Error occurred updating SDM message with sequence number " + seq_num + ":" + sdme);
			System.out.flush();	
			return null;
		}
	}

	private static void print_bw(double sec, long msgs, long bytes)
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

class SrcCB implements LBMSourceEventCallback
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
			default:
				break;
		}
        sourceEvent.dispose();
		System.out.flush();	
		return 0;
	}
}

class SrcStatsTimer extends LBMTimer
{
	LBMSource _src;
	boolean _done = false;
	long _tmo;
	LBMObjectRecyclerBase _recycler = null;

	public SrcStatsTimer(LBMContext ctx, LBMSource src, long tmo, LBMObjectRecyclerBase objRec) throws LBMException
	{
		super(ctx, tmo, null);
		_src = src;
		_tmo = tmo;
		_recycler = objRec;
	}

	public void done()
	{
		_done = true;
	}

	private void onExpiration()
	{
		print_stats();
		if (!_done)
		{
			try
			{
				this.reschedule(_tmo);
			}
			catch (LBMException ex)
			{
				System.err.println("Error rescheduling timer: " + ex.toString());
			}
		}
	}

	private void print_stats()
	{
		try
		{
			LBMSourceStatistics stats = _src.getStatistics();
			switch (stats.type())
			{
				case LBM.TRANSPORT_STAT_TCP:
					System.out.println("TCP, buffered " + stats.bytesBuffered()
							   			+ ", clients " + stats.numberOfClients());
					break;
				case LBM.TRANSPORT_STAT_LBTRU:
					System.out.println("LBT-RU, sent " + stats.messagesSent()  + "/" + stats.bytesSent()
									+ ", naks " + stats.naksReceived() + "/" + stats.nakPacketsReceived()
									+ ", ignored " + stats.naksIgnored() + "/" + stats.naksIgnoredRetransmitDelay()
									+ ", shed " + stats.naksShed()
									+ ", rxs " + stats.retransmissionsSent()
									+ ", clients " + stats.numberOfClients());
					break;
				case LBM.TRANSPORT_STAT_LBTRM:
					System.out.println("LBT-RM, sent " + stats.messagesSent() + "/" + stats.bytesSent()
									+ ", txw " + stats.transmissionWindowMessages() + "/" + stats.transmissionWindowBytes()
									+ ", naks " + stats.naksReceived() + "/" + stats.nakPacketsReceived()
									+ ", ignored " + stats.naksIgnored() + "/" + stats.naksIgnoredRetransmitDelay()
									+ ", shed " + stats.naksShed()
									+ ", rxs " + stats.retransmissionsSent()
									+ ", rctl " + stats.messagesQueued() + "/" + stats.retransmissionsQueued());
					break;
				case LBM.TRANSPORT_STAT_LBTIPC:
					System.out.println("LBT-IPC, clients " + stats.numberOfClients()
									+ ", sent " + stats.messagesSent() + "/" + stats.bytesSent());
					break;
				case LBM.TRANSPORT_STAT_LBTRDMA:
					System.out.println("LBT-RDMA, clients " + stats.numberOfClients()
									+ ", sent "  + stats.messagesSent() + "/" + stats.bytesSent());
					break;
			}
			if(_recycler != null) {
				_recycler.doneWithSourceStatistics(stats);
			}
			System.out.flush();
		}
		catch (LBMException ex)
		{
			System.err.println("Error getting source statistics: " + ex.toString());
		}	
	}
}

