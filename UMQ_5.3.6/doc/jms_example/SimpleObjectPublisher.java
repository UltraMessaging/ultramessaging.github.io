/*file: SimpleObjectPublisher.java
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
 * The SimpleObjectPublisher consists only of a main
 * method, which publishes 10 ObjectMessages
*/

import javax.jms.*;
import javax.naming.*;
import java.io.*;

public class SimpleObjectPublisher {

    /**
     * Main method.
     *
     * @param args     the topic used by the example
     */
    public static void main(String[] args) {
        String topicName = null;
        Context jndiContext = null;
        TopicConnectionFactory topicConnectionFactory = null;
        TopicConnection topicConnection = null;
        TopicSession topicSession = null;
        Topic topic = null;
        TopicSubscriber topicSubscriber = null;
        MessageListener topicListener = null;
        TextMessage textMessage = null;
        InputStreamReader inputStreamReader = null;
        char answer = '\0';
        BillyBob bb;

        /*
         * Read topic name from command line and display it.
         */
        if (args.length != 1) {
            System.out.println("Usage: java "
                    + "delay");
            System.exit(1);
        }

        int delay = Integer.parseInt(args[0]);

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
        Connection connection = null;
        try {



            ConnectionFactory factory = (ConnectionFactory) jndiContext.lookup("uJMSConnectionFactory");
            Destination source = (Destination) jndiContext.lookup("ObjectTopic");
            connection = factory.createConnection();
            connection.start();
            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

            MessageProducer producer = session.createProducer(null);
            bb = new BillyBob();
            ObjectMessage message = session.createObjectMessage();

            for (int i = 0; i < 10; i++) {
                //   System.out.println(" getBodyLength() "+ message.getBodyLength());
                bb.x = i;
                bb.y = i * 2;
                bb.z = i * 4;
                message.setObject(bb);

                producer.send(source, message);
                if (delay > 0) {
                    Thread.sleep(delay);
                }
            }
            producer.send(source, session.createMessage());
            connection.close();

        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);

        } finally {
            if (connection != null) {
                try {
//                    connection.close();
                    System.exit(0);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }
}

class BillyBob implements Serializable {

    int x = 0;
    int y = 0;
    int z = 0;
    String type = "BOATING";

    public BillyBob() {
    }

    public String toString() {
        return type + " " + x + " " + y + " " + z;
    }
}

