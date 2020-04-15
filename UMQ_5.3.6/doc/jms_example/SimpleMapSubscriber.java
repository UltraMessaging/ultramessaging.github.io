/*file: SimpleMapSubscriber.java
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

/*
 * The SimpleMapSubscriber class consists only of a main
 * method, which receives one or more MapMessages from a topic 
 * and prints the contents.
 */
import com.latencybusters.jms.topic.LBMTopic;
import javax.jms.*;
import javax.naming.*;

public class SimpleMapSubscriber {

    /**
     * Main method.
     *
     * @param args     the topic used by the example
     */
    public static void main(String[] args) {
        Context jndiContext = null;

System.out.println("Subscriber");
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
            Topic source = (Topic) jndiContext.lookup("MapTopic");
            Connection connection = factory.createConnection();

            Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
            MessageConsumer consumer = session.createConsumer(source);

            connection.start();
            while (true) {
                Message message = consumer.receive();
                if (message instanceof MapMessage == false) {
                    break;
                }
//                System.out.println("One" + " " + ((MapMessage) message).getDouble("One"));
//                System.out.println("Two" + " " + ((MapMessage) message).getString("Two"));
//                System.out.println("Three" + " " + ((MapMessage) message).getLong("Three"));
//
                byte b[] = ((MapMessage) message).getBytes("Four");
                System.out.println("Subscriber Four "+b.length);
//
//                System.out.println();
//                System.out.println("Five" + " " + ((MapMessage) message).getBoolean("Five"));
//                System.out.println("Six" + " " + ((MapMessage) message).getObject("Six") + " - " + ((MapMessage) message).getObject("Six").getClass().getName());
//                System.out.println("NEGATIVE" + " " + ((MapMessage) message).getObject("NEGATIVE"));


            }

            connection.close();
            jndiContext.close();
            System.exit(0);
        } catch (Exception ex) {
            ex.printStackTrace();
            System.exit(1);
        }
    }
}
