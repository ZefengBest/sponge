#include "tcp_connection.hh"

#include <cassert>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _cfg.send_capacity - _sender.stream_in().buffer_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return this->timeSinceLastSegmentReceived; }

void TCPConnection::segment_received(const TCPSegment &seg) {

    if(!active()) return;
    this->timeSinceLastSegmentReceived = 0;
    //!\info abort the connection if RST flag set
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    if(!_receiver.ackno().has_value() && seg.payload().size()>0){       //receive data before reciving ACK on SYN
        _sender.send_empty_segment();
    }
    else if (_receiver.ackno().has_value() && seg.header().seqno == _receiver.ackno().value() - 1 &&
        seg.length_in_sequence_space() == 0) {  //!\info receive keep-alive segment
        _sender.send_empty_segment();           //!\info is not actually sent through TCPConnection's sgment out queue
    } else {
        bool prevSynReCD = _receiver.ackno().has_value();
        uint64_t prevOutSize = _receiver.stream_out().buffer_size();
        uint64_t prevUnAssembledSize = unassembled_bytes();
        bool valid = _receiver.segment_received(seg);
        //!\info state: SYN-acked, received FIN before sending FIN on our side
        if (_receiver.ackno().has_value() && seg.header().fin && (_sender.next_seqno_absolute() < _sender.stream_in().bytes_written() + 2)) {
            _linger_after_streams_finish = false;
        }

        if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }

        //!\info check if can clean shutdown
        if (!_linger_after_streams_finish && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
            bytes_in_flight() == 0) {
            cleanShutdown = true;
            return;
        }

        if (seg.header().syn) {
            if (_sender.next_seqno_absolute() == 0) {  // not send SYN yet, need to send SYN/ACK
                connect();
                return;
            } else if (!prevSynReCD && _receiver.ackno().has_value()) {
                _sender.send_empty_segment();  // sent SYN, need to send ACK, if first time receives SYN&ACK
            } else if (_receiver.stream_out().buffer_size() > prevOutSize) {  // some bytes are acknowledged
                _sender.send_empty_segment();
            }
        } else if(_sender.segments_out().empty() && seg.header().ack && seg.length_in_sequence_space()==0){      //TODO NO ACK for ACK
            return;
        }
        else if (_sender.segments_out().empty() && ((_receiver.stream_out().buffer_size() > prevOutSize) ||
                                                      unassembled_bytes() > prevUnAssembledSize)) {
            _sender.send_empty_segment();
        } else if (_receiver.ackno().has_value() &&
                   !valid) {  //!\info send ack when receive out of window segment AFTER LISTEN
            _sender.send_empty_segment();
        } else if (_sender.segments_out().empty() && seg.header().fin) {
            _sender.send_empty_segment();
        } else if(_sender.segments_out().empty() && seg.length_in_sequence_space()!=0){
            _sender.send_empty_segment();
        }
    }

    swap(this->segments_out(), _sender.segments_out());

    std::vector<TCPSegment> tempVector;
    while (!segments_out().empty()) {
        tempVector.push_back(std::move(this->segments_out().front()));
        this->segments_out().pop();
    }

    uint64_t win = _receiver.window_size();
    optional<WrappingInt32> ackno = _receiver.ackno();

    for (TCPSegment &segment : tempVector) {
        segment.header().win = win;
        if (ackno.has_value()) {
            segment.header().ackno = ackno.value();
            segment.header().ack = true;
        }
    }

    for (TCPSegment &s : tempVector) {
        _segments_out.push(std::move(s));
    }

    assert(tempVector.empty());
}

bool TCPConnection::active() const {
    if (_sender.stream_in().error() || _receiver.stream_out().error())
        return false;  //!\info after set/receive RST flag
    if (cleanShutdown)
        return false;  // clean shutdown
    return true;
}

// sender
size_t TCPConnection::write(const string &data) {
    // Step 1 write data into outbound stream
    uint64_t  n = min(remaining_outbound_capacity(), data.size());
    _sender.stream_in().write(data.substr(0, n));
    //!\brief should there be checking on the size of data?
//    uint64_t prevBytesSize = bytes_in_flight();

    // 2 : fill_ in window
    _sender.fill_window();

    // get all segments in queue and modify them
    std::swap(this->segments_out(), _sender.segments_out());

    std::vector<TCPSegment> tempVector;
    while (!segments_out().empty()) {
        tempVector.push_back(std::move(this->segments_out().front()));
        this->segments_out().pop();
    }

    uint64_t win = _receiver.window_size();
    optional<WrappingInt32> ackno = _receiver.ackno();
    for (TCPSegment &segment : tempVector) {
        segment.header().win = win;
        if (ackno.has_value()) {
            segment.header().ackno = ackno.value();
            segment.header().ack = true;
        }
    }

    for (auto &seg : tempVector) {
        _segments_out.push(std::move(seg));
    }

    // return number of bytes actually written?
    return n;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    this->timeSinceLastSegmentReceived += ms_since_last_tick;

    //!\info clean shutdown
    if (_linger_after_streams_finish && time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {  // req 4
        if (_receiver.stream_out().eof()) {                                                            // req 1
            if (_sender.stream_in().eof() && bytes_in_flight() == 0 &&                                 // req 2 & req 3
                (_sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2)) {
                cleanShutdown = true;
                return;
            }
        }
    }

    _sender.tick(ms_since_last_tick);

    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        //!\info abort the connect
        TCPSegment RSTsegment;
        this->segments_out().push(RSTsegment);
        this->segments_out().front().header().rst = true;
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    } else {
        swap(this->segments_out(), _sender.segments_out());

        std::vector<TCPSegment> tempVector;
        while (!segments_out().empty()) {
            tempVector.push_back(std::move(this->segments_out().front()));
            this->segments_out().pop();
        }

        uint64_t win = _receiver.window_size();
        optional<WrappingInt32> ackno = _receiver.ackno();

        for (TCPSegment &segment : tempVector) {
            segment.header().win = win;
            if (ackno.has_value()) {
                segment.header().ackno = ackno.value();
                segment.header().ack = true;
            }
        }

        for (TCPSegment &s : tempVector) {
            _segments_out.push(std::move(s));
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    //!\info try to send the FIN flag
    write("");
}

void TCPConnection::connect() {
    //!\info send empty syn flag
    write("");
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            TCPSegment RSTsegment;
            this->segments_out().push(RSTsegment);
            this->segments_out().front().header().rst = true;
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
