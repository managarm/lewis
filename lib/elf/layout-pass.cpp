// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <climits>
#include <iostream>
#include <elf.h>
#include <lewis/elf/passes.hpp>
#include <lewis/elf/utils.hpp>

namespace {
    constexpr bool verbose = false;

    // Adapted from Bit Twiddling Hacks.
    template <typename T>
    T ceil2Power(T v) {
        static_assert(std::is_unsigned<T>::value);
        v--;
        for (size_t i = 1; i < sizeof(v) * CHAR_BIT; i *= 2)
            v |= v >> i;
        return ++v;
    }

    uint32_t elf64Hash(const std::string &s) {
        uint32_t h = 0;
        for(size_t i = 0; i < s.size(); ++i) {
            h = (h << 4) + (uint8_t)s[i];
            uint32_t g = h & 0xF0000000;
            if(g)
                h ^= g >> 24;
            h &= 0x0FFFFFFF;
        }
        return h;
    }
}

namespace lewis::elf {

struct LayoutPassImpl : LayoutPass {
    LayoutPassImpl(Object *elf)
    : _elf{elf} { }

    void run() override;

private:
    Object *_elf;
};

void LayoutPassImpl::run() {
    size_t sectionIndex = 1;
    size_t offset = 64; // Size of EHDR.
    size_t address = 0x1000;
    if(verbose)
        std::cout << "Running LayoutPass" << std::endl;
    for (auto fragment : _elf->fragments()) {
        size_t size;
        if (auto phdrs = hierarchy_cast<PhdrsFragment *>(fragment); phdrs) {
            // TODO: # of PHDRs should be independent of # of sections.
            size = (_elf->numberOfFragments() + 1) * sizeof(Elf64_Phdr);
        } else if (auto shdrs = hierarchy_cast<ShdrsFragment *>(fragment); shdrs) {
            size = (1 + _elf->numberOfSections()) * sizeof(Elf64_Shdr);
        } else if (auto dynamic = hierarchy_cast<DynamicSection *>(fragment); dynamic) {
            size = 6 * 16;
        } else if (auto strtab = hierarchy_cast<StringTableSection *>(fragment); strtab) {
            size = 1; // ELF uses index zero for non-existent strings.
            for (auto string : _elf->strings()) {
                string->designatedOffset = size;
                size += string->buffer.size() + 1;
            }
        } else if (auto symtab = hierarchy_cast<SymbolTableSection *>(fragment); symtab) {
            size_t numEntries = 1; // ELF uses index zero for non-existent symbols.
            for (auto symbol : _elf->symbols()) {
                symbol->designatedIndex = numEntries;
                numEntries++;
            }
            size = sizeof(Elf64_Sym) * numEntries;
        } else if (auto rel = hierarchy_cast<RelocationSection *>(fragment); rel) {
            size_t numEntries = 0;
            for (auto relocation : _elf->relocations()) {
                relocation->designatedIndex = numEntries;
                numEntries++;
            }
            size = sizeof(Elf64_Rela) * numEntries;
        } else if (auto hash = hierarchy_cast<HashSection *>(fragment); hash) {
            size_t tableSize = ceil2Power(_elf->symbols().size());
            hash->buckets.resize(tableSize, 0);
            hash->chains.resize(_elf->symbols().size() + 1);

            struct BucketData {
                size_t tail;
                size_t collisions;
            };

            std::vector<BucketData> bucketData;
            bucketData.resize(tableSize, BucketData{0, 0});

            size_t maxCollisions = 0;
            for (auto symbol : _elf->symbols()) {
                assert(symbol->designatedIndex.has_value() && "Symbol layout needs to be fixed"
                        " before hash table is realized.");

                auto b = elf64Hash(symbol->name->buffer) & (tableSize - 1);
                if (auto t = bucketData[b].tail; !t) {
                    hash->buckets[b] = symbol;
                    bucketData[b].tail = symbol->designatedIndex.value();
                }else{
                    hash->chains[t] = symbol;
                    bucketData[b].tail = symbol->designatedIndex.value();
                    bucketData[b].collisions++;
                    maxCollisions = std::max(maxCollisions, bucketData[b].collisions);
                }
            }

            if(verbose)
                std::cout << "ELF hash table of size " << tableSize
                        << " contains " << _elf->symbols().size()
                        << " symbols; there are at most " << maxCollisions
                        << " collisions" << std::endl;
        } else {
            auto section = hierarchy_cast<ByteSection *>(fragment);
            assert(section && "Unexpected ELF fragment");
            size = section->buffer.size();
        }

        if(verbose)
            std::cout << "Laying out fragment " << fragment << " at " << (void *)offset
                    << ", size: " << (void *)size << std::endl;
        if (fragment->isSection())
            fragment->designatedIndex = sectionIndex++;

        // Make sure that sections are at least 8-byte aligned.
        // TODO: Support arbitrary alignment.
        offset = (offset + 7) & ~size_t(7);

        // TODO: Perform virtualAddress allocation per segment and not per section.
        // Make sure each segment starts on it's own page.
        address = (address + 0xFFF) & ~size_t{0xFFF};
        // Make sure virtualAddress and fileOffset match modulo page size.
        address += offset & 0xFFF;

        fragment->fileOffset = offset;
        fragment->virtualAddress = address;
        fragment->computedSize = size;
        offset += size;
    }
}

std::unique_ptr<LayoutPass> LayoutPass::create(Object *elf) {
    return std::make_unique<LayoutPassImpl>(elf);
}

} // namespace lewis::elf
