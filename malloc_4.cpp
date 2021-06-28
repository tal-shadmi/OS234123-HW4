#include <cstring>
#include <unistd.h>
#include <cmath>
#include <cstddef>
#include <sys/mman.h>
#include "malloc_3.h"

using std::memset;
using std::memcpy;
using std::ceil;
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

size_t aligned_size(size_t old_size){
    return ceil(float(old_size)/float(8))*8;
}


size_t _num_free_blocks();

size_t _num_free_bytes();

size_t _num_allocated_blocks();

size_t _num_allocated_bytes();

size_t _num_meta_data_bytes();

size_t _size_meta_data();

void split_block(size_t size, MallocMetadata* p) {
    if (p->size - size - _size_meta_data() < MIN_SPLIT ) {
        return;
    }
    size_t size_aligned = aligned_size(size);
    size_t size_left = p->size - size_aligned - _size_meta_data();
    //size_t old_size = block_to_split->size;
    //block_to_split->size = aligned_size(size);
    if(size_left>=MIN_SPLIT) {
        void* temp = static_cast<char*>(p->address) + (size_aligned);
        MallocMetadata* new_md = static_cast<MallocMetadata*>(temp);
        new_md->address = static_cast<char*>(temp) + _size_meta_data();
        new_md->size = size_left;
        //new_md->is_free = true;
        //MallocMetadata *new_metadata = (MallocMetadata *) ((char *) p->address + size);
        //new_metadata->address = static_cast<char *>new_metadata + _size_meta_data();
        new_metadata->size = size_aligned;
        MallocMetadata *tmp = p->next;
        new_md->next = tmp;
        new_md->prev = p;
        p->next = new_md;
        sfree((void *) new_md);
    }
}

bool merge(MallocMetadata* first , MallocMetadata* second){
    if ((first == nullptr or second == nullptr)) {
        return false;
    }
    if (not first->is_free or not second->is_free) {
        return false;
    }
    first->size += second->size + _size_meta_data();
    first->next = second->next;
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
    return true;
}

MallocMetadata *merge_free (MallocMetadata* block_to_merge) {
    merge(block_to_merge , block_to_merge->next);
    if (merge(block_to_merge->prev, block_to_merge)) {
        return block_to_merge->prev;
    }
    return block_to_merge;
}

void* mmap_create (size_t size) {
    void* new_mmap = mmap(NULL, size + _size_meta_data(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS, -1, 0);
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
    size_t size_aligned = aligned_size(size);
    if (size_aligned == 0 or size_aligned > pow(10,8)) {
        return NULL;
    }
    if (size_aligned > MMAP_MIN_SIZE) {
        return mmap_create(size_aligned);
    }
    size_t index = size_aligned / KB;
    MallocMetadata* first_in_bin;
    for (; index < BIN_MAX_SIZE; index++) {
        first_in_bin = free_bins[index];
        while (first_in_bin) {
            if (first_in_bin->size >= size_aligned) {
                split_block(size_aligned, first_in_bin);
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
    if (list_block_tail->is_free) {
        if (sbrk(size_aligned - list_block_tail->size) == (void *)(-1)) {
            return NULL;
        }
        if (list_block_tail->prev_free != nullptr) {
            list_block_tail->prev_free->next_free = list_block_tail->next_free;
        }
        if (list_block_tail->next_free != nullptr) {
            list_block_tail->next_free->prev_free = list_block_tail->prev_free;
        }
        list_block_tail->is_free = false;
        list_block_tail->size = size_aligned;
        return list_block_tail;
    }
    void* prev_prog_break = sbrk(size_aligned + _size_meta_data());
    if (prev_prog_break == (void*)(-1)) {
        return NULL;
    }
    ((MallocMetadata*) prev_prog_break)->size = size_aligned;
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
        if (tmp->next != nullptr) {
            tmp->next->prev = tmp->prev;
        }
        if (tmp->prev != nullptr) {
            tmp->prev->next = tmp->next;
        }
        munmap((void*)tmp, tmp->size + _size_meta_data());
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
    size_t size_aligned = aligned_size(size);
    if (size_aligned == 0 or size_aligned > pow(10,8)) {
        return NULL;
    }
    MallocMetadata* oldp_meta_data = nullptr;
    if (oldp != NULL) {
        oldp_meta_data = (MallocMetadata*)oldp;
        oldp_meta_data--;
    }
    oldp_meta_data->is_free = true;
    if (oldp_meta_data->size <= size_aligned) {
        if (oldp_meta_data->prev != nullptr and
            oldp_meta_data->prev->is_free and
            oldp_meta_data->size + oldp_meta_data->prev->size >= size_aligned) {
            if (merge(oldp_meta_data->prev, oldp_meta_data)) {
                oldp_meta_data = oldp_meta_data->prev;
            }
        } else if (oldp_meta_data->next != nullptr and
                   oldp_meta_data->next->is_free and
                   oldp_meta_data->size + oldp_meta_data->next->size >= size_aligned) {
            merge(oldp_meta_data, oldp_meta_data->next);
        }
        else if (oldp_meta_data->next != nullptr and oldp_meta_data->prev != nullptr and
                 oldp_meta_data->next->is_free and oldp_meta_data->prev->is_free and
                 oldp_meta_data->size + oldp_meta_data->next->size + oldp_meta_data->prev->size >= size_aligned) {
            oldp_meta_data = merge_free(oldp_meta_data);
        }
    }
    oldp_meta_data->is_free = false;
    if (oldp_meta_data->size >= size_aligned) {
        split_block(size_aligned, oldp_meta_data);
        return oldp_meta_data->address;
    }
    if (list_block_tail->is_free) {
        if (sbrk(size_aligned - list_block_tail->size) == (void *)(-1)) {
            return NULL;
        }
        if (list_block_tail->prev_free != nullptr) {
            list_block_tail->prev_free->next_free = list_block_tail->next_free;
        }
        if (list_block_tail->next_free != nullptr) {
            list_block_tail->next_free->prev_free = list_block_tail->prev_free;
        }
        list_block_tail->is_free = false;
        list_block_tail->size = size_aligned;
        return list_block_tail;
    }
    void* prev_prog_break = smalloc(size_aligned);
    if(oldp != NULL and prev_prog_break != NULL){
        memcpy(prev_prog_break,oldp,size);
        oldp_meta_data->is_free = true;
        size_t index = oldp_meta_data->size / KB;
        MallocMetadata* first_in_bin = free_bins[index];
        if (first_in_bin) {
            first_in_bin->prev_free = oldp_meta_data;
            oldp_meta_data->next_free = first_in_bin;
            oldp_meta_data->prev_free = nullptr;
            free_bins[index] = oldp_meta_data;
        }
        free_bins[index] = oldp_meta_data;
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
    MallocMetadata* tmp = list_block_head;
    size_t count_of_allocated_blocks = 0;
    while(tmp) {
        ++count_of_allocated_blocks;
        tmp = tmp->next;
    }
    return count_of_allocated_blocks;
}

size_t _num_allocated_bytes() {
    MallocMetadata* tmp = list_block_head;
    size_t num_of_allocated_bytes = 0;
    while(tmp) {
        num_of_allocated_bytes += tmp->size;
        tmp = tmp->next;
    }
    return num_of_allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks()*_size_meta_data();
}

size_t _size_meta_data() {
    return aligned_size(sizeof(MallocMetadata));
}