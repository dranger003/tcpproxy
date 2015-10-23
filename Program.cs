using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace tcpproxy
{
    class Program
    {
        static void Main(string[] args)
        {
            var endpoint1 = new IPEndPoint(IPAddress.Parse(args[0].Split(':')[0]), Int32.Parse(args[0].Split(':')[1]));
            var endpoint2 = new IPEndPoint(IPAddress.Parse(args[1].Split(':')[0]), Int32.Parse(args[1].Split(':')[1]));

            var window = 524288;

            using (var listen = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
            {
                new Thread(
                    () =>
                    {
                        try
                        {
                            listen.Bind(endpoint1);
                            listen.Listen(4);

                            while (true)
                            {
                                try
                                {
                                    var accept = listen.Accept();
                                    new Thread(
                                        (object state) =>
                                        {
                                            try
                                            {
                                                using (var socket1 = (Socket)state)
                                                {
                                                    Console.WriteLine("A:{0}", socket1.RemoteEndPoint);

                                                    using (var socket2 = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
                                                    {
                                                        socket1.SendBufferSize = window;
                                                        socket1.ReceiveBufferSize = window;

                                                        socket2.SendBufferSize = window;
                                                        socket2.ReceiveBufferSize = window;

                                                        socket2.Connect(endpoint2);
                                                        Console.WriteLine("C:{0}", socket2.RemoteEndPoint);

                                                        new Thread(() => { Proxy(socket2, socket1); }).Start();
                                                        Proxy(socket1, socket2);

                                                        Console.WriteLine("D:{0}-{1}", socket1.RemoteEndPoint, socket2.RemoteEndPoint);
                                                    }
                                                }
                                            }
                                            catch (Exception ex)
                                            {
                                                while (ex != null)
                                                {
                                                    Console.WriteLine(ex.Message);
                                                    ex = ex.InnerException;
                                                }
                                            }
                                        }
                                    ).Start(accept);
                                }
                                catch
                                {
                                    break;
                                }
                            }
                        }
                        catch (Exception ex)
                        {
                            while (ex != null)
                            {
                                Console.WriteLine(ex.Message);
                                ex = ex.InnerException;
                            }
                        }
                    }
                ).Start();

                Console.WriteLine("Press <any key> to quit.\n");
                Console.ReadKey(true);
            }
        }

        static void Proxy(Socket socket1, Socket socket2)
        {
            try
            {
                var buffer = new byte[524288];

                while (true)
                {
                    var received = socket1.Receive(buffer);
                    if (received == 0)
                        return;

                    socket2.Send(buffer, received, SocketFlags.None);
                }
            }
            catch
            { }
        }
    }
}
