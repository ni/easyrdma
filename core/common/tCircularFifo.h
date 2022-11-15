// Copyright (c) 2022 National Instruments
// SPDX-License-Identifier: MIT

#pragma once

//============================================================================
//  Includes
//============================================================================
#include <algorithm>

//============================================================================
//  Class tCircularFifo
//============================================================================
template<typename T>
class tCircularFifo {
    public:
        //------------------------------------------------------------------------
        //  Constructor
        //------------------------------------------------------------------------
        tCircularFifo() : _buffer(0), _head(0), _size(0) {
        }
        tCircularFifo(size_t size) : _buffer(size), _head(0), _size(0) {
        }
        void reallocate(size_t size) {
            clear();
            _buffer.resize(size);
        }
        //------------------------------------------------------------------------
        //  Basic container info
        //------------------------------------------------------------------------
        size_t size() const {
            return _size;
        }
        size_t capacity() const {
            return _buffer.size();
        }
        size_t unused() const {
            return capacity() - size();
        }
        bool empty() const {
            return _size == 0;
        }
        //------------------------------------------------------------------------
        //  push() - pushes element to back
        //------------------------------------------------------------------------
        void push(T elem) {
            assert(_size+1 <= capacity());
            _buffer[getWriteIndex(0)] = elem;
            ++_size;
        }
        //------------------------------------------------------------------------
        //  front() - returns the first element
        //------------------------------------------------------------------------
        T& front() {
            assert(size() > 0);
            return _buffer[getReadIndex(0)];
        }
        //------------------------------------------------------------------------
        //  pop() - removes first element from front
        //------------------------------------------------------------------------
        void pop() {
            assert(size() >= 1);
            _head = (_head + 1) % capacity();
            --_size;
        }
        //------------------------------------------------------------------------
        //  clear() - Resets all counters
        //------------------------------------------------------------------------
        void clear() {
            _head = 0;
            _size = 0;
        }
        //------------------------------------------------------------------------
        // getWriteIndex/getReadIndex - Gets position of read or write pointer
        //------------------------------------------------------------------------
        size_t getWriteIndex(size_t offset = 0) const {
            return (_head + size() + offset) % capacity();
        }
        size_t getReadIndex(size_t offset = 0) const {
            return (_head + offset) % capacity();
        }
    private:
        //------------------------------------------------------------------------
        //  Data members
        //------------------------------------------------------------------------
        std::vector<T> _buffer;
        size_t _head;
        size_t _size;
};
