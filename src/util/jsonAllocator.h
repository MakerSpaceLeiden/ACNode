#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_debug_helpers.h>

// Just for logging.
#include "ACNode.h"
#include "util/log_backtrace.h"

#ifndef _jsonAllocator_H
#define _jsonAllocator_H

static unsigned int _max = 0;

// We're struggling with very fragmented heaps. There are two
// likely culprints - the TLS stack (which we cannot really change)
// and the JSON stack. This customer allocator tries two things - 
// first to see if it can use PSRAM. And if that fails - it tries
// to rely on a fixed, capped, buffer which is never released, 
// and if it could not get that (_SIZE stays 0) it falls back
// on whatever the default malloc/free/realloc is.
//
// We rely on the fact that we know that there should not be
// any nested json going on; and there should be no dangling
// jsons eithers -- so we always return to an empty state with
// never more than one doc. We've not yet wrapped JsonDocument
// in a singleton to police this.
//
// This last assumption does not actually hold - it is possible
// for a web hook callback to hit exactly in the middle of
// a timed reporting (on the other core). So at that time
// we have a genuine need for double the allocation (or need
// to error out of either of the two work streams). 

struct SpiRamAllocator : ArduinoJson::Allocator {
public:
#if 1
  SpiRamAllocator() { return ;};
  SpiRamAllocator(size_t s) { return ; };
  void* allocate(size_t size) override { return malloc(size); };
  void deallocate(void* ptr) override { free(ptr); };
  void* reallocate(void* ptr, size_t new_size) override { return realloc(ptr, new_size); };
#else
  SpiRamAllocator() : SpiRamAllocator(6 * 1024) {};
  SpiRamAllocator(size_t s) {
	if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
		return;
	if (!(_buff = (unsigned char*) malloc(s))) {
		return;
	};
        _ptr = _buff;
	_SIZE = s;
   };

  void* allocate(size_t size) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
         return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

    if (!_SIZE)
        return malloc(size);

    if ((unsigned char *)_ptr + size > _buff + _SIZE) {
	Log.printf("JSON Allocator - out of memory (%u claimed, needs %u extra)\n", _SIZE,size);
        log_backtrace(&Debug);
	return NULL;
    };

    void * ptr = _ptr; 
    _ptr = (unsigned char *)_ptr + size;

    return ptr;
  }

  void deallocate(void* ptr) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
       heap_caps_free(ptr);

    if (!_SIZE)
       free(ptr);

    if (ptr != _buff)
	return;

    if ((unsigned char *)_ptr - _buff > _max)
	_max = (unsigned char *)_ptr - _buff;

    Debug.printf("JSON Allocator - used %u; peak %u\n", (unsigned char *)_ptr - _buff, _max);

    _ptr = _buff;
  }

  void* reallocate(void* ptr, size_t new_size) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);

    if (!_SIZE)
        return realloc(ptr,new_size);


    // We cannot really support a re-alloc - as we cannot change the 'mid' something
    // pointers. All we can do is add a second block or something. Not yet implemented.
    //
    Log.println("JSON Allocator - unsupported realloc()");
    log_backtrace(&Log);
    return NULL;
  };
 
private:
  size_t _SIZE = 0;
  unsigned char * _buff;
  void  * _ptr = NULL;
#endif
};
extern SpiRamAllocator jsonAllocator;
#endif
