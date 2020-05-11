/**********************************************************************
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#pragma once

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <new>

#include "utils.h"
#include "reflection_error.h"

namespace ffx_sssr
{
    /**
        The marker for an invalid index.
    */
    static constexpr auto const kInvalidIndex = 0xFFFFFFFFu;

    /**
        The IdDispenser class allows to allocate and free identifiers up to a given count at constant cost.

        \note A given identifier possesses the following structure:
         - top 16 bits: reserved for application use (used to flag the resource type).
         - next 16 bits: generational identifier (so deleting twice does not crash).
         - bottom 32 bits: object index (for looking up attached components).
    */
    class IdDispenser
    {
        FFX_SSSR_NON_COPYABLE(IdDispenser);

    public:
        inline IdDispenser(std::uint32_t max_id_count);
        inline ~IdDispenser();

        inline bool AllocateId(std::uint64_t& id);
        inline void FreeId(std::uint64_t id);

        inline std::uint32_t GetIdCount() const;
        inline std::uint32_t GetMaxIdCount() const;
        inline bool IsValid(std::uint64_t id) const;

    protected:
        inline std::uint32_t CalculateFreeIdCount() const;

        // The list of all available identifiers.
        std::uint64_t* ids_;
        // The index of the next available slot.
        std::uint32_t next_index_;
        // The number of allocated identifiers.
        std::uint32_t id_count_;
        // The maximum capacity of the dispenser.
        std::uint32_t max_id_count_;
    };

    /**
        The SparseArray class allows to insert objects at given indices while maintaining the underlying storage compacted.
    */
    template<typename TYPE>
    class SparseArray
    {
        FFX_SSSR_NON_COPYABLE(SparseArray);

    public:
        /**
            The iterator class allows to iterate over the inserted objects inside a sparse array.
        */
        class iterator
        {
            friend class SparseArray;

        public:
            iterator();

            iterator& operator++();
            TYPE& operator *() const;
            operator std::uint32_t() const;
            bool operator !=(iterator const& other) const;

        protected:
            // The iterated index.
            std::uint32_t index_;
            // The iterated array.
            SparseArray<TYPE>* array_;
        };

        /**
            The const_iterator class allows to iterate over the inserted objects inside a sparse array.
        */
        class const_iterator
        {
            friend class SparseArray;

        public:
            const_iterator();
            const_iterator(iterator const& other);

            const_iterator& operator++();
            TYPE const& operator *() const;
            operator std::uint32_t() const;
            bool operator !=(const_iterator const& other) const;

        protected:
            // The iterated index.
            std::uint32_t index_;
            // The iterated array.
            SparseArray<TYPE> const* array_;
        };

        SparseArray(std::uint32_t max_object_count);
        ~SparseArray();

        TYPE& operator [](std::uint32_t index);
        TYPE const& operator [](std::uint32_t index) const;

        TYPE* At(std::uint32_t index);
        TYPE const* At(std::uint32_t index) const;
        bool Has(std::uint32_t index) const;

        TYPE& Insert(std::uint32_t index);
        TYPE& Insert(std::uint32_t index, TYPE const& object);
        bool Erase(std::uint32_t index);
        void Clear();

        TYPE* GetObjects();
        TYPE const* GetObjects() const;
        std::uint32_t GetObjectCount() const;
        std::uint32_t GetMaxObjectCount() const;

        std::uint32_t GetVirtualIndex(std::uint32_t physical_index) const;
        std::uint32_t GetPhysicalIndex(std::uint32_t virtual_index) const;

        iterator begin();
        const_iterator begin() const;
        const_iterator cbegin() const;
        iterator end();
        const_iterator end() const;
        const_iterator cend() const;

    protected:
        // The storage for the allocated objects.
        TYPE* objects_;
        // The current size of the sparse array.
        std::uint32_t object_count_;
        // The maximum capacity of the sparse array.
        std::uint32_t max_object_count_;
        // The physical to virtual mapping table.
        std::uint32_t* virtual_indices_;
        // The virtual to physical mapping table.
        std::uint32_t* physical_indices_;
    };

    /**
        The RingBuffer class implements some standard wrap-around type of memory allocator.

        \note The BLOCK_TYPE type must implement the CanBeReused() method that is called when re-using previously acquired memory blocks.
    */
    template<typename BLOCK_TYPE>
    class RingBuffer
    {
        FFX_SSSR_NON_COPYABLE(RingBuffer);

    public:
        RingBuffer(std::size_t size);
        ~RingBuffer();

        BLOCK_TYPE* AcquireBlock(std::size_t& start, std::size_t size, std::size_t alignment = 16u);

    protected:
        /**
            The Block class represents an individual block inside the ring buffer.
        */
        class Block
        {
        public:
            Block();

            bool CanBeReused() const;

            // The underlying block.
            BLOCK_TYPE block_;
            // The start of the block.
            std::size_t start_;
            // The size of the block.
            std::size_t size_;
        };

        Block const* GrabNextAvailableBlock() const;
        std::size_t CalculateSpaceToNextAvailableBlock(Block const* next_block, std::size_t alignment) const;

        // The size of the ring buffer.
        std::size_t size_;
        // The head of the ring buffer.
        std::size_t head_;
        // The available blocks.
        std::deque<Block> blocks_;
    };
}

#include "memory.inl"
