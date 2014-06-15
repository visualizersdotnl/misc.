
// UDP multicast to TCP relay server for Linux/Ubuntu.

// Running Ubuntu? For the UDP (multicast) NIC, don't forget to:
// 1) Assign a static IP through the network manager.
// 2) As root: echo 0 > /proc/sys/net/ipv4/conf/all + <interface>/rp_filter
// 3) As super: route add -net 224.0.0.0 netmask 224.0.0.0 <interface>
// 
// Still not working? Read the manual instead.
// Or call Saul.

// To do (major):
// - Instead of multiple instances, instantiate multiple servers (*).

// To do (minor):
// - Unicast support.
// - Figure out what constant parameters (see below) are worth configuring manually.
// - Debug/Release in Makefile.
// - Rename RequestManager to Server?

// Bugs:
// - There's some issue with getting the same range of multicasts through different TCP NICs?

// Non-standard dependencies:
// - Boost.

// * Roadmap:
//   - Read conf. file with parameters.
//   - Resolve *all* NIC IPs at once prior to startup.
//   - Spawn desired number of servers, and either wait on global terminate or all servers.
//   - Logger now specifies the server, so I should probably parametrize LOG() a bit further to split messages up accordingly.

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <syslog.h>

#include <boost/signals2.hpp>
#include <boost/lexical_cast.hpp>
// #include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>
// #include <boost/thread/mutex.hpp>

// --------------------------------------------------------------------------------------------------------------------
// Constants.
// --------------------------------------------------------------------------------------------------------------------

// Time in microseconds before retrying to resolve NICs to IPs.
const size_t kResolveRetryWait = 1000000*5; 

// Time in microseconds before any connection is attempted to be restored.
const size_t kConnRetryWait = 1000000/2; 

// Amount of connections that the listener socket will accept at once.
const size_t kListenerQueueSize = 512;

// Timeout for UDP recv() in seconds.
const size_t kUDPTimeout = 1;

// Run verbose (per second statistics, not really recommended).
const bool kIsVerbose = false;

// Version strings.
const size_t kVersionMajor = 0;
const size_t kVersionMinor = 2;
const std::string kPlatform = "Ubuntu/x64";

// Common transfer rate units.
const size_t kKilobit = 125;
const size_t kMegabit = 1000*kKilobit;
const size_t kGigabit = 1000*kMegabit;

// Server configuration path.
const std::string kConfigPath = "/etc/dvbproxy.conf";

// PID file path (for daemon).
const std::string kPIDPath = "/var/run/dvbproxy.pid";


const std::string kNullIP = "0.0.0.0";

// --------------------------------------------------------------------------------------------------------------------
// Globals.
// --------------------------------------------------------------------------------------------------------------------

// Set to true to terminate server.
static bool s_terminate = false;

// Relay delete signal.
static boost::signals2::signal<void ()> s_sigKillRelays;

// BlockingRelay ref. count.
static size_t s_relayCount = 0;

// Ticks passed since main loop started.
static size_t s_ticksPassed = 0;

// Log mutex.
static pthread_mutex_t s_logMutex = PTHREAD_MUTEX_INITIALIZER;

// Process ID.
// Left uninitialized, filled in directly on startup.
static pid_t s_procID;

// TCP server IP & port (for logging purposes).
static std::string s_serverAddr = "";

// --------------------------------------------------------------------------------------------------------------------
// Local time helper functions.
// --------------------------------------------------------------------------------------------------------------------

inline const tm GetLocalTime()
{
	const time_t sysTime = time(nullptr);
	return *localtime(&sysTime);
}

inline const std::string FormatLocalTime()
{
	const tm t = GetLocalTime();
	const std::string readable(asctime(&t));
	return std::move(readable.substr(0, readable.length()-1)); // -1 is to chop off new line.
}

// --------------------------------------------------------------------------------------------------------------------
//
// Log facility.
// Use this macro for all output and let the user redirect to fit.
//
// Macro:
// - Accepts a string stream.
// - Takes hold of a global mutex.
// - Prefixes with TCP details, date and time.
//
// It isn't very efficient but at the rate we're logging that's no concern.
//
// --------------------------------------------------------------------------------------------------------------------

inline const std::string LogPrefix()
{
	std::stringstream stream;

	// 1. Process ID.
	stream << "[PID " << s_procID << ", ";

	// 2. TCP server details (if available).
	if (0 != s_serverAddr.length()) stream << "Server: " << s_serverAddr << ", ";

	// 3. Readable time stamp.
	stream << FormatLocalTime() << "] ";

	return std::move(stream.str());
}

#define LOG(Stream) pthread_mutex_lock(&s_logMutex); \
                    std::cout << LogPrefix() << Stream << std::endl; \
                    pthread_mutex_unlock(&s_logMutex);

// --------------------------------------------------------------------------------------------------------------------
// Socket helper functions.
// This stuff is easily reusable.
// --------------------------------------------------------------------------------------------------------------------

// Winsock commodities :)
typedef unsigned int SOCKET;
const unsigned int INVALID_SOCKET = -1;

static const std::string GetIPv4(const std::string &interface)
{
	SOCKET sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq nicDesc;
	nicDesc.ifr_addr.sa_family = AF_INET;
	strncpy(nicDesc.ifr_name, interface.c_str(), IFNAMSIZ-1);
	ioctl(sock_fd, SIOCGIFADDR, &nicDesc);
	close(sock_fd);
	const std::string IPv4(inet_ntoa(reinterpret_cast<sockaddr_in *>(&nicDesc.ifr_addr)->sin_addr));
	return std::move(IPv4);
}

inline void SetSocketBlock(SOCKET &sock_fd, bool isBlocking)
{
	int options = fcntl(sock_fd, F_GETFL);
	if (true == isBlocking)
		options &= ~O_NONBLOCK;
	else
		options |= O_NONBLOCK;
	fcntl(sock_fd, F_SETFL, options);
}

static SOCKET ConnectToMulticast(const std::string &interfaceIP, const std::string &bindIP, unsigned int port, const std::string &serverIP, unsigned int serverPort)
{
	// Create UDP socket.
	SOCKET sock_fd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (INVALID_SOCKET != sock_fd)
	{
		// "Share" socket address.
		int sockOpt = 1;
		if (0 == setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&sockOpt), sizeof(int)))
		{
			// Enlarge (or embiggen, if you will) recv. buffer.
			sockOpt = 1024*512; // 0x80000;
			if (0 == setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char *>(&sockOpt), sizeof(int)))
			{
				// Disable loopback.
				u_char sockOpt8 = 0;
				if (0 == setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &sockOpt8, sizeof(sockOpt8)))
				{
					// Bind to interface(s).
					sockaddr_in address;
					memset(&address, 0, sizeof(sockaddr_in));
					address.sin_family = AF_INET;
					address.sin_port = htons(port);
					address.sin_addr.s_addr = inet_addr(bindIP.c_str());
					int addrLen = sizeof(sockaddr_in);
					if (0 == bind(sock_fd, reinterpret_cast<sockaddr *>(&address), addrLen))
					{
						int result = -1;
						if (serverIP == "")
						{
							// Join regular multicast.
							ip_mreq multicast;
							multicast.imr_multiaddr.s_addr = inet_addr(bindIP.c_str());
							multicast.imr_interface.s_addr = ("" == interfaceIP) ? INADDR_ANY : inet_addr(interfaceIP.c_str());
							result = setsockopt(sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char *>(&multicast), sizeof(ip_mreq));
						}
						else
						{
							// Join IGMPv3 multicast.
							ip_mreq_source multicast;
							multicast.imr_multiaddr.s_addr = inet_addr(bindIP.c_str());
							multicast.imr_sourceaddr.s_addr = inet_addr(serverIP.c_str());
							multicast.imr_interface.s_addr = ("" == interfaceIP) ? INADDR_ANY : inet_addr(interfaceIP.c_str());
							if (0 == setsockopt(sock_fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, reinterpret_cast<char *>(&multicast), sizeof(ip_mreq_source)))
							{
								// Connect our socket.
								sockaddr_in address;
								memset(&address, 0, sizeof(sockaddr_in));
								address.sin_family = AF_INET;
								address.sin_port = htons(serverPort);
								address.sin_addr.s_addr = inet_addr(serverIP.c_str());
								int addrLen = sizeof(sockaddr_in);
								result = connect(sock_fd, reinterpret_cast<sockaddr *>(&address), addrLen);
							}
						}

						if (0 == result)
						{
							LOG("Joined multicast: " << bindIP.c_str() << ":" << port);
							if ("" != serverIP) LOG("w/IGMPv3: " << serverIP << ":" << serverPort);
							return sock_fd;
						}
					}
				}
			}
		}

		close(sock_fd);
	}

	LOG("Failed to join multicast: " << bindIP.c_str() << ":" << port << " (" << errno << ").");
	return INVALID_SOCKET;
}

static void DisconnectFromMulticast(SOCKET sock_fd, const std::string &interfaceIP, const std::string &bindIP, const std::string &serverIP)
{
	if ("" != serverIP)
	{
		// Drop IGMPv3 membership.
		ip_mreq_source multicast;
		multicast.imr_multiaddr.s_addr = inet_addr(bindIP.c_str());
		multicast.imr_sourceaddr.s_addr = inet_addr(serverIP.c_str());
		multicast.imr_interface.s_addr = ("" == interfaceIP) ? INADDR_ANY : inet_addr(interfaceIP.c_str());
		if (0 != setsockopt(sock_fd, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP, reinterpret_cast<char *>(&multicast), sizeof(ip_mreq_source)))
		{
			LOG("Failed to drop IGMPv3 (server: " << serverIP << ") multicast: " << bindIP << " (" << errno << ").");
		}
	}
	else
	{
		// Drop membership.
		ip_mreq multicast;
		multicast.imr_multiaddr.s_addr = inet_addr(bindIP.c_str());
		multicast.imr_interface.s_addr = ("" == interfaceIP) ? INADDR_ANY : inet_addr(interfaceIP.c_str());
		if (0 != setsockopt(sock_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<char *>(&multicast), sizeof(ip_mreq)))
		{
			LOG("Failed to drop multicast: " << bindIP << " (" << errno << ").");
		}
	}

	close(sock_fd);
}

static SOCKET CreateTCPListener(const std::string &interfaceIP, unsigned int port, int queueSize)
{
	// Create TCP socket.
	SOCKET sock_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (INVALID_SOCKET != sock_fd)
	{
		// Bind to local interface(s).
		sockaddr_in address;
		memset(&address, 0, sizeof(sockaddr_in));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = ("" == interfaceIP) ? INADDR_ANY : inet_addr(interfaceIP.c_str());
		int addrLen = sizeof(sockaddr_in);
		if (0 == bind(sock_fd, reinterpret_cast<sockaddr *>(&address), addrLen))
		{
			if (0 == listen(sock_fd, queueSize))
			{
				// Non-blocking.
				SetSocketBlock(sock_fd, false);

				LOG("Ready to accept incoming TCP connections on: " << interfaceIP << ":" << port);
				return sock_fd;
			}	
		}

		close(sock_fd);
	}

	LOG("Failed to create TCP listener socket on: " << interfaceIP << ":" << port);

	if (EADDRINUSE != errno)
	{
		LOG("Error: " << errno)
	}
	else
	{
		LOG("Error EADDRINUSE, socket lingers. Retry in a bit.");
	}

	return INVALID_SOCKET;
}

// --------------------------------------------------------------------------------------------------------------------
// Traffic container for statistics.
// Tailored to do exactly what's needed.
// --------------------------------------------------------------------------------------------------------------------

class Traffic
{
public:
	Traffic() : m_received(0), m_sent(0) {}
	Traffic(size_t received, size_t sent) : m_received(received) , m_sent(sent) {}

	void AddRecv(size_t bytes) { m_received += bytes; }
	const std::string GetRecv() const { return ToString(m_received); }

	void AddSent(size_t bytes) { m_sent += bytes; }
	const std::string GetSent() const { return ToString(m_sent); }

	const Traffic operator -(const Traffic &RHS)
	{
		return Traffic(m_received-RHS.m_received, m_sent-RHS.m_sent);
	}

private:
	size_t m_received;
	size_t m_sent;

	// Rounds a double to have a single decimal point (for display).
	static double Round(double value)
	{
		return floor(value*10.0 + 0.5)/10.0;
	}

	static const std::string ToString(size_t bytes)
	{
		std::stringstream stream;

		if (bytes < kKilobit)
		{
			stream << bytes << " bytes";
		}
		else if (bytes < kMegabit)
		{
			const double kilobits = (double) bytes / kKilobit;
			stream << Round(kilobits) << " kilobit";
//			stream << std::setprecision(2) << kilobits << " kilobit";
		}
		else if (bytes < kGigabit)
		{
			const double megabits = (double) bytes / kMegabit;
			stream << Round(megabits) << " megabit";
//			stream << std::setprecision(2) << megabits << " megabit";
		}
		else
		{
			const double gigabits = (double) bytes / kGigabit;
			stream << Round(gigabits) << " gigabit";
//			stream << std::setprecision(2) << gigabits << " gigabit";
		}

		return std::move(stream.str());
	}
};

// --------------------------------------------------------------------------------------------------------------------
// Blocking UDP->TCP relay.
// Should be set up with a valid multicast address and an open TCP socket ready to receive.
// Class will destroy itself as soon as the TCP socket closes.
// --------------------------------------------------------------------------------------------------------------------

class BlockingRelay : public boost::noncopyable
{
private:
	static const size_t kMTUSize = 1500;
	static const size_t kTSPacketSize = 188;

public:
	BlockingRelay(const std::string &interfaceIP, const std::string &bindIP, unsigned int port, const std::string &serverIP, unsigned int serverPort, SOCKET relaySocket, Traffic &traffic) :
		m_interfaceIP(interfaceIP)
,		m_bindIP(bindIP)
,		m_port(port)
,		m_serverIP(serverIP)
,		m_serverPort(serverPort)
,		m_udpSocket(INVALID_SOCKET)
, 		m_relaySocket(relaySocket)
,		m_traffic(traffic)
,		m_stopThread(false)
	{
		// Bind to global termination signal.
		m_killConnection = s_sigKillRelays.connect(boost::bind(&BlockingRelay::Terminate, this));

		// Spawn thread.
		pthread_create(&m_thread, NULL, ThreadFunc, this);

		++s_relayCount;
	}

	~BlockingRelay()
	{
		// Disconnect from global termination signal.
		m_killConnection.disconnect();

		m_stopThread = true;
		pthread_join(m_thread, NULL);

		// Close sockets if necessary.
		if (INVALID_SOCKET != m_udpSocket)   DisconnectFromMulticast(m_udpSocket, m_interfaceIP, m_bindIP, m_serverIP);
		if (INVALID_SOCKET != m_relaySocket) CloseRelaySocket();

		--s_relayCount;
	}

private:
	const std::string m_interfaceIP;
	const std::string m_bindIP;
	const unsigned int m_port;
	const std::string m_serverIP;
	const unsigned int m_serverPort;

	SOCKET m_udpSocket;
	SOCKET m_relaySocket;

	Traffic &m_traffic;

	bool m_stopThread;
	pthread_t m_thread;

	boost::signals2::connection m_killConnection;

	bool Connect()
	{
		m_udpSocket = ConnectToMulticast(m_interfaceIP, m_bindIP, m_port, m_serverIP, m_serverPort);
		return INVALID_SOCKET != m_udpSocket;
	}

	bool MulticastConnected() const { return INVALID_SOCKET != m_udpSocket; }

	void CloseRelaySocket()
	{
		close(m_relaySocket);
		m_relaySocket = INVALID_SOCKET;
		LOG("TCP relay connection closed for multicast: " << m_bindIP << ":" << m_port << " (" << errno << ").");
	}

	static void *ThreadFunc(void *parameter)
	{
		BlockingRelay *pInst = reinterpret_cast<BlockingRelay *>(parameter);
		return pInst->Thread();
	}

	void *Thread()
	{
		while (false == m_stopThread)
		{
			if (INVALID_SOCKET == m_relaySocket)
			{
				// Will invoke destructor.
				Terminate();
			}
			else
			{
				// Connected to UDP multicast?
				if (false == MulticastConnected())
				{
					if (false == Connect())
					{
						// Wait for next retry.
						usleep(kConnRetryWait);
					}
					else
					{
						// Set a predefined timeout so we won't get stuck in recvfrom() indefinitely.
						timeval timeout;
						timeout.tv_sec = kUDPTimeout;
						timeout.tv_usec = 0;
						setsockopt(m_udpSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
					}
				}
				else
				{
					// Receive UDP.
					char buffer[kMTUSize];
					sockaddr_storage address;
					socklen_t addrSize = sizeof(sockaddr_storage);
					const int result = recvfrom(m_udpSocket, buffer, kMTUSize, 0, reinterpret_cast<sockaddr *>(&address), &addrSize);
					if (-1 == result || 0 == result)
					{
						if (EAGAIN == errno || EWOULDBLOCK == errno)
						{
							// Timeout occured.
							LOG("Multicast starving: " << m_bindIP << ":" << m_port);

							// Check if relay socket is still alive (async.).
							SetSocketBlock(m_relaySocket, false);
							int bytes;
							const int result = recv(m_relaySocket, &bytes, sizeof(int), 0);
							if (-1 == result || 0 == result)
							{
								// No: terminate.
								CloseRelaySocket();
							}
							else
							{
								SetSocketBlock(m_relaySocket, true);
							}
						}
						else
						{
							// Something went haywire, close UDP socket.
							LOG("Connection to multicast lost: " << m_bindIP << ":" << m_port << " (" << errno << ").");
							DisconnectFromMulticast(m_udpSocket, m_interfaceIP, m_bindIP, m_serverIP);
							m_udpSocket = INVALID_SOCKET;

							// And wait for a bit.
							usleep(kConnRetryWait);
						}
					}
					else // Success!
					{
						// Keep track.
						m_traffic.AddRecv(result);

						const size_t chunkSize = result; // kTSPacketSize;
						size_t numPackets = result / chunkSize;
						const char *pPackets = buffer;
						while (numPackets)
						{
							// Relay chunk to TCP.
							const int result = send(m_relaySocket, pPackets, chunkSize, 0);
							if (-1 == result || 0 == result)
							{
								// Connection dropped: terminate.
								CloseRelaySocket();
								break;
							}
							else
							{
								// Keep track.
								m_traffic.AddSent(result);

								pPackets += chunkSize;
								--numPackets;
							}
						}
					}
				}
			}
		}

		return NULL;
	}

	void Terminate()
	{
		delete this;
	}
};

// --------------------------------------------------------------------------------------------------------------------
// RequestManager.
// Opens up a TCP listener socket to accept incoming connections.
// Incoming connections are expected to send a REST request for a multicast stream, which is then created.
// Other options include server termination and real-time performance statistics.
// --------------------------------------------------------------------------------------------------------------------

static void SplitString(std::string string, const std::string &delimiter, std::vector<std::string> &results)
{
	results.clear();

	size_t iSplit;
	while (std::string::npos != (iSplit = string.find_first_of(delimiter)))
	{
		if (iSplit > 0)
		{
			results.push_back(string.substr(0, iSplit));
		}

		string = string.substr(iSplit+1);
	}

	if (0 != string.length())
		results.push_back(string);
}

class RequestManager : public boost::noncopyable
{
public:
	RequestManager(const std::string &tcpInterfaceIP, unsigned int port, const std::string &udpInterfaceIP) :
		m_interfaceIP(tcpInterfaceIP)
,		m_port(port)
,		m_udpInterfaceIP(udpInterfaceIP)
,		m_listenSocket(INVALID_SOCKET)
,		m_stopThread(false)
,		m_thread(0)
	{
	}

	~RequestManager() 
	{
		pthread_join(m_thread, NULL);

		// Close listener socket, if necessary.
		if (true == ListenerConnected()) close(m_listenSocket);
	}

	bool Start()
	{
		if (0 == m_thread)
		{
			if (true == Connect())
			{
				pthread_create(&m_thread, nullptr, ThreadFunc, this);
				return true;
			}
		}

		return false;
	}

	void Stop() { m_stopThread = true; }
	bool IsStopped() const { return m_stopThread; }

	const Traffic &GetTraffic() 
	{ 
		return m_traffic; 
	}

	// Must be called approx. every second.
	void Tick()
	{
		m_curTraffic = m_traffic - m_prevTraffic;
		m_prevTraffic = m_traffic;
	}

private: // Just some private data, data for money...
	const std::string &m_interfaceIP;
	const unsigned int m_port;
	const std::string &m_udpInterfaceIP;

	SOCKET m_listenSocket;

	bool m_stopThread;
	pthread_t m_thread;

	// Local statistics.
	Traffic m_traffic;
	Traffic m_prevTraffic;
	Traffic m_curTraffic;

	bool Connect()
	{
		if (true == ListenerConnected()) close(m_listenSocket);		
		m_listenSocket = CreateTCPListener(m_interfaceIP, m_port, kListenerQueueSize);
		return INVALID_SOCKET != m_listenSocket;
	}

	bool ListenerConnected() const { return INVALID_SOCKET != m_listenSocket; }

	static void *ThreadFunc(void *parameter)
	{
		RequestManager *pInst = reinterpret_cast<RequestManager *>(parameter);
		return pInst->Thread();
	}

	void *Thread()
	{
		while (false == m_stopThread)
		{
			if (true == ListenerConnected())
			{
				// 1. Try and see if someone's trying to connect.
				sockaddr_in clientAddr;
				socklen_t addrSize = sizeof(sockaddr_in);
				SOCKET relaySocket = accept(m_listenSocket, reinterpret_cast<sockaddr *>(&clientAddr), &addrSize);
				if (INVALID_SOCKET != relaySocket)
				{
					// Yes!
					std::string clientIP(inet_ntoa(clientAddr.sin_addr));
					LOG("Incoming TCP connection from: " << clientIP);

					// Receive and parse REST, please and thank you.
					char buffer[2048];
					const int result = recv(relaySocket, buffer, 2048, 0);
					if (-1 == result || 0 == result)
					{
						LOG("HTTP request not received from: " << clientIP);
						close(relaySocket);
					}
					else
					{
						bool closeSocket = true;

						// Parse the REST for information on the requested multicast.
						// This is a little hairy, but it gets the job done.
						const std::string REST(buffer, result);
						const size_t getPos = REST.find("GET");
						if (getPos != std::string::npos)
						{
							const size_t lineBreakPos = REST.substr(getPos, std::string::npos).find("\n");
							if (lineBreakPos != std::string::npos)
							{
								const std::string GET = REST.substr(getPos, lineBreakPos);
								std::vector<std::string> splits;
								SplitString(GET, " ", splits);

								if (3 >= splits.size())
								{
									SplitString(splits[1], "/", splits);
									const std::string &command = splits[0];
									if ("terminate" == command)
									{
										// Terminate server.
										LOG("Server terminated by HTTP request.");
										m_stopThread = true;

										std::stringstream htmlStream;
										htmlStream << MakeHTMLResponseHeader();
										htmlStream << "<html><body><h1>DVBProxy server instance terminated.</h1></body></html>";

										// Send confirmation (unchecked).
										send(relaySocket, htmlStream.str().c_str(), htmlStream.str().length(), 0);
									}
									else if ("performance" == command)
									{
										// Report performance statistics.
										LOG("Client requested performance statistics.");

										const std::string recvStr = m_curTraffic.GetRecv() + "/Sec.";
										const std::string sentStr = m_curTraffic.GetSent() + "/Sec.";

										std::stringstream htmlStream;
										htmlStream << MakeHTMLResponseHeader();
										htmlStream << "<html><head><meta http-equiv=\"refresh\" content=\"5\"><style>body{background-color: #11a9e2;background-image: -webkit-gradient(linear, 0 0, 0 100%, from(#0d86b3), to(#11a9e2));background-image: -moz-linear-gradient(#0d86b3, #11a9e2);background-repeat: no-repeat;padding: 20px;text-rendering: optimizeLegibility;font: 14px/20px \"Helvetica Neue\", Helvetica, Arial, sans-serif;text-shadow: 0 1px 1px rgba(0, 0, 0, 0.25);}h1, h2, p, span{color: #fff;color: rgba(255, 255, 255, 0.75);text-align: center;text-shadow: 0 1px 1px rgba(0, 0, 0, 0.25);}h1{margin: 10;font: bold 70px/1 \"Helvetica Neue\", Helvetica, Arial, sans-serif;color: #fff;text-shadow: 0 1px 0 #cccccc, 0 2px 0 #c9c9c9, 0 3px 0 #bbbbbb, 0 4px 0 #b9b9b9, 0 5px 0 #aaaaaa, 0 6px 1px rgba(0, 0, 0, 0.1), 0 0 5px rgba(0, 0, 0, 0.1), 0 1px 3px rgba(0, 0, 0, 0.3), 0 3px 5px rgba(0, 0, 0, 0.2), 0 5px 10px rgba(0, 0, 0, 0.25), 0 10px 10px rgba(0, 0, 0, 0.2), 0 20px 20px rgba(0, 0, 0, 0.15);-webkit-transition: .2s all linear;}span{color: rgba(255, 255, 255, 1);}h1{text-shadow: 0 1px 0 #ccc, 0 2px 0 #c9c9c9, 0 3px 0 #bbb, 0 4px 0 #b9b9b9, 0 5px 0 #aaa, 0 6px 1px rgba(0,0,0,.1), 0 0 5px rgba(0,0,0,.1), 0 1px 3px rgba(0,0,0,.3), 0 3px 5px rgba(0,0,0,.2), 0 5px 10px rgba(0,0,0,.25), 0 10px 10px rgba(0,0,0,.2), 0 20px 20px rgba(0,0,0,.15);}</style><head><body><h1>DVBProxy (C)2014 RTSS B.V.</h1><hr><h2>Current UDP traffic: <span>" << recvStr << "</span> | Current TCP traffic: <span>" << sentStr << "</span></h2></body></html>";

										// Send information (unchecked).
										send(relaySocket, htmlStream.str().c_str(), htmlStream.str().length(), 0);
									}
									else if ("multicast" == command)
									{
										BlockingRelay *pInst = nullptr;

										const std::string &bindIP = splits[1];
										const std::string &port = splits[2];

										// Got IGMPv3 address?
										if (5 == splits.size())
										{
											const std::string &serverIP = splits[3];
											const std::string &serverPort = splits[4];

											// Instantiate BlockingRelay w/IGMPv3.
											pInst = new BlockingRelay(m_udpInterfaceIP, bindIP, boost::lexical_cast<unsigned int>(port), serverIP, boost::lexical_cast<unsigned int>(serverPort), relaySocket, m_traffic);
										}
										else
										{
											// Instantiate BlockingRelay.
											pInst = new BlockingRelay(m_udpInterfaceIP, bindIP, boost::lexical_cast<unsigned int>(port), "", 0, relaySocket, m_traffic);
										}

										closeSocket = nullptr == pInst;
									}
									else if ("odelay" == command)
									{
										LOG("Say what?");

										// Easter egg :-)
										std::stringstream htmlStream;
										htmlStream << MakeHTMLResponseHeader();
										htmlStream << "<html><body><img src=""http://coolalbumreview.com/wp-content/uploads/2010/12/beck.jpg""></img></body></html>";
										
										// Send information (unchecked).
										send(relaySocket, htmlStream.str().c_str(), htmlStream.str().length(), 0);
									}
									else
									{
										// Invalid command specified.
										std::stringstream htmlStream;
										htmlStream << MakeHTMLResponseHeader();
										htmlStream << "<html><head><style>body{background-color: #8A0808;padding: 20px;text-rendering: optimizeLegibility;font: 14px/20px \"Helvetica Neue\", Helvetica, Arial, sans-serif;text-shadow: 0 1px 1px rgba(0, 0, 0, 0.25);}h1, h2, p, span{color: #fff;color: rgba(255, 255, 255, 1);text-align: center;text-shadow: 0 1px 1px rgba(0, 0, 0, 0.25);}h1{margin: 10;font: bold 70px/1 \"Helvetica Neue\", Helvetica, Arial, sans-serif;color: #fff;text-shadow: 0 1px 0 #cccccc, 0 2px 0 #c9c9c9, 0 3px 0 #bbbbbb, 0 4px 0 #b9b9b9, 0 5px 0 #aaaaaa, 0 6px 1px rgba(0, 0, 0, 0.1), 0 0 5px rgba(0, 0, 0, 0.1), 0 1px 3px rgba(0, 0, 0, 0.3), 0 3px 5px rgba(0, 0, 0, 0.2), 0 5px 10px rgba(0, 0, 0, 0.25), 0 10px 10px rgba(0, 0, 0, 0.2), 0 20px 20px rgba(0, 0, 0, 0.15);-webkit-transition: .2s all linear;}h1{text-shadow: 0 1px 0 #ccc, 0 2px 0 #c9c9c9, 0 3px 0 #bbb, 0 4px 0 #b9b9b9, 0 5px 0 #aaa, 0 6px 1px rgba(0,0,0,.1), 0 0 5px rgba(0,0,0,.1), 0 1px 3px rgba(0,0,0,.3), 0 3px 5px rgba(0,0,0,.2), 0 5px 10px rgba(0,0,0,.25), 0 10px 10px rgba(0,0,0,.2), 0 20px 20px rgba(0,0,0,.15);}</style><head><body><h1>DVBProxy (C)2014 RTSS B.V.</h1><hr><h2>Invalid command specified!</h2></body></html>";
										
										// Send information (unchecked).
										send(relaySocket, htmlStream.str().c_str(), htmlStream.str().length(), 0);
									}
								}
							}
						}

						if (true == closeSocket)
						{
							// Response is either handled immediately or request was invalid.
							close(relaySocket);
						}
					}
				}
				else
				{
					if (EAGAIN == errno || EWOULDBLOCK == errno)
					{
						// Nobody's knocking, so sit around for a bit.
						usleep(kConnRetryWait);
					}
					else
					{
						// I'll assume the listener socket is broken, so we'll attempt to repair it.
						close(m_listenSocket);
						m_listenSocket = INVALID_SOCKET;
					}
				}
			}
			else
			{
				if (false == Connect())
				{
					// Wait for next retry.
					usleep(kConnRetryWait);
				}
			}
		}

		return NULL;
	}

	const std::string MakeHTMLResponseHeader()
	{
		// #FIXME: check if this is more or less complete, and if a correct date would be necessary.
		std::string header;
		header  = "HTTP/1.1 200 OK\n";
		header += "Server: DVBProxy\n";
		header += "Connection: close\n";
		header += "Content-Type: text/html; charset=ISO-8859-1\n";
		header += "\n";
		return std::move(header);
	}
};

// --------------------------------------------------------------------------------------------------------------------
// Configuration parser.
// --------------------------------------------------------------------------------------------------------------------

class Config
{
public:
	class Server
	{
	public:
		Server(const std::string &udpInterface, const std::string &tcpInterface, unsigned int port);
		~Server() {}

	private:
		// UDP input.
		const std::string m_udpInterface;
		std::string m_udpIP;

		// TCP output.
		const std::string m_tcpInterface;
		const unsigned int m_port;
		std::string m_tcpIP;
	};

	Config(const std::string &path) :
		m_path(path)
	{
	}

	~Config() {}

	bool Parse() 
	{ 
		return false; 
	}

private:
	const std::string m_path;
} static s_config(kConfigPath);

// --------------------------------------------------------------------------------------------------------------------
// Main.
// --------------------------------------------------------------------------------------------------------------------

static bool s_terminateDirectly = true;

static void SigTerminate(int sigNum)
{
	const std::string termStr = "Received termination signal.";
	LOG(termStr);

	if (false == s_terminateDirectly)
	{
		// Terminate gracefully.
		s_terminate = true;
	}
	else
	{
		// Just sod off.
		exit(EXIT_FAILURE);
	}
}

int main(int argC, char **argV)
{
	s_procID = getpid();

	// Define our default signal handler (SigTerminate()).
	struct sigaction sigAction;
	sigAction.sa_handler = SigTerminate;
	sigemptyset(&sigAction.sa_mask);
	sigAction.sa_flags = 0;

	// And couple it to signals that are reason for termination.
	sigaction(SIGHUP,  &sigAction, NULL);
	sigaction(SIGTERM, &sigAction, NULL);
	sigaction(SIGINT,  &sigAction, NULL);

	LOG("DVBProxy (" << kPlatform << ") " << kVersionMajor << "." << kVersionMinor << " - UDP multicast to TCP relay server.");
	LOG("(C) " << __DATE__ <<  " RTSS B.V.");

	// FIXME: lame expiration check for BETA.
	const tm t = GetLocalTime();
	if (t.tm_mon > 4 || t.tm_year != 114)
	{
		LOG("BETA trial period expired (May 2014).");
		return 1;
	}

	if (0 != getuid())
	{
		LOG("Please run this program as root: sudo ./dvbproxy");
		return 1;
	}

	// REPLACING...
	//

	if (false == s_config.Parse())
	{
		return 1;		
	}

	// Replace following with proper configuration parse:	
	//

	if (argC <= 3)
	{
		LOG("Please specify command line: <UDP adapter name> <TCP adapter name> <TCP port>");
		return 1;
	}

	// Grab command line paramaters.
	const std::string udpInterface = argV[1];
	const std::string tcpInterface = argV[2];
	const unsigned int tcpPort = boost::lexical_cast<unsigned int>(argV[3]);

	// Crude check.
	// #FIXME: Check if given NIC names actually exist!
	if ("" == udpInterface || "" == tcpInterface || 0 == tcpPort)
	{
		LOG("Invalid command line parameters specified.");
		return 1;
	}

	// ^^
	
	int hPIDFile = -1;

	// Daemonize.
	{
		s_procID = fork();

		// Set file permissions.
		umask(27); 

		if (-1 == s_procID)
		{
			LOG("Could not fork process.");
			exit(EXIT_FAILURE);
		}
		else if (s_procID > 0)
		{
			// Terminate main process.
			exit(EXIT_SUCCESS);
		}
		else 
		{
			// We're in the child process now, reacquire PID and let's roll.
			s_procID = getpid();
			LOG("Process daemonized.");
		}

		// Close std. input (0), but keep std. output & input (1 & 2) open.
		close(0);

		// Take over from parent as process group leader.
		const pid_t sessionID = setsid();
		if (sessionID < 0)
		{
			LOG("Unable to acquire child session ID.");
			return EXIT_FAILURE;
		}

		LOG("Session ID: " << sessionID);

		// Attempt to change directory.
		if (chdir("/") < 0)
		{
			LOG("Unable to find directory.");
			return EXIT_FAILURE;
		}

		// Ensure only one copy of PID file exists.
		hPIDFile = open(kPIDPath.c_str(), O_RDWR|O_CREAT, 0644);
		if (-1 == hPIDFile)
		{
			LOG("Could not open PID file.");
			return EXIT_FAILURE;
		}
	 
		// Attempt to lock PID file.
		if (-1 == lockf(hPIDFile, F_TLOCK, 0))
		{
			LOG("Could not lock PID file. DVBProxy can only run 1 instance at a time!");
			return EXIT_FAILURE;
		}
	 
		// Get and format PID, then write it to it's file.
		char PID[16];
		sprintf(PID, "%d\n", getpid());
		const size_t result = write(hPIDFile, PID, strlen(PID));
		if (strlen(PID) != result)
		{
			LOG("Could not write to PID file.");
			return EXIT_FAILURE;
		}

		// No standard input for daemon.
		if (open("/dev/null", O_RDONLY) < 0)
		{
			LOG("Unable to open /dev/null w/O_RDONLY.");
			return EXIT_FAILURE;
		}
	}

	// From this point onward, we want to terminate gracefully instead of directly.
	s_terminateDirectly = false;

	// Create log mutex.
	pthread_mutex_init(&s_logMutex, nullptr);

	// Block a few signals:
	{
		sigset_t newSigSet;
		sigemptyset(&newSigSet);

		// Child - i.e. we don't need to wait for it.
		sigaddset(&newSigSet, SIGCHLD); 

		// Stop & continue.
		sigaddset(&newSigSet, SIGTSTP); 
		sigaddset(&newSigSet, SIGSTOP); 
		sigaddset(&newSigSet, SIGCONT); 

		// Background writes & reads.
		sigaddset(&newSigSet, SIGTTOU);
		sigaddset(&newSigSet, SIGTTIN);

		sigprocmask(SIG_BLOCK, &newSigSet, NULL);
	}

	// Two booleans used to compose a sensible exit status.
	bool IPsResolved = false;
	bool serverStarted = false;

	// Now try to resolve NICs to valid IPs.
	LOG("Resolving NIC IPs...");
	std::string udpInterfaceIP = kNullIP;
	std::string tcpInterfaceIP = kNullIP;
	while (false == s_terminate && false == IPsResolved)
	{
		if (kNullIP == udpInterfaceIP) udpInterfaceIP = GetIPv4(udpInterface);
		if (kNullIP == tcpInterfaceIP) tcpInterfaceIP = GetIPv4(tcpInterface);

		if (kNullIP != udpInterfaceIP && kNullIP != tcpInterfaceIP)
		{
			LOG("Resolved UDP NIC " << udpInterface << " to: " << udpInterfaceIP);
			LOG("Resolved TCP NIC " << tcpInterface << " to: " << tcpInterfaceIP);
			IPsResolved = true;
		}
		else
		{
			LOG("Unable to resolve both NICs, retrying in " << kResolveRetryWait/1000000 << " seconds.");
			usleep(kResolveRetryWait);
		}
	}

	// If resolved, kick off an actual session.
	if (true == IPsResolved)
	{
		// From now on also display TCP server details in log output.
		s_serverAddr  = tcpInterfaceIP + ":";
		s_serverAddr += boost::lexical_cast<std::string>(tcpPort);

		// Attempt to start request manager.
		RequestManager reqMan(tcpInterfaceIP, tcpPort, udpInterfaceIP);
		if (true == reqMan.Start())
		{
			// Mark server startup.
			LOG("RequestManager (server) launched.");
			serverStarted = true;

			// Do not terminate on broken pipe.
			// This can occur when the client bails midst our send()/write() call, and we handle that.
			signal(SIGPIPE, SIG_IGN);

			// Global statistics.
			Traffic globTraffic;
			Traffic prevGlobTraffic;

			// Go!
			while (false == s_terminate && false == reqMan.IsStopped())
			{
				// Sit around for about a second.
				usleep(1000000);
				++s_ticksPassed;

				// Tick server.
				reqMan.Tick();

				// Update global statistics.
				globTraffic = reqMan.GetTraffic();
				const Traffic curTraffic = globTraffic - prevGlobTraffic;

				if (true == kIsVerbose)
				{
					if (0 != s_relayCount)
					{
						LOG("Speed (UDP): " << curTraffic.GetRecv() << "/Sec." << " Speed (TCP): " << curTraffic.GetSent() << "/Sec.");
					}

					LOG("Active relays: " << s_relayCount);
				}

				prevGlobTraffic = globTraffic;
			}

			// Stop taking requests (if still necessary).
			reqMan.Stop();

			// Terminate all BlockingRelay instances.
			s_sigKillRelays();

			// Some more statistics.
			LOG("Total UDP traffic: " << globTraffic.GetRecv() << ".");
			LOG("Total TCP traffic: " << globTraffic.GetSent() << ".");
			LOG("Leaking relays: "    << s_relayCount);
		}
	}

	pthread_mutex_destroy(&s_logMutex);

	// Wipe PID file.
	if (-1 != hPIDFile) close(hPIDFile);
	std::remove(kPIDPath.c_str());

	return (IPsResolved && serverStarted) ? EXIT_SUCCESS : EXIT_FAILURE;
}
