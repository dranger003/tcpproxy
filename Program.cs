using System;
using System.Net;

namespace tcpproxy
{
    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length != 2)
            {
                // tcpproxy.exe 127.0.0.1:12345 127.0.0.1:12346
                Console.WriteLine("Usage: tcpproxy.exe [listen] [connect]");
                return;
            }

            var listen = new IPEndPoint(IPAddress.Parse(args[0].Split(':')[0]), Int32.Parse(args[0].Split(':')[1]));
            var connect = new IPEndPoint(IPAddress.Parse(args[1].Split(':')[0]), Int32.Parse(args[1].Split(':')[1]));

            using (var proxy = new TcpProxy(listen, connect))
            {
                proxy.Start(8);
                Console.ReadKey(true);
            }
        }
    }
}
