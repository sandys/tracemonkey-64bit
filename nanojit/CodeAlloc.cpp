/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set ts=4 sw=4 expandtab: (add to ~/.vimrc: set modeline modelines=5) */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is [Open Source Virtual Machine].
 *
 * The Initial Developer of the Original Code is
 * Adobe System Incorporated.
 * Portions created by the Initial Developer are Copyright (C) 2004-2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Adobe AS3 Team
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nanojit.h"

//#define DOPROF
#include "../vprof/vprof.h"

#ifdef FEATURE_NANOJIT

namespace nanojit
{
    static const bool verbose = false;
    static const int pagesPerAlloc = 1;
    static const int bytesPerAlloc = pagesPerAlloc * GCHeap::kBlockSize;

    CodeAlloc::CodeAlloc(GCHeap* heap)
        : heap(heap), heapblocks(0)
    {}

    CodeAlloc::~CodeAlloc() {
        // give all memory back to gcheap.  Assumption is that all
        // code is done being used by now.
        for (CodeList* b = heapblocks; b != 0; ) {
            _nvprof("free page",1);
            CodeList* next = b->next;
            void *mem = firstBlock(b);
            VMPI_setPageProtection(mem, bytesPerAlloc, false /* executable */, true /* writable */);
            heap->Free(mem);
            b = next;
        }
    }

    CodeList* CodeAlloc::firstBlock(CodeList* term) {
        // fragile but correct as long as we allocate one block at a time.
        return (CodeList*) alignTo(term, bytesPerAlloc);
    }

    int round(size_t x) {
        return (int)((x + 512) >> 10);
    }
    void CodeAlloc::logStats() {
        size_t total = 0;
        size_t frag_size = 0;
        size_t free_size = 0;
        int free_count = 0;
        for (CodeList* hb = heapblocks; hb != 0; hb = hb->next) {
            total += bytesPerAlloc;
            for (CodeList* b = hb->lower; b != 0; b = b->lower) {
                if (b->isFree) {
                    free_count++;
                    free_size += b->blockSize();
                    if (b->size() < minAllocSize)
                        frag_size += b->blockSize();
                }
            }
        }
        avmplus::AvmLog("code-heap: %dk free %dk fragmented %d\n",
            round(total), round(free_size), frag_size);
    }

    void CodeAlloc::alloc(NIns* &start, NIns* &end) {
        // search each heap block for a free block
        for (CodeList* hb = heapblocks; hb != 0; hb = hb->next) {
            // check each block to see if it's free and big enough
            for (CodeList* b = hb->lower; b != 0; b = b->lower) {
                if (b->isFree && b->size() >= minAllocSize) {
                    // good block
                    b->isFree = false;
                    start = b->start();
                    end = b->end;
                    if (verbose)
                        avmplus::AvmLog("alloc %p-%p %d\n", start, end, int(end-start));
                    return;
                }
            }
        }
        // no suitable block found, get more memory
        void *mem = heap->Alloc(pagesPerAlloc);  // allocations never fail
        _nvprof("alloc page", uintptr_t(mem)>>12);
        VMPI_setPageProtection(mem, bytesPerAlloc, true/*executable*/, true/*writable*/);
        CodeList* b = addMem(mem, bytesPerAlloc);
        b->isFree = false;
        start = b->start();
        end = b->end;
        if (verbose)
            avmplus::AvmLog("alloc %p-%p %d\n", start, end, int(end-start));
    }

    void CodeAlloc::free(NIns* start, NIns *end) {
        CodeList *blk = getBlock(start, end);
        if (verbose)
            avmplus::AvmLog("free %p-%p %d\n", start, end, (int)blk->size());

		AvmAssert(!blk->isFree); 

        // coalesce
        if (blk->lower && blk->lower->isFree) {
            // combine blk into blk->lower (destroy blk)
            CodeList* lower = blk->lower;
            CodeList* higher = blk->higher;
            lower->higher = higher;
            higher->lower = lower;
            debug_only( sanity_check();)
            blk = lower;
        }
        // the last block in each heapblock is never free, therefore blk->higher != null
        if (blk->higher->isFree) {
            // combine blk->higher into blk (destroy blk->higher)
            CodeList *higher = blk->higher->higher;
            blk->higher = higher;
            higher->lower = blk;
            debug_only(sanity_check();)
        }
        blk->isFree = true;
        NanoAssert(!blk->lower || !blk->lower->isFree);
        NanoAssert(blk->higher && !blk->higher->isFree);
        //memset(blk->start(), 0xCC, blk->size()); // INT 3 instruction
    }

    void CodeAlloc::sweep() {
        debug_only(sanity_check();)
        CodeList** prev = &heapblocks;
        for (CodeList* hb = heapblocks; hb != 0; hb = *prev) {
            NanoAssert(hb->lower != 0);
            if (!hb->lower->lower && hb->lower->isFree) {
                // whole page is unused
                void* mem = hb->lower;
                *prev = hb->next;
                _nvprof("free page",1);
                VMPI_setPageProtection(mem, bytesPerAlloc, false /* executable */, true /* writable */);
                heap->Free(mem);
            } else {
                prev = &hb->next;
            }
        }
    }

    void CodeAlloc::freeAll(CodeList* &code) {
        while (code) {
            CodeList *b = removeBlock(code);
            free(b->start(), b->end);
        }
    }

#if defined(AVMPLUS_UNIX) && defined(NANOJIT_ARM)
#include <asm/unistd.h>
extern "C" void __clear_cache(char *BEG, char *END);
#endif

#ifdef AVMPLUS_SPARC
extern  "C"	void sync_instruction_memory(caddr_t v, u_int len);
#endif

#if defined NANOJIT_IA32 || defined NANOJIT_X64
    // intel chips have dcache/icache interlock
	void CodeAlloc::flushICache(CodeList* &)
    {}

#elif defined NANOJIT_ARM && defined UNDER_CE
    // on arm/winmo, just flush the whole icache
    // fixme: why?
	void CodeAlloc::flushICache(CodeList* &) {
		// just flush all of it
		FlushInstructionCache(GetCurrentProcess(), NULL, NULL);
    }

#elif defined AVMPLUS_MAC && defined NANOJIT_PPC

#  ifdef NANOJIT_64BIT
    extern "C" void sys_icache_invalidate(const void*, size_t len);
    extern "C" void sys_dcache_flush(const void*, size_t len);

    // mac 64bit requires 10.5 so use that api
	void CodeAlloc::flushICache(CodeList* &blocks) {
        for (CodeList *b = blocks; b != 0; b = b->next) {
            void *start = b->start();
            size_t bytes = b->size();
            sys_dcache_flush(start, bytes);
            sys_icache_invalidate(start, bytes);
		}
    }
#  else
    // mac ppc 32 could be 10.0 or later
    // uses MakeDataExecutable() from Carbon api, OSUtils.h
    // see http://developer.apple.com/documentation/Carbon/Reference/Memory_Manag_nt_Utilities/Reference/reference.html#//apple_ref/c/func/MakeDataExecutable
	void CodeAlloc::flushICache(CodeList* &blocks) {
        for (CodeList *b = blocks; b != 0; b = b->next)
            MakeDataExecutable(b->start(), b->size());
    }
#  endif

#elif defined AVMPLUS_SPARC
    // fixme: sync_instruction_memory is a solaris api, test for solaris not sparc
	void CodeAlloc::flushICache(CodeList* &blocks) {
        for (CodeList *b = blocks; b != 0; b = b->next)
			sync_instruction_memory((char*)b->start(), b->size());
    }

#elif defined AVMPLUS_UNIX
    // fixme: __clear_cache is a libgcc feature, test for libgcc or gcc 
	void CodeAlloc::flushICache(CodeList* &blocks) {
        for (CodeList *b = blocks; b != 0; b = b->next) {
			__clear_cache((char*)b->start(), (char*)b->start()+b->size());
		}
    }
#endif // AVMPLUS_MAC && NANOJIT_PPC

    void CodeAlloc::addBlock(CodeList* &blocks, CodeList* b) {
        b->next = blocks;
        blocks = b;
    }

    CodeList* CodeAlloc::addMem(void *mem, size_t bytes) {
        CodeList* b = (CodeList*)mem;
        b->lower = 0;
        b->end = (NIns*) (uintptr_t(mem) + bytes - sizeofMinBlock);
        b->next = 0;
        b->isFree = true;

        // create a tiny terminator block, add to fragmented list, this way
        // all other blocks have a valid block at b->higher
        CodeList* terminator = b->higher;
        terminator->lower = b;
        terminator->end = 0; // this is how we identify the terminator
        terminator->isFree = false;
        debug_only(sanity_check();)
        
        // add terminator to heapblocks list so we can track whole blocks
        addBlock(heapblocks, terminator);
        return b;
    }

    CodeList* CodeAlloc::getBlock(NIns* start, NIns* end) {
        CodeList* b = (CodeList*) (uintptr_t(start) - offsetof(CodeList, code));
        NanoAssert(b->end == end && b->next == 0); (void) end;
        return b;
    }

    CodeList* CodeAlloc::removeBlock(CodeList* &blocks) {
        CodeList* b = blocks;
        blocks = b->next;
        b->next = 0;
        return b;
    }

    void CodeAlloc::add(CodeList* &blocks, NIns* start, NIns* end) {
        addBlock(blocks, getBlock(start, end));
    }

    /**
     * split a block by freeing the hole in the middle defined by [holeStart,holeEnd),
     * and adding the used prefix and suffix parts to the blocks CodeList.
     */
    void CodeAlloc::addRemainder(CodeList* &blocks, NIns* start, NIns* end, NIns* holeStart, NIns* holeEnd) {
        NanoAssert(start < end && start <= holeStart && holeStart <= holeEnd && holeEnd <= end);
        // shrink the hole by aligning holeStart forward and holeEnd backward
        holeStart = (NIns*) ((uintptr_t(holeStart) + sizeof(NIns*)-1) & ~(sizeof(NIns*)-1));
        holeEnd = (NIns*) (uintptr_t(holeEnd) & ~(sizeof(NIns*)-1));
        size_t minHole = minAllocSize;
        if (minHole < 2*sizeofMinBlock)
            minHole = 2*sizeofMinBlock;
        if (uintptr_t(holeEnd) - uintptr_t(holeStart) < minHole) {
            // the hole is too small to make a new free block and a new used block. just keep
            // the whole original block and don't free anything.
            add(blocks, start, end);
        } else if (holeStart == start && holeEnd == end) {
            // totally empty block.  free whole start-end range
            this->free(start, end);
        } else if (holeStart == start) {
            // hole is lower-aligned with start, so just need one new block
            // b1 b2
            CodeList* b1 = getBlock(start, end);
            CodeList* b2 = (CodeList*) (uintptr_t(holeEnd) - offsetof(CodeList, code));
            b2->isFree = false;
            b2->next = 0;
            b2->higher = b1->higher;
            b2->lower = b1;
            b2->higher->lower = b2;
            b1->higher = b2;
            debug_only(sanity_check();)
            this->free(b1->start(), b1->end);
            addBlock(blocks, b2);
        } else if (holeEnd == end) {
            // hole is right-aligned with end, just need one new block
            // todo
            NanoAssert(false);
        } else {
            // there's enough space left to split into three blocks (two new ones)
            CodeList* b1 = getBlock(start, end);
            CodeList* b2 = (CodeList*) holeStart;
            CodeList* b3 = (CodeList*) (uintptr_t(holeEnd) - offsetof(CodeList, code));
            b1->higher = b2;
            b2->lower = b1;
            b2->higher = b3;
            b2->isFree = false; // redundant, since we're about to free, but good hygiene
            b3->lower = b2;
            b3->end = end;
            b3->isFree = false;
            b3->higher->lower = b3;
            b2->next = 0;
            b3->next = 0;
            debug_only(sanity_check();)
            this->free(b2->start(), b2->end);
            addBlock(blocks, b3);
            addBlock(blocks, b1);
        }
    }

    size_t CodeAlloc::size(const CodeList* blocks) {
        size_t size = 0;
        for (const CodeList* b = blocks; b != 0; b = b->next)
            size += int((uintptr_t)b->end - (uintptr_t)b);
        return size;
    }

    bool CodeAlloc::contains(const CodeList* blocks, NIns* p) {
        for (const CodeList *b = blocks; b != 0; b = b->next) {
            _nvprof("block contains",1);
            if (b->contains(p))
                return true;
        }
        return false;
    }

    void CodeAlloc::moveAll(CodeList* &blocks, CodeList* &other) {
        if (other) {
            CodeList* last = other;
            while (last->next)
                last = last->next;
            last->next = blocks;
            blocks = other;
            other = 0;
        }
    }

    // figure out whether this is a pointer into allocated/free code,
    // or something we don't manage.
    CodeAlloc::CodePointerKind CodeAlloc::classifyPtr(NIns *p) {
        for (CodeList* hb = heapblocks; hb != 0; hb = hb->next) {
            CodeList* b = firstBlock(hb);
            if (!containsPtr((NIns*)b, (NIns*)((uintptr_t)b + bytesPerAlloc), p))
                continue;
            do {
                if (b->contains(p))
                    return b->isFree ? kFree : kUsed;
            } while ((b = b->higher) != 0);
        }
        return kUnknown;
    }

    // check that all block neighbors are correct
    #ifdef _DEBUG
    void CodeAlloc::sanity_check() {
        for (CodeList* hb = heapblocks; hb != 0; hb = hb->next) {
            NanoAssert(hb->higher == 0);
            for (CodeList* b = hb->lower; b != 0; b = b->lower) {
                NanoAssert(b->higher->lower == b);
            }
        }
    }
    #endif
}
#endif // FEATURE_NANOJIT
