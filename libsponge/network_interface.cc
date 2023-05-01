#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>
#include <cassert>
// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cache[_ip_address.ipv4_numeric()]=_ethernet_address;
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    //does cache have the ip address?
    if(cache.find(next_hop_ip)!=cache.end()){
        EthernetAddress  ethernetAddress = cache[next_hop_ip];

        sendFrame(dgram.serialize(),ethernetAddress,EthernetHeader::TYPE_IPv4);
        return;
    }

    //! \note don't send ARP request if already sent about same IP address in last 5 seconds
    //see the time passed and check if that arp request existed, if not exist || exist and pass 5 seconds then resend
    auto it= queueIPMap.find(next_hop_ip);
    auto en = queueIPMap.end();
    if(it == en || (it!=en && totalPassedTime >=5000)){
            EthernetAddress  ethernetAddress;
            //construct ARPMessage
            ARPMessage message;
            message.opcode = ARPMessage::OPCODE_REQUEST;
            message.sender_ip_address = this->_ip_address.ipv4_numeric();
            message.sender_ethernet_address = this->_ethernet_address;
            message.target_ip_address = next_hop_ip;

            ethernetAddress = ETHERNET_BROADCAST;
            sendFrame(message.serialize(),ethernetAddress,EthernetHeader::TYPE_ARP);

            //store it into the IP Datagram queue
            queueIPMap[next_hop_ip] = dgram;
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    EthernetAddress  dstAddress = frame.header().dst;
    //! \info check if the frame is coming to us
    if(dstAddress!= ETHERNET_BROADCAST && dstAddress!= this->_ethernet_address){
        return {};
    }

    EthernetHeader header = frame.header();
    if(header.type == EthernetHeader::TYPE_IPv4){
            InternetDatagram dgram;
            ParseResult result = dgram.parse(frame.payload());
            if(result==ParseResult::NoError){
                return optional<InternetDatagram>{dgram};
            }
    }
    else if(header.type == EthernetHeader::TYPE_ARP){
        ARPMessage message;
        ParseResult result = message.parse(frame.payload());
        assert(message.target_ip_address = this->_ip_address.ipv4_numeric());

        if(result==ParseResult::NoError){
            //! \info record mapping no matter request or reply
            cache[message.sender_ip_address] = message.sender_ethernet_address;
            cacheIPTimeMap[message.sender_ip_address] = 0;

            if(queueIPMap.find(message.sender_ip_address)!=queueIPMap.end()){
                sendFrame(queueIPMap[message.sender_ip_address].serialize(), frame.header().src,EthernetHeader::TYPE_IPv4);
                queueIPMap.erase(message.sender_ip_address);
            }

            //throw away reply message
            if(message.opcode == ARPMessage::OPCODE_REQUEST && (cache.find(message.target_ip_address)!=cache.end())){

                    //! \info send an ARP reply message
                    EthernetAddress  ethernetAddress = frame.header().src;
                    //construct ARPMessage
                    ARPMessage replyARPMessage;
                    replyARPMessage.opcode = ARPMessage::OPCODE_REPLY;
                    replyARPMessage.sender_ip_address = this->_ip_address.ipv4_numeric();
                    replyARPMessage.sender_ethernet_address = this->_ethernet_address;
                    replyARPMessage.target_ip_address =message.sender_ip_address;
                    replyARPMessage.target_ethernet_address = message.sender_ethernet_address;

                    sendFrame(replyARPMessage.serialize(),ethernetAddress,EthernetHeader::TYPE_ARP);
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    totalPassedTime+=ms_since_last_tick;
    for(auto it = cacheIPTimeMap.begin(); it!=cacheIPTimeMap.end();){
        if(cacheIPTimeMap[it->first]+ms_since_last_tick>=30000){
//            cout<<"tick  "<<ms_since_last_tick<<endl;
//            cout<<"Erase cache IP map for "<<it->first<<endl;
            cache.erase(it->first);
            it = cacheIPTimeMap.erase(it);
        }else {
            cacheIPTimeMap[it->first]+=ms_since_last_tick;
            it++;
        }
    }
}

void NetworkInterface::sendFrame(const BufferList& payload, const EthernetAddress& dst, const uint16_t type){
    EthernetHeader header;  //!\info set header
    header.src = _ethernet_address;
    header.dst = dst;
    header.type = type;

    EthernetFrame ethernetFrame;    //!\info set ethernetFrame  2. serialize the ip datagram to payload
    ethernetFrame.header() = header;
    ethernetFrame.payload() = payload;

    frames_out().push(ethernetFrame);
}