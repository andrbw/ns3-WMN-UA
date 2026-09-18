/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* Experiment to compare the performance of Aloha, CSMA and DCF protocols
 * Network topology:
 * Ad-hoc wireless (Wi-Fi based) network with (numOfStations+1) nodes. One node has packet sink
 * application installed that receives packets from UDP echo client application installed on each
 * of the other numOfStations nodes. The client application in this experiment starts at simulation time 2
 * and will stop at simulation time defined in variable simTime.
 *
 * Default params:
 * 2 nodes: server and client
 * client node generates Poisson flow, payload = 1000 bytes, average inter-packet interval = 100 ms
 * MAC protocol: Aloha (pure), without acknowledgements
 *
 * Output:
 * File 'result.txt' located in the working directory.
 * File contents: <numOfStations>\t<throughput, Mbps>
 * Default contents: 1\t0.0758333
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/neighbor-cache-helper.h"
#include "ns3/multi-model-spectrum-channel.h"

// WiFiViz lives in an optional contrib module (contrib/wifiviz)
// alongside ns-3. Pull it in only when its header is available.
// The lab builds even when the module is missing.
#if __has_include("ns3/wifiviz.h")
#include "ns3/wifiviz.h"
#define HAS_WIFIVIZ 1
#endif

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("AlohaVsDcf");

uint32_t RcvPktCount = 0;

void
ReceiveTrace (Ptr<const Packet> pkt, const Address & addr)
{
  RcvPktCount++;
}

void
PrintProgress (Time simTime)
{
  uint32_t progress = Simulator::Now ().GetSeconds () / simTime.GetSeconds () * 100.;
  std::cerr << '\r' << progress << "%, elapsed " << Simulator::Now ().GetSeconds () << "s out of " << simTime.GetSeconds ();
  Simulator::Schedule (Seconds (1), &PrintProgress, simTime);
}

int main (int argc, char *argv[])
{
  Time simTime = Seconds(50);

  uint32_t numOfStations = 1; 
  uint32_t packetSize = 1000; // bytes
  Time packetInterval = MilliSeconds(100);

  Time clientStart = Seconds(2);
  Time serverStart = Seconds(1);
 
  bool collectPcap = true;
  std::string outFileName = "result.txt";

  std::string protocol = "aloha";
  bool useAck = false;

  bool enableViz = false;
  bool launchViewer = false;

  CommandLine cmd;

  cmd.AddValue ("simTime", "simulation time", simTime);

  cmd.AddValue ("numOfStations", "number of client stations", numOfStations);
  cmd.AddValue ("packetSize", "size of application payload, bytes", packetSize);
  cmd.AddValue ("interval", "average interval between packets", packetInterval);
  cmd.AddValue ("protocol", "MAC protocol: aloha, csma or dcf", protocol);
  cmd.AddValue ("useAck", "if true - Aloha/CSMA frames are acknowledged and retransmitted after a "
                          "random delay (ignored if protocol is dcf)", useAck);

  cmd.AddValue ("collectPcap", "turn on PCAP traces collection", collectPcap);
  cmd.AddValue ("outFileName", "out file name", outFileName);

  cmd.AddValue ("enableViz", "collect WiFiViz records for the timeline viewer "
                             "(requires the wifiviz module in contrib)", enableViz);
  cmd.AddValue ("launchViewer", "start the WiFiViz viewer together with the run "
                                "(requires the wifiviz module in contrib)", launchViewer);

  cmd.Parse (argc, argv);

  NS_ABORT_MSG_UNLESS (protocol == "aloha" || protocol == "csma" || protocol == "dcf",
                       "Unknown protocol '" << protocol << "', expected aloha, csma or dcf");

  // disable fragmentation for frames below 2200 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
  // turn off fragmentation at IP layer
  Config::SetDefault ("ns3::WifiNetDevice::Mtu", StringValue ("2200"));
  // turn off RTS/CTS for frames below 2200 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));

  //Allow only one transmission attempt per frame at the Wi-Fi level. With Aloha/CSMA
  //acknowledgements enabled, this makes the frame exchange manager report a missed Ack right
  //away, so that retransmissions are scheduled by AdhocAlohaMac (after a random delay) rather
  //than by the DCF.
  Config::SetDefault ("ns3::WifiMac::FrameRetryLimit", UintegerValue (1));

  // Aloha and CSMA run their own channel access procedure in the MAC high, so the DCF
  // backoff is disabled.
  if (protocol != "dcf")
    {
      Config::SetDefault ("ns3::Txop::DisableBackoff", BooleanValue (true));
    }

  NodeContainer serverStation;
  serverStation.Create(1);
  NodeContainer clientStations;
  clientStations.Create(numOfStations);

  NodeContainer allStations;
  allStations.Add(serverStation);
  allStations.Add(clientStations);

  // The below set of helpers will help us to put together the wifi NICs we want
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211a);
  SpectrumWifiPhyHelper wifiPhy;
  // the lab was calibrated against the NIST error rate model, not the ns-3 default
  wifiPhy.SetErrorRateModel ("ns3::NistErrorRateModel");
  Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();
  wifiPhy.SetChannel (channel);

  WifiMacHelper wifiMac;
  // Set adhoc mode and select the MAC protocol
  if (protocol == "dcf")
    {
      wifiMac.SetType ("ns3::AdhocWifiMac", "QosSupported", BooleanValue (false));
    }
  else
    {
      wifiMac.SetType (protocol == "csma" ? "ns3::AdhocCsmaMac" : "ns3::AdhocAlohaMac",
                       "QosSupported", BooleanValue (false),
                       "EnableAck", BooleanValue (useAck));
    }

  // disable rate control
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue ("OfdmRate6Mbps"));
  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, allStations);

  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allStations);

  InternetStackHelper internet;
  internet.Install (allStations);

  Ipv4AddressHelper ipv4;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);

  NeighborCacheHelper neighborCache;
  neighborCache.PopulateNeighborCache ();

  //Install applications
  //Install server on station 0
  PacketSinkHelper server ("ns3::UdpSocketFactory",InetSocketAddress(interfaces.GetAddress (0),9));

  ApplicationContainer serverApps = server.Install (serverStation);
  serverApps.Start (serverStart);
  serverApps.Stop (simTime);
  serverApps.Get(0)->TraceConnectWithoutContext ("Rx", MakeCallback(&ReceiveTrace));

  //Install clients
  UdpEchoClientHelper echoClient (interfaces.GetAddress (0), 9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (UINT32_MAX));
  //echoClient.SetAttribute ("Interval", TimeValue (packetInterval));
  echoClient.SetAttribute ("EnableRandomInterval", BooleanValue (true));
  std::ostringstream intervalDist;
  intervalDist <<  "ns3::ExponentialRandomVariable[Mean=" << packetInterval.GetSeconds () << "]";
  echoClient.SetAttribute ("RandomIntervalVariable", StringValue (intervalDist.str ()));
  echoClient.SetAttribute ("PacketSize", UintegerValue (packetSize));

  ApplicationContainer clientApps = echoClient.Install (clientStations);

  clientApps.Start (clientStart);
  clientApps.Stop (simTime);

  // Tracing
  if (collectPcap)
    {
      wifiPhy.EnablePcap ("aloha_vs_dcf", devices);
    }

  //Record on every device.
#ifdef HAS_WIFIVIZ
  Ptr<SniffUtils> viz =
    WiFiVizHelper::MaybeEnableVisualizer (enableViz, devices, simTime.GetSeconds (), launchViewer);
#else
  if (enableViz || launchViewer)
    {
      std::cerr << "WiFiViz is not available: clone the wifiviz module into contrib/ and "
                   "re-run ./ns3 configure to use --enableViz/--launchViewer" << std::endl;
    }
#endif

  Simulator::Stop(simTime);

  Simulator::Schedule (Seconds (1), &PrintProgress, simTime);

  Simulator::Run ();
  Simulator::Destroy ();

  //Print simulation results in the file
  std::ofstream outStream;
  outStream.open (outFileName.c_str (),std::ios::out);
  if (!outStream.is_open ())
   {
     NS_FATAL_ERROR ("Cannot open file " << outFileName);
   }

  double throuhgput = 8*RcvPktCount*packetSize/((simTime-clientStart).GetSeconds()*1e6);
  outStream<<numOfStations<<"\t"<<throuhgput<<std::endl;

  outStream.close ();

  return 0;
}
