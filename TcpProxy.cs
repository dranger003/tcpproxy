using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace tcpproxy
{
    public class TcpProxy : IDisposable
    {
        private bool _disposed = false;
        private List<IPEndPoint> _endpoints = new List<IPEndPoint>();
        private List<Socket> _sockets = new List<Socket>();

        public TcpProxy(IPEndPoint listen, IPEndPoint connect)
        {
            _endpoints.AddRange(new[] { listen, connect });
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
                return;

            if (disposing)
            {
                // Managed
                Stop();
            }

            // Unmanaged

            _disposed = true;
        }

        public void Start(int backlog = 0)
        {
            new Thread(
                () =>
                {
                    using (var listen = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
                    {
                        _sockets.Add(listen);

                        listen.Bind(_endpoints[0]);
                        listen.Listen(backlog);

                        while (true)
                        {
                            try
                            {
                                var accept = listen.Accept();
                                var connect = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);

                                try
                                {
                                    connect.Connect(_endpoints[1]);

                                    _sockets.AddRange(new[] { accept, connect });

                                    new Thread(
                                        (object state) =>
                                        {
                                            using (Socket socket1 = ((Socket[])state)[0], socket2 = ((Socket[])state)[1])
                                                _Run(socket1, socket2);
                                        }
                                    ).Start(new[] { accept, connect });
                                }
                                catch
                                {
                                    connect.Dispose();
                                    accept.Dispose();
                                }
                            }
                            catch
                            {
                                break;
                            }
                        }
                    }
                }
            ).Start();
        }

        public void Stop()
        {
            _sockets.ForEach(x => x.Close());
            _sockets.Clear();
        }

        private void _Run(Socket socket1, Socket socket2)
        {
            var thread = new Thread(() => { _Replicate(socket1, socket2); });
            thread.Start();

            _Replicate(socket2, socket1);

            thread.Join();
        }

        private void _Replicate(Socket socket1, Socket socket2)
        {
            var buffer = new byte[524288];
            while (true)
            {
                try
                {
                    var received = socket2.Receive(buffer);
                    if (received == 0)
                        throw new Exception();

                    for (var sent = 0; sent != received; sent += socket1.Send(buffer, sent, received - sent, SocketFlags.None)) ;
                }
                catch
                {
                    break;
                }
            }

            socket1.Close();
            socket2.Close();
        }
    }
}
