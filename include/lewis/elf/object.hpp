
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
        stringTableSection
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
    std::optional<uintptr_t> virtualAdress;
    std::optional<uintptr_t> computedSize;

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

    void insertFragment(std::unique_ptr<Fragment> fragment);

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
    FragmentUse stringTableFragment;

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

    void addString(std::unique_ptr<String> string);

    StringRange strings() {
        return StringRange{this};
    }

private:
    std::vector<std::unique_ptr<Fragment>> _fragments;
    std::vector<std::unique_ptr<String>> _strings;
    size_t _numSections = 0;
};

} // namespace lewis::elf

