/*file: SimpleTopicPublisher.java
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
 * The SimpleTopicPublisher class consists only of a main method,
 * which publishes several messages to a topic.
 *
 * Run this program in conjunction with SimpleTopicSubscriber.
 */
import javax.jms.*;
import javax.naming.*;

public class SimpleTopicPublisher {

    public static void main(String[] args) {
        Context jndiContext = null;
        TopicConnectionFactory topicConnectionFactory = null;
        TopicConnection topicConnection = null;
        TopicSession topicSession = null;
        Topic topic = null;
        javax.jms.TopicPublisher topicPublisher = null;
        Message message = null;

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
            topicConnectionFactory = (TopicConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            topic = (Topic) jndiContext.lookup("TempTopic");

        } catch (Exception e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            e.printStackTrace();
            System.exit(1);
        }

        /*
         * Create connection.
         * Create session from connection; false means session is
         * not transacted.
         * Create publisher and text message.
         * Send messages, varying text slightly.
         * Finally, close connection.
         */

        try {
            topicConnection =
                    topicConnectionFactory.createTopicConnection();
            topicConnection.start();
            topicConnection.setExceptionListener(new MyExceptionListener());

            topicSession =
                    topicConnection.createTopicSession(false,
                    Session.AUTO_ACKNOWLEDGE);
            topicPublisher = topicSession.createPublisher(topic);

            message = topicSession.createTextMessage();
            for (int i = 0; i < 10; i++) {
                ((TextMessage) message).setText("This is message " + (i + 1));
                System.out.println("Publishing message: "
                        + ((TextMessage) message).getText());
                Thread.sleep(100);
                topicPublisher.publish(message);
            }

            topicPublisher.publish(topicSession.createMessage());

            topicPublisher.close();
            topicSession.close();

        } catch (JMSException e) {
            System.out.println("Exception occurred: "
                    + e.toString());

        } catch (Exception e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } finally {
            if (topicConnection != null) {
                try {
                    topicConnection.close();
                    System.exit(0);
                } catch (JMSException e) {
                }
            }
        }
    }
}
