#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , timer(retx_timeout) {}

size_t TCPSender::bytes_in_flight() const {
    uint64_t total = 0;
    for (const auto &[ab_seqno, segment] : outMap) {
        total += segment.length_in_sequence_space();
    }
    return total;
}

void TCPSender::fill_window() {

    if(bytes_in_flight() > curRecvWindowSize) return;

    if ((stream_in().eof()) && (next_seqno_absolute() == _stream.bytes_written() + 2)) //TODO weak fix when FIN_SENT
        return;

    if (next_seqno_absolute() != 0 && _stream.buffer_empty() && !_stream.eof())
        return;

    while (curSendWindowSize > 0) {
        TCPSegment segment;

        if (next_seqno_absolute() == 0) {  // closed stated
            segment.header().syn = true;
            curSendWindowSize = 0;
        }

        std::string data = "";
        while (data.size() < TCPConfig::MAX_PAYLOAD_SIZE && !_stream.buffer_empty() && curSendWindowSize > 0) {
            data += _stream.read(1);
            curSendWindowSize--;
        }

        if (_stream.eof() && curSendWindowSize>0) {
            segment.header().fin = true;
            curSendWindowSize = 0;  // TODO weak fix when fin flag is inserted, code style not scalable
        }

        segment.payload() = Buffer(std::move(data));

        // fill in seqno
        // iniital seqno = wrap(next abosulte seqno, isn)
        segment.header().seqno = next_seqno();

        // add into outstanding segment Map and output segment queue
        if (outMap.find(next_seqno_absolute()) == outMap.end()) {
            this->outMap[this->_next_seqno] = segment;
            this->_segments_out.push(segment);

            this->_next_seqno += segment.length_in_sequence_space();

            //!\brief start the timer if it's not started
            if (!timer.hasStart()) {
                timer.restart();
            }
        }

        if (_stream.buffer_empty() || curSendWindowSize == 0 || _stream.eof()) {
            return;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // convert ackno to 64bit
    uint64_t unwrap_ackno = unwrap(ackno, _isn, checkPoint);
    uint64_t prevMapSize = outMap.size();

    if (unwrap_ackno > next_seqno_absolute())
        return;  // TODO weak fix handle impossible case when ackno > next expected ack

    // Step 1: remove acknowledged outstanding segments
    for (auto it = outMap.begin(); it != outMap.end();) {
        uint64_t ab_seqno = it->first;
        TCPSegment curSegment = it->second;
        if (unwrap_ackno > (ab_seqno + curSegment.length_in_sequence_space() - 1)) {
            it = outMap.erase(it);
        } else {
            ++it;
        }
    }

    if (outMap.size() < prevMapSize) {  // some data has been successfully acknowledged
        checkPoint = unwrap_ackno - 1;
        timer.resetAckRTO();
        timer.restart();
        consecutiveReTX = 0;
    }

    if (outMap.empty())
        timer.stop();

    // check window size and call fill_in window
    if (window_size == 0) {
        curSendWindowSize = 1;
        this->curRecvWindowSize =0;
    } else {
        curSendWindowSize = window_size;
        this->curRecvWindowSize = curSendWindowSize;
    }

    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (timer.hasStart() && timer.checkExpire(ms_since_last_tick)) {
        // Step 1 retransmit the earliest one

        auto it = outMap.begin();

//        cout<<"push segment with seqno "<<it->second.header().seqno<<endl;
        _segments_out.push(it->second);

        // Step 2 check if window size is 0, if no, RTO double & ++ consecutiveTranx
        if (this->curRecvWindowSize> 0) {
            consecutiveReTX++;
            timer.resetReTXRTO();
        }

        // Step 3  restart the timer
        timer.restart();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return consecutiveReTX; }

void TCPSender::send_empty_segment() {
    TCPSegment emSegment;
    emSegment.header().seqno = next_seqno();
    this->segments_out().push(emSegment);
}

///// SendTimer Class Implementation
SendTimer::SendTimer(const uint16_t _initial_retransmission_timeout)
    : RTO(_initial_retransmission_timeout), RTO_init(_initial_retransmission_timeout) {}

void SendTimer::resetAckRTO() { RTO = RTO_init; }

void SendTimer::resetReTXRTO() { RTO = 2 * RTO; }

void SendTimer::restart() { curTime.emplace(0); }

bool SendTimer::checkExpire(const size_t ms_since_last_tick) {
    curTime.value() += ms_since_last_tick;
    if (curTime.value() >= RTO) {
        return true;
    }
    return false;
}

bool SendTimer::hasStart() { return curTime.has_value(); }

void SendTimer::stop() { curTime = std::nullopt; }
