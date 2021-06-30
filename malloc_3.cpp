#include <cstring>
#include <unistd.h>
#include <cmath>
#include <cstddef>
#include <sys/mman.h>

using std::memset;
using std::memmove;

#define MMAP_MIN_SIZE (128 * 1024)
#define BIN_MAX_SIZE 128
#define MIN_SPLIT 128
#define KB 1024

struct MallocMetadata{
    size_t size;
    bool is_free;
    void* address;
    MallocMetadata* next;
    MallocMetadata* prev;
    MallocMetadata* next_free;
    MallocMetadata* prev_free;
};

static MallocMetadata* list_block_head = nullptr;
static MallocMetadata* list_block_tail = nullptr;
static MallocMetadata* mmap_list_block_head = nullptr;
static MallocMetadata* mmap_list_block_tail = nullptr;
static MallocMetadata* free_bins[BIN_MAX_SIZE] = {nullptr};

size_t _num_free_blocks();

size_t _num_free_bytes();

size_t _num_allocated_blocks();

size_t _num_allocated_bytes();

size_t _num_meta_data_bytes();

size_t _size_meta_data();

static void split_block(size_t size, MallocMetadata* block_to_split) {
    if (block_to_split->size < MIN_SPLIT + size + _size_meta_data()) {
        return;
    }
    size_t size_left = block_to_split->size - size - _size_meta_data();
    block_to_split->size = size;
    void * temp = static_cast<char*>(block_to_split->address) + size;
    MallocMetadata * new_metadata = static_cast<MallocMetadata*>(temp);
    new_metadata->address = static_cast<char*>(temp) + _size_meta_data();
    new_metadata->size = size_left;
    MallocMetadata * tmp =  block_to_split->next;
    new_metadata->next = tmp;
    new_metadata->prev = block_to_split;
    if (block_to_split->next != nullptr) {
        block_to_split->next->prev = new_metadata;
    }
    block_to_split->next = new_metadata;
    if (block_to_split == list_block_tail) {
        list_block_tail = new_metadata;
    }
    new_metadata->is_free = true;
    if (new_metadata->size >= MMAP_MIN_SIZE) {
        if (new_metadata == mmap_list_block_tail) {
            mmap_list_block_tail = new_metadata->prev;
        }
        if (new_metadata == mmap_list_block_head) {
            mmap_list_block_head = new_metadata->next;
        }
        if (new_metadata->next != nullptr) {
            new_metadata->next->prev = new_metadata->prev;
        }
        if (new_metadata->prev != nullptr) {
            new_metadata->prev->next = new_metadata->next;
        }
        munmap((void*)new_metadata, new_metadata->size + _size_meta_data());
        return;
    }
    size_t index = new_metadata->size / KB;
    MallocMetadata* first_in_bin = free_bins[index];
    if (first_in_bin) {
        first_in_bin->prev_free = new_metadata;
        new_metadata->next_free = first_in_bin;
        new_metadata->prev_free = nullptr;
        free_bins[index] = new_metadata;
    }
    free_bins[index] = new_metadata;
}

static bool merge(MallocMetadata* first , MallocMetadata* second){
    if (first == nullptr or second == nullptr) {
        return false;
    }
    if (not first->is_free or not second->is_free) {
        return false;
    }
    first->size += second->size + _size_meta_data();
    first->next = second->next;
    if (second->next != nullptr) {
        second->next->prev = first;
    }
    if (first->prev_free != nullptr) {
        first->prev_free->next_free = first->next_free;
    }
    if (first->next_free != nullptr) {
        first->next_free->prev_free = first->prev_free;
    }
    if (second->prev_free != nullptr) {
        second->prev_free->next_free = second->next_free;
    }
    if (second->next_free != nullptr) {
        second->next_free->prev_free = second->prev_free;
    }
    if (list_block_tail == second) {
        list_block_tail = first;
    }
    return true;
}

static MallocMetadata *merge_free (MallocMetadata* block_to_merge) {
    merge(block_to_merge , block_to_merge->next);
    if (merge(block_to_merge->prev, block_to_merge)) {
        return block_to_merge->prev;
    }
    return block_to_merge;
}

static void* mmap_create (size_t size) {
    void* new_mmap = mmap(NULL, size + _size_meta_data(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_mmap == (void*)(-1)) {
        return NULL;
    }
    ((MallocMetadata*) new_mmap)->size = size;
    ((MallocMetadata*) new_mmap)->is_free = false;
    ((MallocMetadata*) new_mmap)->address = (void*)((char*)new_mmap + _size_meta_data());
    if (mmap_list_block_head == nullptr){ //case list empty
        mmap_list_block_head = (MallocMetadata*) new_mmap;
        mmap_list_block_head->next = nullptr;
        mmap_list_block_head->prev = nullptr;
        mmap_list_block_tail = mmap_list_block_head;
    } else { // new element added to last place
        /*update prev and next*/
        mmap_list_block_tail->next = (MallocMetadata*) new_mmap;
        ((MallocMetadata*) new_mmap)->next = nullptr;
        ((MallocMetadata*) new_mmap)->prev = mmap_list_block_tail;
        mmap_list_block_tail = ((MallocMetadata*) new_mmap);
    }
    return ((MallocMetadata*) new_mmap)->address;
}

void* smalloc(size_t size) {
    if (size == 0 or size > pow(10,8)) {
        return NULL;
    }
    if (size >= MMAP_MIN_SIZE) {
        return mmap_create(size);
    }
    size_t index = size / KB;
    MallocMetadata* first_in_bin;
    for (; index < BIN_MAX_SIZE; index++) {
        first_in_bin = free_bins[index];
        while (first_in_bin) {
            if (first_in_bin->size >= size) {
                split_block(size, first_in_bin);
                first_in_bin->is_free = false;
                if (first_in_bin->prev_free) {
                    first_in_bin->prev_free->next_free = first_in_bin->next_free;
                }
                if (first_in_bin->next_free) {
                    first_in_bin->next_free->prev_free = first_in_bin->prev_free;
                }
                first_in_bin->next_free = nullptr;
                first_in_bin->prev_free = nullptr;
                return first_in_bin->address;
            }
            first_in_bin = first_in_bin->next_free;
        }
    }
    if (list_block_tail != nullptr and list_block_tail->is_free) {
        if (sbrk(size - list_block_tail->size) == (void *)(-1)) {
            return NULL;
        }
        if (list_block_tail->prev_free == nullptr and list_block_tail->next_free == nullptr) {
            free_bins[list_block_tail->size / KB] = nullptr;
        }
        if (list_block_tail->prev_free != nullptr) {
            list_block_tail->prev_free->next_free = list_block_tail->next_free;
        }
        if (list_block_tail->next_free != nullptr) {
            list_block_tail->next_free->prev_free = list_block_tail->prev_free;
        }
        list_block_tail->is_free = false;
        list_block_tail->size = size;
        return (void*)list_block_tail->address;
    }
    void* prev_prog_break = sbrk(size + _size_meta_data());
    if (prev_prog_break == (void*)(-1)) {
        return NULL;
    }
    ((MallocMetadata*) prev_prog_break)->size = size;
    ((MallocMetadata*) prev_prog_break)->is_free = false;
    ((MallocMetadata*) prev_prog_break)->address = static_cast<char*>(prev_prog_break) + _size_meta_data();
    if (list_block_head == nullptr){ //case list empty
        list_block_head = (MallocMetadata*) prev_prog_break;
        list_block_head->next = nullptr;
        list_block_head->prev = nullptr;
        list_block_tail = list_block_head;
    } else { // new element added to last place
        /*update prev and next*/
        list_block_tail->next = (MallocMetadata*) prev_prog_break;
        ((MallocMetadata*) prev_prog_break)->next = nullptr;
        ((MallocMetadata*) prev_prog_break)->prev = list_block_tail;
        list_block_tail = ((MallocMetadata*) prev_prog_break);
    }
    return ((MallocMetadata*) prev_prog_break)->address;
}

void* scalloc(size_t num, size_t size) {
    if (size == 0 or size * num > pow(10,8)) {
        return NULL;
    }
    void* prev_prog_break = smalloc(num * size);
    if (prev_prog_break == NULL) {
        return NULL;
    }
    void* curr = prev_prog_break;
    memset(curr, 0, num * size);
    return prev_prog_break;
}

void sfree(void* p) {
    if (p == NULL){
        return;
    }
    MallocMetadata* tmp = (MallocMetadata*)p;
    tmp--;
    tmp->is_free = true;
    if (tmp->size >= MMAP_MIN_SIZE) {
        if (tmp == mmap_list_block_tail) {
            mmap_list_block_tail = tmp->prev;
        }
        if (tmp == mmap_list_block_head) {
            mmap_list_block_head = tmp->next;
        }
        if (tmp->next != nullptr) {
            tmp->next->prev = tmp->prev;
        }
        if (tmp->prev != nullptr) {
            tmp->prev->next = tmp->next;
        }
        munmap((void*)tmp->address, tmp->size + _size_meta_data());
        return;
    }
    tmp = merge_free(tmp);
    size_t index = tmp->size / KB;
    MallocMetadata* first_in_bin = free_bins[index];
    if (first_in_bin) {
        first_in_bin->prev_free = tmp;
        tmp->next_free = first_in_bin;
        tmp->prev_free = nullptr;
        free_bins[index] = tmp;
    }
    free_bins[index] = tmp;
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 or size > pow(10,8)) {
        return NULL;
    }
    MallocMetadata* oldp_meta_data = nullptr;
    MallocMetadata* temp = nullptr;
    if (oldp != NULL) {
        oldp_meta_data = (MallocMetadata*)oldp;
        oldp_meta_data--;
    }
    if (size < MMAP_MIN_SIZE) {
        if (oldp_meta_data == list_block_tail) {
            if (sbrk(size - list_block_tail->size) == (void *)(-1)) {
                return NULL;
            }
            if (list_block_tail->prev_free != nullptr) {
                list_block_tail->prev_free->next_free = list_block_tail->next_free;
            }
            if (list_block_tail->next_free != nullptr) {
                list_block_tail->next_free->prev_free = list_block_tail->prev_free;
            }
            list_block_tail->is_free = false;
            list_block_tail->size = size;
            memmove(list_block_tail->address, oldp, oldp_meta_data->size);
            if (list_block_tail->address != oldp) {
                sfree((void*)oldp);
            }
            return list_block_tail->address;
        }
        oldp_meta_data->is_free = true;
        if (oldp_meta_data->size < size) {
            if (oldp_meta_data->prev != nullptr and
                oldp_meta_data->prev->is_free and
                oldp_meta_data->size + oldp_meta_data->prev->size >= size) {
                if (merge(oldp_meta_data->prev, oldp_meta_data)) {
                    temp = oldp_meta_data;
                    oldp_meta_data = oldp_meta_data->prev;
                    if (oldp_meta_data->prev_free == nullptr and oldp_meta_data->next_free == nullptr) {
                        free_bins[oldp_meta_data->size / KB] = nullptr;
                    }
                    if (oldp_meta_data->prev_free != nullptr) {
                        oldp_meta_data->prev_free->next_free = oldp_meta_data->next_free;
                    }
                    if (oldp_meta_data->next_free != nullptr) {
                        oldp_meta_data->next_free->prev_free = oldp_meta_data->prev_free;
                    }
                    oldp_meta_data->next_free = nullptr;
                    oldp_meta_data->prev_free = nullptr;
                }
            } else if (oldp_meta_data->next != nullptr and
                       oldp_meta_data->next->is_free and
                       oldp_meta_data->size + oldp_meta_data->next->size >= size) {
                merge(oldp_meta_data, oldp_meta_data->next);
            }
            else if (oldp_meta_data->next != nullptr and oldp_meta_data->prev != nullptr and
                     oldp_meta_data->next->is_free and oldp_meta_data->prev->is_free and
                     oldp_meta_data->size + oldp_meta_data->next->size + oldp_meta_data->prev->size >= size) {
                temp = oldp_meta_data;
                oldp_meta_data = merge_free(oldp_meta_data);
                if (oldp_meta_data->prev_free == nullptr and oldp_meta_data->next_free == nullptr) {
                    free_bins[oldp_meta_data->size / KB] = nullptr;
                }
                if (oldp_meta_data->prev_free != nullptr) {
                    oldp_meta_data->prev_free->next_free = oldp_meta_data->next_free;
                }
                if (oldp_meta_data->next_free != nullptr) {
                    oldp_meta_data->next_free->prev_free = oldp_meta_data->prev_free;
                }
                oldp_meta_data->next_free = nullptr;
                oldp_meta_data->prev_free = nullptr;
            }
        }
        oldp_meta_data->is_free = false;
        if (oldp_meta_data->size >= size) {
            split_block(size, oldp_meta_data);
            if (temp != nullptr) {
                memmove(oldp_meta_data->address, temp->address, temp->size);
            }
            return oldp_meta_data->address;
        }
        if (list_block_tail != nullptr and list_block_tail->is_free) {
            if (sbrk(size - list_block_tail->size) == (void *)(-1)) {
                return NULL;
            }
            if (list_block_tail->prev_free != nullptr) {
                list_block_tail->prev_free->next_free = list_block_tail->next_free;
            }
            if (list_block_tail->next_free != nullptr) {
                list_block_tail->next_free->prev_free = list_block_tail->prev_free;
            }
            list_block_tail->is_free = false;
            list_block_tail->size = size;
            memmove(list_block_tail->address, oldp, oldp_meta_data->size);
            if (list_block_tail->address != oldp) {
                sfree((void*)oldp);
            }
            return list_block_tail->address;
        }
    }
    void* prev_prog_break = smalloc(size);
    if(oldp != NULL and prev_prog_break != NULL){
        if (size < oldp_meta_data->size) {
            memmove(prev_prog_break,oldp,size);
        }
        else {
            memmove(prev_prog_break,oldp,oldp_meta_data->size);
        }
        sfree((void*)oldp);
    }
    return prev_prog_break;
}

size_t _num_free_blocks() {
    MallocMetadata* tmp = list_block_head;
    size_t count_of_free_blocks = 0;
    while(tmp) {
        if(tmp->is_free) {
            ++count_of_free_blocks;
        }
        tmp = tmp->next;
    }
    return count_of_free_blocks;
}

size_t _num_free_bytes() {
    MallocMetadata* tmp = list_block_head;
    size_t num_of_free_bytes = 0;
    while(tmp) {
        if (tmp->is_free) {
            num_of_free_bytes += tmp->size;
        }
        tmp = tmp->next;
    }
    return num_of_free_bytes;
}

size_t _num_allocated_blocks() {
    MallocMetadata* block_tmp = list_block_head;
    MallocMetadata* mmap_tmp = mmap_list_block_head;
    size_t count_of_allocated_blocks = 0;
    while(block_tmp) {
        ++count_of_allocated_blocks;
        block_tmp = block_tmp->next;
    }
    while(mmap_tmp) {
        ++count_of_allocated_blocks;
        mmap_tmp = mmap_tmp->next;
    }
    return count_of_allocated_blocks;
}

size_t _num_allocated_bytes() {
    MallocMetadata* block_tmp = list_block_head;
    MallocMetadata* mmap_tmp = mmap_list_block_head;
    size_t num_of_allocated_bytes = 0;
    while(block_tmp) {
        num_of_allocated_bytes += block_tmp->size;
        block_tmp = block_tmp->next;
    }
    while(mmap_tmp) {
        num_of_allocated_bytes += mmap_tmp->size;
        mmap_tmp = mmap_tmp->next;
    }
    return num_of_allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks()*_size_meta_data();
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}