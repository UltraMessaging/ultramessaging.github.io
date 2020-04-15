/*
  All of the documentation and software included in this and any
  other Informatica Corporation Ultra Messaging Releases
  Copyright (C) Informatica Corporation. All rights reserved.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted only as covered by the terms of a
  valid software license agreement with Informatica Corporation.
  
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

#ifndef MONMODOPTS_H_INCLUDED
#define MONMONOPTS_H_INCLUDED

#define MONMODULEOPTS_PRELUDE \
"\n" \
"Transport and format options are passed as name=value pairs, separated by a semicolon.\n" \
"The entire option string should be enclosed in double-quotes.\n"


#define MONMODULEOPTS_LBM_PRELUDE \
"\n" \
"LBM transport options:\n" \
"Note that individual LBM options can be specified as <scope>|<option>=value, where\n" \
"  <scope> is one of context, source, receiver, wildcard_receiver, or event_queue\n" \
"  <option> is the LBM configuration option name\n" \
"The vertical bar (pipe symbol) is required when specifying individual LBM options.\n"

#define MONMODULEOPTS_LBMSNMP_PRELUDE \
"\n" \
"LBMSNMP transport options:\n" \
"Note that individual LBM options can be specified as <scope>|<option>=value, where\n" \
"  <scope> is one of context, source, receiver, wildcard_receiver, or event_queue\n" \
"  <option> is the LBM configuration option name\n" \
"The vertical bar (pipe symbol) is required when specifying individual LBM options.\n"

#define MONMODULEOPTS_UDP_PRELUDE \
"\n" \
"UDP transport options:\n"

#define MONMODULEOPTS_CSV_PRELUDE \
"\n" \
"CSV format options:\n"

/* All module options. */
#define MONMODULEOPTS_ALL \
MONMODULEOPTS_PRELUDE \
MONMODULEOPTS_LBM_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            send/receive statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
"  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n" \
"                         See https://communities.informatica.com/infakb/faq/5/Pages/80075.aspx\n" \
"                         for guidelines on using wildcard topics. Also make sure the statistics\n" \
"                         topic namespace is disjoint from the data topic namespace.\n" \
MONMODULEOPTS_LBMSNMP_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            send/receive statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
"  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n" \
"                         See https://communities.informatica.com/infakb/faq/5/Pages/80075.aspx\n" \
"                         for guidelines on using wildcard topics. Also make sure the statistics\n" \
"                         topic namespace is disjoint from the data topic namespace.\n" \
MONMODULEOPTS_UDP_PRELUDE \
"  address=IP             send statistics to address IP\n" \
"  port=NUM               receive on UDP port NUM\n" \
"                         default is 2933\n" \
"  interface=IP           receive multicast on interface IP\n" \
"                         default is INADDR_ANY (0.0.0.0)\n" \
"  mcgroup=GRP            send/receive on multicast group GRP\n" \
"  bcaddress=IP           send statistics to broadcast address IP\n" \
"  ttl=NUM                send multicast statistics with TTL NUM\n" \
"                         default is 16\n" \
MONMODULEOPTS_CSV_PRELUDE \
"  separator=CHAR         separate CSV fields with character CHAR\n" \
"                         defaults to `,'\n" \
"                         Don\'t use a semicolon!\n"

/* Sender-specific module options. */
#define MONMODULEOPTS_SENDER \
MONMODULEOPTS_PRELUDE \
MONMODULEOPTS_LBM_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            send statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
MONMODULEOPTS_LBMSNMP_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            send statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
MONMODULEOPTS_UDP_PRELUDE \
"  address=IP             send statistics to address IP\n" \
"  port=NUM               send to UDP port NUM\n" \
"                         default is 2933\n" \
"  mcgroup=GRP            send on multicast group GRP\n" \
"  bcaddress=IP           send statistics to broadcast address IP\n" \
"  ttl=NUM                send multicast statistics with TTL NUM\n" \
"                         default is 16\n" \
MONMODULEOPTS_CSV_PRELUDE \
"  separator=CHAR         separate CSV fields with character CHAR\n" \
"                         defaults to `,'\n" \
"                         Don\'t use a semicolon!\n"

/* Receiver-specific module options. */
#define MONMODULEOPTS_RECEIVER \
MONMODULEOPTS_PRELUDE \
MONMODULEOPTS_LBM_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            receive statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
"  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n" \
"                         See https://communities.informatica.com/infakb/faq/5/Pages/80075.aspx\n" \
"                         for guidelines on using wildcard topics. Also make sure the statistics\n" \
"                         topic namespace is disjoint from the data topic namespace.\n" \
MONMODULEOPTS_LBMSNMP_PRELUDE \
"  config=FILE            use LBM configuration file FILE\n" \
"  topic=TOPIC            receive statistics on topic TOPIC\n" \
"                         default is /29west/statistics\n" \
"  wctopic=PATTERN        receive statistics on wildcard topic PATTERN\n" \
"                         See https://communities.informatica.com/infakb/faq/5/Pages/80075.aspx\n" \
"                         for guidelines on using wildcard topics. Also make sure the statistics\n" \
"                         topic namespace is disjoint from the data topic namespace.\n" \
MONMODULEOPTS_UDP_PRELUDE \
"  port=NUM               receive on UDP port NUM\n" \
"                         default is 2933\n" \
"  interface=IP           receive multicast on interface IP\n" \
"                         default is INADDR_ANY (0.0.0.0)\n" \
"  mcgroup=GRP            receive on multicast group GRP\n" \
MONMODULEOPTS_CSV_PRELUDE \
"  separator=CHAR         separate CSV fields with character CHAR\n" \
"                         defaults to `,'\n" \
"                         Don\'t use a semicolon!\n"

#define MONOPTS_PRELUDE \
"\n" \
"Monitoring options:\n" 

#define MONOPTS_COMMON_ONLY \
"  --monitor-ctx=NUM              monitor context every NUM seconds\n" \
"  --monitor-transport=TRANS      use monitor transport module TRANS\n" \
"                                 TRANS may be `lbm', `lbmsnmp', or `udp', default is `lbm'\n" \
"  --monitor-transport-opts=OPTS  use OPTS as transport module options\n" \
"  --monitor-format=FMT           use monitor format module FMT\n" \
"                                 FMT may be `csv'\n" \
"  --monitor-format-opts=OPTS     use OPTS as format module options\n" \
"  --monitor-appid=ID             use ID as application ID string\n" \

#define MONOPTS_COMMON \
MONOPTS_PRELUDE \
MONOPTS_COMMON_ONLY

#define MONOPTS_ALL \
MONOPTS_PRELUDE \
"  --monitor-rcv=NUM              monitor receiver every NUM seconds\n" \
"  --monitor-src=NUM              monitor source every NUM seconds\n" \
MONOPTS_COMMON_ONLY

#define MONOPTS_RECEIVER \
MONOPTS_PRELUDE \
"  --monitor-rcv=NUM              monitor receiver every NUM seconds\n" \
MONOPTS_COMMON_ONLY

#define MONOPTS_SENDER \
MONOPTS_PRELUDE \
"  --monitor-src=NUM              monitor source every NUM seconds\n" \
MONOPTS_COMMON_ONLY

#define MONOPTS_EVENT_QUEUE \
"  --monitor-evq=NUM              monitor event queue every NUM seconds\n"

#endif

