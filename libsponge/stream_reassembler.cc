#include "stream_reassembler.hh"

#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const uint64_t index, const bool eof) {

    if(eof && data.empty()) _output.end_input();

    for (size_t i = 0; i < data.size(); i++) {

        uint64_t curIndex = index + i;
        if(eof && i==data.size()-1){
            this->lastByteIndex = curIndex;
        }
        if (visitedIndexSet.find(curIndex) != visitedIndexSet.end())
            continue;  // no duplicate bytes

        char c = data[i];
        if ((curIndex == 0 || (visitedIndexSet.find(curIndex - 1) != visitedIndexSet.end()))&& (_output.buffer_size()<_capacity
                                                                                                 )) {

            if(i==data.size()-1 && eof) {
                this->_output.end_input();
            }

            visitedIndexSet.insert(curIndex);     // byte at this index has been visited
            _output.write(std::string(1, c));  // write this byte to string

            uint64_t nextIndex = curIndex + 1;  // check if any other unassembled byte can be pushed
            while (indexMap.count(nextIndex) > 0 && (_output.buffer_size()<_capacity)) {
                char ch = indexMap[nextIndex];
                indexMap.erase(nextIndex);
                visitedIndexSet.insert(nextIndex);
                _output.write(std::string(1, ch));
                nextIndex++;
            }
        } else {
            if(unassembled_bytes()<_capacity - _output.buffer_size()){      //will store in the indexMap exceeds the total capacity
                indexMap[curIndex] = c;
            }
        }
    }
    if(visitedIndexSet.find(lastByteIndex)!=visitedIndexSet.end()){     //if visited lastByteIndex, indeed eof
        _output.end_input();
    }

}

size_t StreamReassembler::unassembled_bytes() const { return indexMap.size(); }

bool StreamReassembler::empty() const { return indexMap.empty(); }
