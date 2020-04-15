/*file: DurableConsumer.java
 *
 * Copyright (c) 2005-2014 Informatica Corporation  Permission is granted to licensees to use
 * or alter this software for any purpose, including commercial applications,
 * according to the terms laid out in the Software License Agreement.
-
- This source code example is provided by Informatica for educational
- and evaluation purposes only.
-
- THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL WARRANTIES
- EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY IMPLIED WARRANTIES OF
- NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
- PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE OF THE SOFTWARE WILL BE
- UNINTERRUPTED OR ERROR-FREE.  INFORMATICA SHALL NOT, UNDER ANY CIRCUMSTANCES, BE
- LIABLE TO LICENSEE FOR LOST PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR
- INDIRECT DAMAGES ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE
- TRANSACTIONS CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
- THE LIKELIHOOD OF SUCH DAMAGES.
-
 */
package examples;

/**
 * The DurableConsumer class consists only of a main method,
 * which fetches one or more messages from a queue using
 * synchronous message delivery. Run this program in conjunction
 * with DurableProducer.
 */
import javax.jms.*;
import javax.naming.*;

public class DurableConsumer {

    public static void main(String[] args) {
        Context jndiContext = null;
        ConnectionFactory connectionFactory = null;
        Connection connection = null;
        Session session = null;
        Topic dest = null;
        MessageConsumer consumer = null;
        TextMessage message = null;

        /*
         * Create a JNDI API InitialContext object if none exists
         * yet.
         */
        try {
            jndiContext = new InitialContext();
        } catch (NamingException e) {
            System.out.println("Could not create JNDI API "
                    + "context: " + e.toString());
            System.exit(1);
        }

        /*
         * Look up connection factory and queue.  If either does
         * not exist, exit.
         */
        try {
            connectionFactory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            dest = (Topic) jndiContext.lookup("DurableTopic");//This must be a LBMTopic that has the REGID set.
        } catch (NamingException e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            System.exit(1);
        }

        /*
         * Create connection.
         * Create session from connection; false means session is
         * not transacted.
         * Create receiver, then start message delivery.
         * Receive all text messages from queue until
         * a non-text message is received indicating end of
         * message stream.
         * Close connection.
         */
        try {
            connection = connectionFactory.createConnection();
            session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

            // The name argument must be a number. It represents an offest from the UME port.
            consumer = session.createDurableSubscriber(dest, "1001");
            connection.start();
            while (true) {
                Message m = consumer.receive(1);
                if (m != null) {
                    if (m instanceof TextMessage) {
                        message = (TextMessage) m;
                        System.out.println("Reading message: " + message.getText());
                    } else {
                        break;
                    }
                }

            }
        } catch (JMSException e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } finally {
            if (connection != null) {
                try {
                    connection.close();
                } catch (JMSException e) {
                }
            }
        }
    }
}


