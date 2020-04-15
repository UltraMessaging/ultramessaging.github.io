/*file: Reply.java
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
 * The Reply class consists only of a main
 * method, which receives one or more messages from a topic using
 * asynchronous message delivery The program will reply ot the ender based on getJMSReplyTo. Run this program in conjunction with
 * Request.java.
 *
 */
import javax.jms.*;
import javax.naming.*;

public class Reply implements MessageListener {

    MessageProducer replyProducer;
    Session session;

    public static void main(String[] args) {

        new Reply();
        while (true) {
            try {
                Thread.sleep(1000000);
            } catch (Exception ex) {
                ex.printStackTrace();
            }
        }
    }

    public Reply() {
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

        /*
         * Look up connection factory and topic.  If either does
         * not exist, exit.
         */
        try {
            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            // ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("cn=LBMConnectionFactory");
            Connection connection = factory.createConnection();

            // Create a Session
            session = connection.createSession(false, javax.jms.Session.AUTO_ACKNOWLEDGE);

            // Create request destination
            Destination requestTopic = (Destination) jndiContext.lookup("RequestTopic");

            // Create consumer on request topic
            MessageConsumer requestConsumer = session.createConsumer(requestTopic);

            // Create a producer, don't know reply destination at this point.
            replyProducer = session.createProducer(null);

            // set the message listener callback
            requestConsumer.setMessageListener(this);

            // start the connection
            connection.start();

        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

// The message listener callback
    public void onMessage(Message message) {
        try {
            TextMessage requestMessage = (TextMessage) message;
            String contents = requestMessage.getText();
            System.out.println("Got Message: " + contents);
            // get the reply to destination
            Destination replyDestination = requestMessage.getJMSReplyTo();
            if(replyDestination != null) {
                TextMessage replyMessage = session.createTextMessage();
                replyMessage.setText(contents);

                replyMessage.setJMSCorrelationID(requestMessage.getJMSMessageID());
                System.out.println("Sending reply");
                replyProducer.send(replyDestination, replyMessage);
            } else {
                System.err.println("WARNING: No reply can be sent because the message did not have a JMSReplyTo set.");
            }
        } catch (Exception e) {
            System.err.println("Exception occurred: " + e.getMessage());
            System.exit(-1);
        }
        System.exit(0);
    }
}
