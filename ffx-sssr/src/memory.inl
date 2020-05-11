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

namespace ffx_sssr
{
    /**
        The constructor for the IdDispenser class.

        \param max_id_count The maximum capacity of the dispenser.
    */
    IdDispenser::IdDispenser(std::uint32_t max_id_count)
        : ids_(max_id_count ? static_cast<std::uint64_t*>(malloc(max_id_count * sizeof(std::uint64_t))) : nullptr)
        , next_index_(kInvalidIndex)
        , id_count_(0u)
        , max_id_count_(0u)
    {
        // Could we allocate our memory successfully?
        if (max_id_count && !ids_)
        {
            free(ids_);

            throw reflection_error(FFX_SSSR_STATUS_OUT_OF_MEMORY);
        }

        // Initialize the freelist
        for (auto i = 0u; i < max_id_count; ++i)
        {
            ids_[i] = (i + 1 == max_id_count ? kInvalidIndex : i + 1);
        }

        // Assign the base members
        next_index_ = (max_id_count ?  0u : kInvalidIndex);
        max_id_count_ = max_id_count;
    }

    /**
        The destructor for the IdDispenser class.
    */
    IdDispenser::~IdDispenser()
    {
        // Were there any non-freed descriptors?
#if 0
        auto const leaked_id_count = (max_id_count_ - CalculateFreeIdCount());

        FFX_SSSR_ASSERT(leaked_id_count == id_count_);

        if (leaked_id_count)
        {
            FFX_SSSR_PRINTLN("%u resource%s %s not destroyed properly; detected memory leak", leaked_id_count, leaked_id_count > 1 ? "s" : "", leaked_id_count > 1 ? "were" : "was");
        }
#endif

        // Release our memory
        free(ids_);
    }

    /**
        Allocates the next available identifier.

        \param id The allocated identifier.
        \return true if successful, false otherwise.
    */
    bool IdDispenser::AllocateId(std::uint64_t& id)
    {
        // Are we out of memory?
        if (next_index_ == kInvalidIndex)
        {
            return false;
        }

        // Get hold of next available slot
        auto const index = next_index_;
        auto& slot = ids_[index];

        // Advance generation
        auto const next_index = static_cast<std::uint32_t>(slot & 0xFFFFFFFFull);
        auto const age = static_cast<std::uint16_t>((slot >> 32) & 0xFFFFull) + 1u;

        // Update the freelist
        next_index_ = next_index;
        id = (static_cast<std::uint64_t>(age) << 32) | static_cast<std::uint64_t>(index);
        slot = (static_cast<std::uint64_t>(age) << 32) | static_cast<std::uint64_t>(kInvalidIndex);

        // Keep track of number of allocated identifiers
        FFX_SSSR_ASSERT(id_count_ < max_id_count_);
        ++id_count_;

        return true;
    }

    /**
        Frees the identifier.

        \param id The identifier to be freed.
    */
    void IdDispenser::FreeId(std::uint64_t id)
    {
        // Get hold of the freed slot
        auto const index = static_cast<std::uint32_t>(id & 0xFFFFFFFFull);
        FFX_SSSR_ASSERT(index < max_id_count_);
        auto& slot = ids_[index];

        // Check whether this is a valid operation
        auto const age = static_cast<std::uint16_t>((slot >> 32) & 0xFFFFull);

        if (age != static_cast<std::uint16_t>((id >> 32) & 0xFFFFull) || static_cast<std::uint32_t>(slot & 0xFFFFFFFFull) != kInvalidIndex)
        {
            return; // identifier was already freed
        }

        // Return to the freelist
        slot = (static_cast<std::uint64_t>(age) << 32) | static_cast<std::uint64_t>(next_index_);
        next_index_ = index;

        // Keep track of number of allocated identifiers
        FFX_SSSR_ASSERT(id_count_ > 0u);
        --id_count_;
    }

    /**
        Gets the number of allocated identifiers.

        \return The number of allocated identifiers.
    */
    std::uint32_t IdDispenser::GetIdCount() const
    {
        return id_count_;
    }

    /**
        Gets the maximum number of identifiers that can be allocated.

        \return The maximum number of identifiers.
    */
    std::uint32_t IdDispenser::GetMaxIdCount() const
    {
        return max_id_count_;
    }

    /**
        Checks whether the identifier is still valid.

        \param id The identifier to be checked.
        \return true if the identifier is valid, false otherwise.
    */
    bool IdDispenser::IsValid(std::uint64_t id) const
    {
        // Get hold of the corresponding slot
        auto const index = static_cast<std::uint32_t>(id & 0xFFFFFFFFull);
        FFX_SSSR_ASSERT(index < max_id_count_);
        auto const slot = ids_[index];

        // Check whether the identifier is still valid
        auto const age = static_cast<std::uint16_t>((slot >> 32) & 0xFFFFull);

        if (age != static_cast<std::uint16_t>((id >> 32) & 0xFFFFull) || static_cast<std::uint32_t>(slot & 0xFFFFFFFFull) != kInvalidIndex)
        {
            return false;   // identifier was previously freed
        }

        return true;
    }

    /**
        Calculates the number of available identifiers.

        \return The number of remaining available identifiers.
    */
    std::uint32_t IdDispenser::CalculateFreeIdCount() const
    {
        auto free_id_count = 0u;

        // Iterate the entire freelist
        for (auto next_index = next_index_; next_index != kInvalidIndex; next_index = static_cast<std::uint32_t>(ids_[next_index] & 0xFFFFFFFFull))
        {
            ++free_id_count;
        }
        FFX_SSSR_ASSERT(free_id_count <= max_id_count_);

        return free_id_count;
    }

    /**
        The constructor for the iterator class.
    */
    template<typename TYPE>
    SparseArray<TYPE>::iterator::iterator()
        : index_(0u)
        , array_(nullptr)
    {
    }

    /**
        Iterates over to the next object.

        \return The updated iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::iterator& SparseArray<TYPE>::iterator::operator ++()
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        ++index_;   // iterate to next
        return *this;
    }

    /**
        Gets the iterated object.

        \return The reference to the iterated object.
    */
    template<typename TYPE>
    TYPE& SparseArray<TYPE>::iterator::operator *() const
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        return array_->objects_[index_];
    }

    /**
        Gets the virtual index for the iterated object.

        \return The virtual index.
    */
    template<typename TYPE>
    SparseArray<TYPE>::iterator::operator std::uint32_t() const
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        return array_->virtual_indices_[index_];
    }

    /**
        Compares the two iterators.

        \return true if the iterators are not equal, false otherwise.
    */
    template<typename TYPE>
    bool SparseArray<TYPE>::iterator::operator !=(iterator const& other) const
    {
        return (index_ != other.index_ || array_ != other.array_);
    }

    /**
        The constructor for the const_iterator class.
    */
    template<typename TYPE>
    SparseArray<TYPE>::const_iterator::const_iterator()
        : index_(0u)
        , array_(nullptr)
    {
    }

    /**
        The constructor for the const_iterator class.

        \param other The iterator to be constructing from.
    */
    template<typename TYPE>
    SparseArray<TYPE>::const_iterator::const_iterator(iterator const& other)
        : index_(other.index_)
        , array_(other.array_)
    {
    }

    /**
        Iterates over to the next object.

        \return The updated iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::const_iterator& SparseArray<TYPE>::const_iterator::operator ++()
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        ++index_;   // iterate to next
        return *this;
    }

    /**
        Gets the iterated object.

        \return The reference to the iterated object.
    */
    template<typename TYPE>
    TYPE const& SparseArray<TYPE>::const_iterator::operator *() const
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        return array_->objects_[index_];
    }

    /**
        Gets the virtual index for the iterated object.

        \return The virtual index.
    */
    template<typename TYPE>
    SparseArray<TYPE>::const_iterator::operator std::uint32_t() const
    {
        FFX_SSSR_ASSERT(array_ && index_ < array_->object_count_);
        return array_->virtual_indices_[index_];
    }

    /**
        Compares the two iterators.

        \return true if the iterators are not equal, false otherwise.
    */
    template<typename TYPE>
    bool SparseArray<TYPE>::const_iterator::operator !=(const_iterator const& other) const
    {
        return (index_ != other.index_ || array_ != other.array_);
    }

    /**
        The constructor for the SparseArray class.

        \param max_object_count The maximum capacity of the sparse array.
    */
    template<typename TYPE>
    SparseArray<TYPE>::SparseArray(std::uint32_t max_object_count)
        : objects_(max_object_count ? static_cast<TYPE*>(malloc(max_object_count * sizeof(TYPE))) : nullptr)
        , object_count_(0u)
        , max_object_count_(max_object_count)
        , virtual_indices_(max_object_count ? static_cast<std::uint32_t*>(malloc(max_object_count * sizeof(std::uint32_t))) : nullptr)
        , physical_indices_(max_object_count ? static_cast<std::uint32_t*>(malloc(max_object_count * sizeof(std::uint32_t))) : nullptr)
    {
        // Could we allocate our memory successfully?
        if (max_object_count && (!objects_ || !virtual_indices_ || !physical_indices_))
        {
            free(objects_);
            free(virtual_indices_);
            free(physical_indices_);

            throw reflection_error(FFX_SSSR_STATUS_OUT_OF_MEMORY);
        }

        // Invalidate all virtual entries
        for (auto i = 0u; i < max_object_count; ++i)
            physical_indices_[i] = kInvalidIndex;
    }

    /**
        The destructor for the SparseArray class.
    */
    template<typename TYPE>
    SparseArray<TYPE>::~SparseArray()
    {
        // Were there any non-freed components?
#if 0
        auto const leaked_object_count = object_count_;

        if (leaked_object_count)
        {
            FFX_SSSR_PRINTLN("%u component%s %s not destroyed properly; detected memory leak", leaked_object_count, leaked_object_count > 1 ? "s" : "", leaked_object_count > 1 ? "were" : "was");
        }
#endif

        // Release all that was not properly destroyed
        Clear();

        // Release our memory
        free(objects_);
        free(virtual_indices_);
        free(physical_indices_);
    }

    /**
        Gets the object at the given index.

        \param index The index to be queried.
        \return The reference to the requested object.
    */
    template<typename TYPE>
    TYPE& SparseArray<TYPE>::operator [](std::uint32_t index)
    {
        auto const object = At(index);
        FFX_SSSR_ASSERT(object != nullptr);
        return *object;
    }

    /**
        Gets the object at the given index.

        \param index The index to be queried.
        \return The reference to the requested object.
    */
    template<typename TYPE>
    TYPE const& SparseArray<TYPE>::operator [](std::uint32_t index) const
    {
        auto const object = At(index);
        FFX_SSSR_ASSERT(object != nullptr);
        return *object;
    }

    /**
        Gets the object at the given index.

        \param index The index to be queried.
        \return A pointer to the requested object, or nullptr if not found.
    */
    template<typename TYPE>
    TYPE* SparseArray<TYPE>::At(std::uint32_t index)
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        auto const physical_index = physical_indices_[index];
        if (physical_index == kInvalidIndex)
            return nullptr; // not found
        return &objects_[physical_index];
    }

    /**
        Gets the object at the given index.

        \param index The index to be queried.
        \return A pointer to the requested object, or nullptr if not found.
    */
    template<typename TYPE>
    TYPE const* SparseArray<TYPE>::At(std::uint32_t index) const
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        auto const physical_index = physical_indices_[index];
        if (physical_index == kInvalidIndex)
            return nullptr; // not found
        return &objects_[physical_index];
    }

    /**
        Checks whether an object exists at the given index.

        \param index The index to be checked.
        \return true if an object exists, false otherwise.
    */
    template<typename TYPE>
    bool SparseArray<TYPE>::Has(std::uint32_t index) const
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        return physical_indices_[index] != kInvalidIndex;
    }

    /**
        Inserts a new object inside the sparse array.

        \param index The virtual index at which to insert.
        \return The reference to the inserted object.
    */
    template<typename TYPE>
    TYPE& SparseArray<TYPE>::Insert(std::uint32_t index)
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        auto const physical_index = physical_indices_[index];
        if (physical_index != kInvalidIndex)
        {
            (void)objects_[physical_index].~TYPE();
            return *new(&objects_[physical_index]) TYPE();
        }
        FFX_SSSR_ASSERT(object_count_ < max_object_count_);
        virtual_indices_[object_count_] = index;
        physical_indices_[index] = object_count_;
        return *new(&objects_[object_count_++]) TYPE();
    }

    /**
        Inserts a new object inside the sparse array.

        \param index The virtual index at which to insert.
        \param object The object to be inserted in the array.
        \return The reference to the inserted object.
    */
    template<typename TYPE>
    TYPE& SparseArray<TYPE>::Insert(std::uint32_t index, TYPE const& object)
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        auto const physical_index = physical_indices_[index];
        if (physical_index != kInvalidIndex)
        {
            (void)objects_[physical_index].~TYPE();
            return *new(&objects_[physical_index]) TYPE(object);
        }
        FFX_SSSR_ASSERT(object_count_ < max_object_count_);
        virtual_indices_[object_count_] = index;
        physical_indices_[index] = object_count_;
        return *new(&objects_[object_count_++]) TYPE(object);
    }

    /**
        Erases the object at the given index.

        \param index The virtual index at which to erase.
        \return true if an object was erased, false otherwise.
    */
    template<typename TYPE>
    bool SparseArray<TYPE>::Erase(std::uint32_t index)
    {
        FFX_SSSR_ASSERT(index < max_object_count_);
        auto const physical_index = physical_indices_[index];
        if (physical_index == kInvalidIndex)
            return false;   // nothing to erase here
        FFX_SSSR_ASSERT(object_count_ > 0u);
        if (physical_index != object_count_ - 1u)
        {
            std::swap(objects_[physical_index], objects_[object_count_ - 1u]);
            virtual_indices_[physical_index] = virtual_indices_[object_count_ - 1u];
            physical_indices_[virtual_indices_[physical_index]] = physical_index;
        }
        physical_indices_[index] = kInvalidIndex;
        (void)objects_[--object_count_].~TYPE();
        return true;    // object has been popped
    }

    /**
        Clears the sparse array.
    */
    template<typename TYPE>
    void SparseArray<TYPE>::Clear()
    {
        for (auto i = 0u; i < object_count_; ++i)
        {
            physical_indices_[virtual_indices_[i]] = kInvalidIndex;
            (void)objects_[i].~TYPE();
        }
        object_count_ = 0u;
    }

    /**
        Gets the storage for the inserted objects.

        \return The array of inserted objects.
    */
    template<typename TYPE>
    TYPE* SparseArray<TYPE>::GetObjects()
    {
        return objects_;
    }

    /**
        Gets the storage for the inserted objects.

        \return The array of inserted objects.
    */
    template<typename TYPE>
    TYPE const* SparseArray<TYPE>::GetObjects() const
    {
        return objects_;
    }

    /**
        Gets the current size of the sparse array.

        \return The number of inserted objects in the array.
    */
    template<typename TYPE>
    std::uint32_t SparseArray<TYPE>::GetObjectCount() const
    {
        return object_count_;
    }

    /**
        Gets the maximum capacity of the sparse array.

        \return The maximum number of objects that can be inserted.
    */
    template<typename TYPE>
    std::uint32_t SparseArray<TYPE>::GetMaxObjectCount() const
    {
        return max_object_count_;
    }

    /**
        Gets the virtual index.

        \param physical_index The physical index to be converted.
        \return The requested virtual index.
    */
    template<typename TYPE>
    std::uint32_t SparseArray<TYPE>::GetVirtualIndex(std::uint32_t physical_index) const
    {
        FFX_SSSR_ASSERT(physical_index < object_count_);

        return virtual_indices_[physical_index];
    }

    /**
        Gets the physical index.

        \param virtual_index The virtual index to be converted.
        \return The requested physical index.
    */
    template<typename TYPE>
    std::uint32_t SparseArray<TYPE>::GetPhysicalIndex(std::uint32_t virtual_index) const
    {
        FFX_SSSR_ASSERT(virtual_index < max_object_count_);

        return physical_indices_[virtual_index];
    }

    /**
        Gets an iterator pointing at the start of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::iterator SparseArray<TYPE>::begin()
    {
        iterator it;
        it.array_ = this;
        return it;
    }

    /**
        Gets an iterator pointing at the start of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::const_iterator SparseArray<TYPE>::begin() const
    {
        const_iterator it;
        it.array_ = this;
        return it;
    }

    /**
        Gets an iterator pointing at the start of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::const_iterator SparseArray<TYPE>::cbegin() const
    {
        const_iterator it;
        it.array_ = this;
        return it;
    }

    /**
        Gets an iterator pointing to the end of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::iterator SparseArray<TYPE>::end()
    {
        iterator it;
        it.index_ = object_count_;
        it.array_ = this;
        return it;
    }

    /**
        Gets an iterator pointing to the end of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::const_iterator SparseArray<TYPE>::end() const
    {
        const_iterator it;
        it.index_ = object_count_;
        it.array_ = this;
        return it;
    }

    /**
        Gets an iterator pointing to the end of the array.

        \return The requested iterator.
    */
    template<typename TYPE>
    typename SparseArray<TYPE>::const_iterator SparseArray<TYPE>::cend() const
    {
        const_iterator it;
        it.index_ = object_count_;
        it.array_ = this;
        return it;
    }

    /**
        The constructor for the Block class.
    */
    template<typename BLOCK_TYPE>
    RingBuffer<BLOCK_TYPE>::Block::Block()
        : start_(0u)
        , size_(0u)
    {
    }

    /**
        Checks whether the memory block can now be re-used.

        \return true if the memory block can be re-used, false otherwise.
    */
    template<typename BLOCK_TYPE>
    bool RingBuffer<BLOCK_TYPE>::Block::CanBeReused() const
    {
        return const_cast<BLOCK_TYPE&>(block_).CanBeReused();
    }

    /**
        The constructor for the RingBuffer class.

        \param size The size of the ring buffer.
    */
    template<typename BLOCK_TYPE>
    RingBuffer<BLOCK_TYPE>::RingBuffer(std::size_t size)
        : size_(size)
        , head_(0u)
    {
    }

    /**
        The destructor for the RingBuffer class.
    */
    template<typename BLOCK_TYPE>
    RingBuffer<BLOCK_TYPE>::~RingBuffer()
    {
    }

    /**
        Acquires the next available memory block.

        \param start The start of the block (in bytes).
        \param size The size of the block (in bytes).
        \param alignment The alignment of the block (in bytes).
        \return The acquired block, or nullptr if none could be acquired.
    */
    template<typename BLOCK_TYPE>
    BLOCK_TYPE* RingBuffer<BLOCK_TYPE>::AcquireBlock(std::size_t& start, std::size_t size, std::size_t alignment)
    {
        // Calculate the amount of space available
        auto next_block = GrabNextAvailableBlock();
        auto const new_head = Align(head_, alignment);  // account for alignment requirements
        auto space_available = CalculateSpaceToNextAvailableBlock(next_block, alignment);

        // If there isn't enough space left, try to make some room
        while (size > space_available)
        {
            if (!next_block)
            {
                if (!head_)
                    return nullptr; // not enough memory in the whole buffer to make space for this request
                head_ = 0u; // loop back to the beginning
                return AcquireBlock(start, size, alignment);
            }

            do
            {
                // Can we free this block?
                if (!next_block->CanBeReused())
                    return nullptr; // unable to make room for this request

                // Get rid of the freed block and advance
                blocks_.pop_front();
                next_block = GrabNextAvailableBlock();
                space_available = CalculateSpaceToNextAvailableBlock(next_block, alignment);
            }
            while (next_block && size > space_available);
        }
        FFX_SSSR_ASSERT(size <= space_available);

        // Insert the new block
        blocks_.emplace_back();
        auto& new_block = blocks_.back();
        new_block.start_ = new_head;
        new_block.size_ = size;

        // Advance head to new position
        start = new_head;
        head_ = new_head + size;

        return &new_block.block_;
    }

    /**
        Grabs the next available block.

        \return The next available block.
    */
    template<typename BLOCK_TYPE>
    typename RingBuffer<BLOCK_TYPE>::Block const* RingBuffer<BLOCK_TYPE>::GrabNextAvailableBlock() const
    {
        Block const* next_block = nullptr;
        if (!blocks_.empty())
            next_block = &blocks_[0];
        if (next_block && next_block->start_ + next_block->size_ <= head_)
            next_block = nullptr;   // we haven't reached back to that block yet
        return next_block;
    }

    /**
        Calculates the amount of space left before reaching the tail.

        \param next_block The next available memory block.
        \param alignment The alignment of the block (in bytes).
        \return The amount of space available (in bytes).
    */
    template<typename BLOCK_TYPE>
    std::size_t RingBuffer<BLOCK_TYPE>::CalculateSpaceToNextAvailableBlock(Block const* next_block, std::size_t alignment) const
    {
        auto const new_head = Align(head_, alignment);
        FFX_SSSR_ASSERT(!next_block || next_block->start_ + next_block->size_ > head_);
        return std::max(next_block ? next_block->start_ : size_, new_head) - new_head;
    }

    /**
        Gets the index for the given object identifier.

        \param object_id The object identifier to be evaluated.
        \return The index for the given object identifier.
    */
    static inline std::uint32_t ID(std::uint64_t object_id)
    {
        return static_cast<std::uint32_t>(object_id & 0xFFFFFFFFull);
    }
}
