#include <cstring>
#include <unistd.h>
#include <cmath>
#include <cstddef>

using std::memset;
using std::memcpy;

struct MallocMetadata{
    size_t size;
    bool is_free;
    void* address;
    MallocMetadata* next;
    MallocMetadata* prev;
    MallocMetadata* next_free;
    MallocMetadata* prev_free;
};

static MallocMetadata* list_head = nullptr;
static MallocMetadata* free_bins[128] = {nullptr};

size_t _num_free_blocks();

size_t _num_free_bytes();

size_t _num_allocated_blocks();

size_t _num_allocated_bytes();

size_t _num_meta_data_bytes();

size_t _size_meta_data();

void* smalloc(size_t size) {
    if (size == 0 or size > pow(10,8)) {
        return NULL;
    }
    size_t index = size / 1000;
    MallocMetadata* first_in_bin = free_bins[index];
    while (first_in_bin) {
        if (first_in_bin->size >= size) {
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
    void* prev_prog_break = sbrk(size + _size_meta_data());
    if (prev_prog_break == (void*)(-1)) {
        return NULL;
    }
    ((MallocMetadata*) prev_prog_break)->size = size;
    ((MallocMetadata*) prev_prog_break)->is_free = false;
    ((MallocMetadata*) prev_prog_break)->address = (void*)((MallocMetadata*)prev_prog_break + _size_meta_data());
    if (list_head == nullptr){ //case list empty
        list_head = (MallocMetadata*) prev_prog_break;
        list_head->next = nullptr;
        list_head->prev = nullptr;
    } else { // new element added to last place
        /*update prev and next*/
        list_head->prev = (MallocMetadata*) prev_prog_break;
        ((MallocMetadata*) prev_prog_break)->next = list_head;
        ((MallocMetadata*) prev_prog_break)->prev = nullptr;
        list_head = ((MallocMetadata*) prev_prog_break);
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
    tmp = tmp - _size_meta_data();
    tmp->is_free= true;
    size_t index = tmp->size / 1000;
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
    if (oldp != NULL) {
        oldp_meta_data = (MallocMetadata*)oldp;
        oldp_meta_data = oldp_meta_data - _size_meta_data();
        if (oldp_meta_data->size >= size) {
            return oldp;
        }
    }
    void* prev_prog_break = smalloc(size);
    if(oldp != NULL and prev_prog_break != NULL){
        memcpy(prev_prog_break,oldp,size);
        oldp_meta_data->is_free = true;
        size_t index = oldp_meta_data->size / 1000;
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
    MallocMetadata* tmp = list_head;
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
    MallocMetadata* tmp = list_head;
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
    MallocMetadata* tmp = list_head;
    size_t count_of_allocated_blocks = 0;
    while(tmp) {
        ++count_of_allocated_blocks;
        tmp = tmp->next;
    }
    return count_of_allocated_blocks;
}

size_t _num_allocated_bytes() {
    MallocMetadata* tmp = list_head;
    size_t num_of_allocated_bytes = 0;
    while(tmp) {
        if (!tmp->is_free) {
            num_of_allocated_bytes += tmp->size;
        }
        tmp = tmp->next;
    }
    return num_of_allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return _num_allocated_blocks()*_size_meta_data();
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}