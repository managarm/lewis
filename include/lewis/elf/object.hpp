// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <frg/list.hpp>
#include <lewis/hierarchy.hpp>

namespace lewis::elf {

using FragmentKindType = uint32_t;

namespace fragment_kinds {
    enum : FragmentKindType {
        null,
        phdrsFragment,
        shdrsFragment,
        // All Fragments are byteSection are considered Sections (see Fragment::isSection()).
        byteSection,
        dynamicSection,
        stringTableSection,
        symbolTableSection,
        relocationSection,
        hashSection
    };
}

struct Fragment;

// Represents a single "use" of a Fragment. This is necessary to support Fragment replacement.
// For convenience, this class also has a Fragment pointer-like interface.
struct FragmentUse {
    friend struct Fragment;

    FragmentUse()
    : _ref{nullptr} { }

    FragmentUse(const FragmentUse &) = delete;

    FragmentUse &operator= (const FragmentUse &) = delete;

    void assign(Fragment *f);

    Fragment *get() {
        return _ref;
    }

    // The following operators define the pointer-like interface.

    FragmentUse &operator= (Fragment *f) {
        assign(f);
        return *this;
    }

    bool operator== (Fragment *f) { return _ref == f; }
    bool operator!= (Fragment *f) { return _ref != f; }

    explicit operator bool () { return _ref; }

    Fragment &operator* () { return *_ref; }
    Fragment *operator-> () { return _ref; }

private:
    Fragment *_ref;
    frg::default_list_hook<FragmentUse> _useListHook;
};

struct String;

// Represents a fragment of an ELF file.
// Before ELF emission, all Fragments need to be converted to Sections.
struct Fragment {
    friend struct FragmentUse;

    Fragment(FragmentKindType kind_)
    : kind{kind_} { }

    Fragment(const Fragment &) = delete;

    virtual ~Fragment() = default;

    Fragment &operator= (const Fragment &) = delete;

    bool isSection() {
        return kind >= fragment_kinds::byteSection;
    }

    void replaceAllUses(Fragment *other);

    const FragmentKindType kind;

public:
    String *name = nullptr;
    uint32_t type = 0;
    uint32_t flags = 0;
    std::optional<size_t> designatedIndex;
    std::optional<uintptr_t> fileOffset;
    std::optional<uintptr_t> virtualAddress;
    std::optional<uintptr_t> computedSize;
    FragmentUse sectionLink;
    std::optional<size_t> sectionInfo;
    std::optional<size_t> entrySize;

private:
    frg::intrusive_list<
        FragmentUse,
        frg::locate_member<
            FragmentUse,
            frg::default_list_hook<FragmentUse>,
            &FragmentUse::_useListHook
        >
    > _useList;
};

template<FragmentKindType K>
struct IsFragmentKind {
    bool operator() (Fragment *p) {
        return p->kind == K;
    }
};

template<typename T, FragmentKindType K>
struct CastableIfFragmentKind : Castable<T, IsFragmentKind<K>> { };

struct PhdrsFragment : Fragment,
        CastableIfFragmentKind<PhdrsFragment, fragment_kinds::phdrsFragment> {
    PhdrsFragment()
    : Fragment{fragment_kinds::phdrsFragment} { }
};

struct ShdrsFragment : Fragment,
        CastableIfFragmentKind<ShdrsFragment, fragment_kinds::shdrsFragment> {
    ShdrsFragment()
    : Fragment{fragment_kinds::shdrsFragment} { }
};

// Fragment that stores actual contents (i.e. some binary buffer).
struct ByteSection : Fragment,
        CastableIfFragmentKind<ByteSection, fragment_kinds::byteSection> {
    ByteSection()
    : Fragment{fragment_kinds::byteSection} { }

    std::vector<uint8_t> buffer;
};

struct DynamicSection : Fragment,
        CastableIfFragmentKind<DynamicSection, fragment_kinds::dynamicSection> {
    DynamicSection()
    : Fragment{fragment_kinds::dynamicSection} { }
};

struct StringTableSection : Fragment,
        CastableIfFragmentKind<StringTableSection, fragment_kinds::stringTableSection> {
    StringTableSection()
    : Fragment{fragment_kinds::stringTableSection} { }
};

struct String {
    String(std::string buffer_)
    : buffer{std::move(buffer_)} { }

    const std::string buffer;
    std::optional<size_t> designatedOffset;
};

struct SymbolTableSection : Fragment,
        CastableIfFragmentKind<SymbolTableSection, fragment_kinds::symbolTableSection> {
    SymbolTableSection()
    : Fragment{fragment_kinds::symbolTableSection} { }
};

struct Symbol {
    String *name = nullptr;
    FragmentUse section;
    size_t value = 0;

    std::optional<size_t> designatedIndex;
};

struct RelocationSection : Fragment,
        CastableIfFragmentKind<RelocationSection, fragment_kinds::relocationSection> {
    RelocationSection()
    : Fragment{fragment_kinds::relocationSection} { }
};

struct Relocation {
    FragmentUse section;
    ptrdiff_t offset = -1;
    Symbol *symbol = nullptr;
    std::optional<ptrdiff_t> addend;

    std::optional<size_t> designatedIndex;
};

struct HashSection : Fragment,
        CastableIfFragmentKind<HashSection, fragment_kinds::hashSection> {
    HashSection()
    : Fragment{fragment_kinds::hashSection} { }

    std::vector<Symbol *> buckets;
    std::vector<Symbol *> chains;
};

struct Object {
    // -------------------------------------------------------------------------------------
    // Fragment management.
    // -------------------------------------------------------------------------------------

    struct FragmentIterator {
    private:
        using Base = std::vector<std::unique_ptr<Fragment>>::iterator;

    public:
        FragmentIterator(Base b)
        : _b{b} { }

        bool operator!= (const FragmentIterator &other) const {
            return _b != other._b;
        }

        Fragment *operator* () const {
            return _b->get();
        }

        void operator++ () {
            ++_b;
        }

    private:
        Base _b;
    };

    struct FragmentRange {
        FragmentRange(Object *elf)
        : _elf{elf} { }

        FragmentIterator begin() {
            return FragmentIterator{_elf->_fragments.begin()};
        }
        FragmentIterator end() {
            return FragmentIterator{_elf->_fragments.end()};
        }

    private:
        Object *_elf;
    };

    void doInsertFragment(std::unique_ptr<Fragment> fragment);

    template<typename F>
    F *insertFragment(std::unique_ptr<F> fragment) {
        auto ptr = fragment.get();
        doInsertFragment(std::move(fragment));
        return ptr;
    }

    void replaceFragment(Fragment *from, std::unique_ptr<Fragment> to);

    FragmentRange fragments() {
        return FragmentRange{this};
    }

    size_t numberOfFragments() {
        return _fragments.size();
    }

    size_t numberOfSections() {
        return _numSections;
    }

    FragmentUse phdrsFragment;
    FragmentUse shdrsFragment;
    FragmentUse dynamicFragment;
    FragmentUse stringTableFragment;
    FragmentUse symbolTableFragment;
    FragmentUse pltRelocationFragment;
    FragmentUse hashFragment;

    // -------------------------------------------------------------------------------------
    // String management.
    // -------------------------------------------------------------------------------------

    struct StringIterator {
    private:
        using Base = std::vector<std::unique_ptr<String>>::iterator;

    public:
        StringIterator(Base b)
        : _b{b} { }

        bool operator!= (const StringIterator &other) const {
            return _b != other._b;
        }

        String *operator* () const {
            return _b->get();
        }

        void operator++ () {
            ++_b;
        }

    private:
        Base _b;
    };

    struct StringRange {
        StringRange(Object *elf)
        : _elf{elf} { }

        StringIterator begin() {
            return StringIterator{_elf->_strings.begin()};
        }
        StringIterator end() {
            return StringIterator{_elf->_strings.end()};
        }

    private:
        Object *_elf;
    };

    void doAddString(std::unique_ptr<String> string);

    String *addString(std::unique_ptr<String> string) {
        auto ptr = string.get();
        doAddString(std::move(string));
        return ptr;
    }

    StringRange strings() {
        return StringRange{this};
    }

    // -------------------------------------------------------------------------------------
    // Symbol management.
    // -------------------------------------------------------------------------------------

    struct SymbolIterator {
    private:
        using Base = std::vector<std::unique_ptr<Symbol>>::iterator;

    public:
        SymbolIterator(Base b)
        : _b{b} { }

        bool operator!= (const SymbolIterator &other) const {
            return _b != other._b;
        }

        Symbol *operator* () const {
            return _b->get();
        }

        void operator++ () {
            ++_b;
        }

    private:
        Base _b;
    };

    struct SymbolRange {
        SymbolRange(Object *elf)
        : _elf{elf} { }

        SymbolIterator begin() {
            return SymbolIterator{_elf->_symbols.begin()};
        }
        SymbolIterator end() {
            return SymbolIterator{_elf->_symbols.end()};
        }

        size_t size() {
            return _elf->_symbols.size();
        }

    private:
        Object *_elf;
    };

    void doAddSymbol(std::unique_ptr<Symbol> symbol);

    Symbol *addSymbol(std::unique_ptr<Symbol> symbol) {
        auto ptr = symbol.get();
        doAddSymbol(std::move(symbol));
        return ptr;
    }

    SymbolRange symbols() {
        return SymbolRange{this};
    }

    // -------------------------------------------------------------------------------------
    // Relocation management.
    // -------------------------------------------------------------------------------------

    struct RelocationIterator {
    private:
        using Base = std::vector<std::unique_ptr<Relocation>>::iterator;

    public:
        RelocationIterator(Base b)
        : _b{b} { }

        bool operator!= (const RelocationIterator &other) const {
            return _b != other._b;
        }

        Relocation *operator* () const {
            return _b->get();
        }

        void operator++ () {
            ++_b;
        }

    private:
        Base _b;
    };

    struct RelocationRange {
        RelocationRange(Object *elf)
        : _elf{elf} { }

        RelocationIterator begin() {
            return RelocationIterator{_elf->_relocations.begin()};
        }
        RelocationIterator end() {
            return RelocationIterator{_elf->_relocations.end()};
        }

    private:
        Object *_elf;
    };

    void doAddRelocation(std::unique_ptr<Relocation> relocation);

    Relocation *addRelocation(std::unique_ptr<Relocation> relocation) {
        auto ptr = relocation.get();
        doAddRelocation(std::move(relocation));
        return ptr;
    }

    RelocationRange relocations() {
        return RelocationRange{this};
    }

    struct InternalRelocationRange {
        InternalRelocationRange(Object *elf)
        : _elf{elf} { }

        RelocationIterator begin() {
            return RelocationIterator{_elf->_internalRelocations.begin()};
        }
        RelocationIterator end() {
            return RelocationIterator{_elf->_internalRelocations.end()};
        }

    private:
        Object *_elf;
    };

    void doAddInternalRelocation(std::unique_ptr<Relocation> relocation);

    Relocation *addInternalRelocation(std::unique_ptr<Relocation> relocation) {
        auto ptr = relocation.get();
        doAddInternalRelocation(std::move(relocation));
        return ptr;
    }

    InternalRelocationRange internalRelocations() {
        return InternalRelocationRange{this};
    }

private:
    std::vector<std::unique_ptr<Fragment>> _fragments;
    std::vector<std::unique_ptr<String>> _strings;
    std::vector<std::unique_ptr<Symbol>> _symbols;
    std::vector<std::unique_ptr<Relocation>> _relocations;
    std::vector<std::unique_ptr<Relocation>> _internalRelocations;
    size_t _numSections = 0;
};

} // namespace lewis::elf
