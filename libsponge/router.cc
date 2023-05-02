#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}
void match(uint32_t dst,
           const std::unordered_map<uint32_t, std::unordered_map<uint8_t, std::pair<std::optional<Address>, uint64_t>>>
               &routingTable,
           uint8_t &matching_length,
           uint32_t &matching_prefix,
           bool &isMatch);

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    //    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
    //         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num <<
    //         "\n";

//    if(prefix_length==0) return; //!\info corner case

    pair<std::optional<Address>, uint64_t> nextPair;
    nextPair.first = next_hop;
    nextPair.second = interface_num;

    if (routingTable.find(route_prefix) == routingTable.end()) {
        routingTable[route_prefix] = {};
    }
    routingTable[route_prefix][prefix_length] = nextPair;

}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    if (dgram.header().ttl <=1)
        return;

    uint8_t matching_length = 0;
    uint32_t matching_prefix = 0;
    uint32_t dst = dgram.header().dst;
    bool isMatch = false;

    match(dst, routingTable, matching_length, matching_prefix, isMatch);

    if (!isMatch)
        return;

    dgram.header().ttl--;

    uint64_t nic_num = routingTable[matching_prefix][matching_length].second;
    if(!routingTable[matching_prefix][matching_length].first.has_value()){
        interface(nic_num).send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
    }else{
        Address address = routingTable[matching_prefix][matching_length].first.value();

        interface(nic_num).send_datagram(dgram, address);
    }

}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}

void match(uint32_t dst,
           const std::unordered_map<uint32_t, std::unordered_map<uint8_t, std::pair<std::optional<Address>, uint64_t>>>
               &routingTable,
           uint8_t &matching_length,
           uint32_t &matching_prefix,
           bool &isMatch) {
    for (auto it : routingTable) {
        uint32_t routing_prefix = it.first;
        for (auto jt : it.second) {
            uint8_t prefix_length = jt.first;
            uint8_t shift_length = 32 - prefix_length;

            if(prefix_length==0 && matching_length==0){
                isMatch = true;
                matching_prefix = routing_prefix;
                matching_length = prefix_length;
            }

            else if((routing_prefix>>shift_length)==(dst>>shift_length)){
                if (prefix_length > matching_length) {
                    isMatch = true;
                    matching_prefix = routing_prefix;
                    matching_length = prefix_length;
                }
            }
        }
    }
}
