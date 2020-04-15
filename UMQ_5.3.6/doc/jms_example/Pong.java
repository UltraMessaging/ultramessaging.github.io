/*file: Pong.java
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
 * The Pong class works with the Ping class.
 *
 */
import javax.jms.*;
import javax.naming.*;

public class Pong implements MessageListener {

    MessageProducer replyProducer;
    Session session;

    public static void main(String[] args) {

        new Pong();
        while (true) {
            try {
                Thread.sleep(1000000);
            } catch (Exception ex) {
                ex.printStackTrace();
            }
        }
    }

    public Pong() {
        Context jndiContext = null;

        /*
         * Create a JNDI API InitialContext object if none exists
         * yet.
         */
        try {
            jndiContext = new InitialContext();
        } catch (NamingException e) {
            System.out.println("Could not create JNDI API "
                    + "context: " + e.toString());
            e.printStackTrace();
            System.exit(1);
        }
        System.out.println("Test Pong");
        /*
         * Look up connection factory and topic.  If either does
         * not exist, exit.
         */
        try {
            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            // ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("cn=LBMConnectionFactory");
            Connection connection = factory.createConnection();

            // Create a Session
            session = connection.createSession(false,
                    javax.jms.Session.AUTO_ACKNOWLEDGE);

            // Create request destination
            Destination replyTopic = (Destination) jndiContext.lookup("ReplyTopic");
            Destination requestTopic = (Destination) jndiContext.lookup("RequestTopic");
//            Destination requestTopic = (Destination) jndiContext.lookup("cn=RequestTopic");

            // Create consumer on request topic
            MessageConsumer requestConsumer = session.createConsumer(requestTopic);

            // Create a producer, don't know reply destination at this point.
            replyProducer = session.createProducer(replyTopic);

            // set the message listener callback
            //    requestConsumer.setMessageListener(this);

            // start the connection
            Message m;
            connection.start();
            while (true) {
                m = requestConsumer.receive();
                replyProducer.send(m);
            }

        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

// The message listener callback
    public void onMessage(Message message) {
        try {
            replyProducer.send(message);
        } catch (Exception e) {
            System.err.println("Exception occurred: " + e.getMessage());
            System.exit(-1);
        }
    }
}
