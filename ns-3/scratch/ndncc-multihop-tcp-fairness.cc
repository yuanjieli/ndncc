/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2012 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Yuanjie Li <yuanjie.li@cs.ucla.edu>
 */
// ndncc-multihop-tcp-fairness.cc Show whether multiple TCP flows outperforms NDNCC
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"
#include <cstdio>
#include <set>
#include <ctime>
#include <list>
#include <string>
#include <map>

using namespace ns3;

class MyApp : public Application 
{
public:

  MyApp ();
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0), 
    m_peer (), 
    m_packetSize (0), 
    m_nPackets (0), 
    m_dataRate (0), 
    m_sendEvent (), 
    m_running (false), 
    m_packetsSent (0)
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void 
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void 
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if (++m_packetsSent < m_nPackets || m_nPackets==0)
    {
      ScheduleTx ();
    }
}

void 
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

struct throughput_info{
	uint32_t totalbytes;
	Time start;
};

static std::map<std::string, throughput_info> mapThroughput;
static void
ShowThroughput (std::string str, Ptr<const Packet> packet, const Address &)
{
	std::map<std::string, throughput_info>::iterator it = mapThroughput.find(str);
	if(it == mapThroughput.end())
	{
		throughput_info new_info;
		new_info.start = Simulator::Now ();
		new_info.totalbytes = packet->GetSize ();
		mapThroughput[str] = new_info;
	}
	else
	{
		it->second.totalbytes += packet->GetSize ();
		Time interval = Simulator::Now () - it->second.start;
		if( interval >= Seconds(1))	//calculate throughput every second
		{
			//Get the node id
			unsigned pos = str.find("/ApplicationList");
			std::string id = str.substr(10,pos-10);
			
			double throughput = it->second.totalbytes*8.0/1000000.0/interval.GetSeconds();
			NS_LOG_UNCOND(id<<" "<<Simulator::Now().GetSeconds()<<" "<<throughput);
			
			//reset structure
			it->second.totalbytes = 0;
			it->second.start = Simulator::Now();		
		}
	}	
}

/*static void
ShowCwnd (std::string str, uint32_t oldval, uint32_t newval)
{
	
	NS_LOG_UNCOND(str<<" "<<Simulator::Now().GetSeconds ()<<" "<<oldval);
}*/

int 
main (int argc, char *argv[])
{
	int simulation_time = 400;
	//int no_flow = 2;	//number of DATA flows, at most 8 (we have 16 servers)
	srand(time(0));
  uint16_t port = 10000;  // well-known echo port number
  
  
  CommandLine cmd;
  cmd.Parse (argc, argv);
  
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));  
	
	//Read topology from BCube
	AnnotatedTopologyReader topologyReader ("", 25);
  topologyReader.SetFileName ("src/ndnSIM/examples/topologies/multihop.txt");
  NodeContainer nodes = topologyReader.Read ();
  
  InternetStackHelper internet;
  internet.InstallAll ();
  
  //Note: topologyReader only supports /24 netmask. So #nodes are limited!!!
  //Can change code later by referring topologyReader.cc
 	topologyReader.AssignIpv4Addresses("10.1.1.0"); 
  
 	
 	//Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  
  //Add packet sinks here      		
	PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
	ApplicationContainer sinkApps = packetSinkHelper.Install (Names::Find<Node> ("C1"));
	sinkApps.Start (Seconds (0.0));
	sinkApps.Stop (Seconds (simulation_time));
	sinkApps = packetSinkHelper.Install (Names::Find<Node> ("C2"));
	sinkApps.Start (Seconds (0.0));
	
				 
  	
  Config::Connect("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx", MakeCallback (&ShowThroughput));

  //Manual configuration
  Ptr<Node> server = Names::Find<Node> ("P1");
  Ptr<Node> client = Names::Find<Node> ("C1");  		  	
  Ptr<Ipv4> ipv4 = client->GetObject<Ipv4>();
  Ipv4Address client_addr = ipv4->GetAddress(1,0).GetLocal(); //randomly choose one interface
  	
  TypeId tid = TypeId::LookupByName("ns3::TcpSocketFactory");  
  Ptr<Socket> serversocket = Socket::CreateSocket(server, tid);  
  InetSocketAddress clientAddr = InetSocketAddress(client_addr, port);  
    
  Ptr<MyApp> app = CreateObject<MyApp> ();
	app->Setup (serversocket, clientAddr, 1140, 0, DataRate ("1Mbps"));	//0 means infinite packets
	server->AddApplication (app);
	app->SetStartTime (Seconds (0));
	app->SetStopTime (Seconds (simulation_time));
	
	//serversocket->TraceConnect ("CongestionWindow", "0", MakeCallback (&ShowCwnd));
	
	
	server = Names::Find<Node> ("P1");
  client = Names::Find<Node> ("C2");  		  	
  ipv4 = client->GetObject<Ipv4>();
  client_addr = ipv4->GetAddress(1,0).GetLocal(); //randomly choose one interface
  	
  tid = TypeId::LookupByName("ns3::TcpSocketFactory");  
  serversocket = Socket::CreateSocket(server, tid);  
  clientAddr = InetSocketAddress(client_addr, port);  
    
  app = CreateObject<MyApp> ();
	app->Setup (serversocket, clientAddr, 1140, 0, DataRate ("1Mbps"));	//0 means infinite packets
	server->AddApplication (app);
	app->SetStartTime (Seconds (0));
	app->SetStopTime (Seconds (simulation_time));
	
	//serversocket->TraceConnect ("CongestionWindow", "1", MakeCallback (&ShowCwnd));
	
	server = Names::Find<Node> ("P2");
  client = Names::Find<Node> ("C1");  		  	
  ipv4 = client->GetObject<Ipv4>();
  client_addr = ipv4->GetAddress(1,0).GetLocal(); //randomly choose one interface
  	
  tid = TypeId::LookupByName("ns3::TcpSocketFactory");  
  serversocket = Socket::CreateSocket(server, tid);  
  clientAddr = InetSocketAddress(client_addr, port);  
    
  app = CreateObject<MyApp> ();
	app->Setup (serversocket, clientAddr, 1140, 0, DataRate ("1Mbps"));	//0 means infinite packets
	server->AddApplication (app);
	app->SetStartTime (Seconds (0));
	app->SetStopTime (Seconds (simulation_time));
	
	//serversocket->TraceConnect ("CongestionWindow", "2", MakeCallback (&ShowCwnd));
	
	
  
  
  Simulator::Stop (Seconds (simulation_time));
  Simulator::Run ();
  Simulator::Destroy ();
  	
  
  	
  	
  return 0;
}



