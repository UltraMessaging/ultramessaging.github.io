/*file: SimpleTopicSubscriber.java
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
 * The SimpleTopicSubscriber class consists only of a main
 * method, which receives one or more messages from a topic using
 * asynchronous message delivery.  It uses the message listener
 * TextListener.  Run this program in conjunction with
 * SimpleTopicPublisher.
 */
import javax.jms.*;
import javax.naming.*;
import java.io.*;

public class SimpleTopicSubscriber {

    public static void main(String[] args) {
        Context jndiContext = null;
        TopicConnectionFactory topicConnectionFactory = null;
        TopicConnection topicConnection = null;
        TopicSession topicSession = null;
        Topic topic = null;
        javax.jms.TopicSubscriber topicSubscriber = null;
        MessageListener topicListener = null;
        TextMessage message = null;
        InputStreamReader inputStreamReader = null;
        char answer = '\0';

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
        } catch (NamingException e) {
            System.out.println("JNDI API lookup failed: "
                    + e.toString());
            e.printStackTrace();
        }

        /*
         * Create connection.
         * Create session from connection; false means session is
         * not transacted.
         * Create subscriber.
         * Receive text messages from topic
         * Close connection.
         */
        try {
            topicConnection = topicConnectionFactory.createTopicConnection();
            topicSession = topicConnection.createTopicSession(false,
                    Session.AUTO_ACKNOWLEDGE);
            topicSubscriber = topicSession.createSubscriber(topic);
            topicListener = new MyListener();
            topicConnection.start();
            while (true) {
                Message m = topicSubscriber.receive();
                if (m instanceof TextMessage) {
                    System.out.println(((TextMessage) m).getText());
                }
                else
                {
                    break;
                }
            }
        } catch (Exception e) {
            System.out.println("Exception occurred: "
                    + e.toString());
        } finally {
            if (topicConnection != null) {
                try {
                    topicConnection.close();
                } catch (JMSException e) {
                }
            }
        }
    }
}
