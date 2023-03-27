#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}
using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
//    uint64_t segSize = seg.length_in_sequence_space();
    uint64_t prevSize = this->stream_out().buffer_size();
    if (header.syn == true) {
        bool eof = false;
        if(header.fin==true){
            fin.emplace(WrappingInt32(1)); //check whether fin flag has been set or not for later update ackno
            eof=true;
        }
        this->ISN.emplace(header.seqno);
        this->_reassembler.push_substring(seg.payload().copy(), 0, eof);
        this->lastCheckPoint = this->stream_out().buffer_size();    //first time lastCheckPoint
    }else{
        if(!this->ISN.has_value()) return;
        bool eof = false;
        if(header.fin==true){
            fin.emplace(WrappingInt32(1)); //check whether fin flag has been set or not for later update ackno
            eof=true;
        }
        uint64_t index  = unwrap(header.seqno,this->ISN.value(), lastCheckPoint)-1;
//        cout<<"The index of first byte of data is "<<index<<endl;
//        cout<<"The actual data is "<<seg.payload().copy()<<endl;
        this->_reassembler.push_substring(seg.payload().copy(), index, eof);
        this->lastCheckPoint = this->lastCheckPoint + (this->stream_out().buffer_size() - prevSize);
    }
    if(this->ISN.has_value() && this->fin.has_value()&& (this->unassembled_bytes()==0)) this->lastCheckPoint++;
//    cout<<"The checkpoint is "<<this->lastCheckPoint<<endl;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(!this->ISN.has_value()){
        return std::nullopt;
    }else{
        return {wrap(this->lastCheckPoint+1, this->ISN.value())};
    }
}

size_t TCPReceiver::window_size() const {
    return this->_capacity - (this->stream_out().buffer_size());
}
