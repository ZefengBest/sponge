#include "byte_stream.hh"

#include <iostream>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : maxCapacity(capacity), total_read(0),total_write(0),_input_ended_flag(false) {
}

size_t ByteStream::write(const string &data) {
    int count = 0;
    for (size_t i = 0; i < data.size() && stream.size() < this->maxCapacity; i++) {
        stream.push_back(data[i]);
        count++;
        this->total_write++;
    }
    return count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    std::string result;
    for (size_t i = 0; i < len && i<this->stream.size(); i++) {
        result += stream[i];
    }
    return result;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    for (size_t i = 0; i < len && !stream.empty(); i++) {
        stream.pop_front();
        this->total_read++;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string result;
    for (size_t i = 0; i < len && i<this->stream.size(); i++) {
        result += stream[i];
    }
    pop_output(len);
    return result;
}

void ByteStream::end_input() {this->_input_ended_flag = true;}

bool ByteStream::input_ended() const { return this->_input_ended_flag; }

size_t ByteStream::buffer_size() const { return this->stream.size(); }

bool ByteStream::buffer_empty() const { return this->stream.empty(); }

bool ByteStream::eof() const { return buffer_empty()&&input_ended(); }

size_t ByteStream::bytes_written() const { return this->total_write; }

size_t ByteStream::bytes_read() const { return this->total_read; }

size_t ByteStream::remaining_capacity() const { return this->maxCapacity - this->stream.size(); }
