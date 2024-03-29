//
// Created by Dell on 24/06/2021.
//

#ifndef OS234123_HW4_MALLOC_3_H
#define OS234123_HW4_MALLOC_3_H

size_t _size_meta_data();

size_t _num_free_blocks() ;


size_t _num_free_bytes();


size_t _num_allocated_blocks() ;

size_t _num_allocated_bytes();


size_t _num_meta_data_bytes() ;

void* smalloc(size_t size) ;

void* scalloc(size_t num, size_t size) ;

void sfree(void* p) ;

void* srealloc(void* oldp, size_t size) ;

#endif //OS234123_HW4_MALLOC_3_H
