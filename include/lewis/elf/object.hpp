
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
        section,
        phdrsReservation,
        shdrsReservation
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

// Represents a fragment of an ELF file.
// Before ELF emission, all Fragments need to be converted to Sections.
struct Fragment {
    friend struct FragmentUse;

    Fragment(FragmentKindType kind_)
    : kind{kind_} { }

    Fragment(const Fragment &) = delete;

    virtual ~Fragment() = default;

    Fragment &operator= (const Fragment &) = delete;

    void replaceAllUses(Fragment *other);

    const FragmentKindType kind;

public:
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

// Fragment that stores actual contents (i.e. some binary buffer).
struct Section : Fragment,
        CastableIfFragmentKind<Section, fragment_kinds::section> {
    Section()
    : Fragment{fragment_kinds::section} { }

    std::vector<uint8_t> buffer;
};

// Fragment that reserves space in an ELF object but does not store contents.
struct Reservation : Fragment {
    Reservation(FragmentKindType kind_)
    : Fragment{kind_} { }
};

struct PhdrsReservation : Reservation,
        CastableIfFragmentKind<PhdrsReservation, fragment_kinds::phdrsReservation> {
    PhdrsReservation()
    : Reservation{fragment_kinds::phdrsReservation} { }
};

struct ShdrsReservation : Reservation,
        CastableIfFragmentKind<ShdrsReservation, fragment_kinds::shdrsReservation> {
    ShdrsReservation()
    : Reservation{fragment_kinds::shdrsReservation} { }
};

struct Object {
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

        size_t size() {
            return _elf->_fragments.size();
        }

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

    void emitTo(FILE *stream);

    FragmentUse phdrsFragment;
    FragmentUse shdrsFragment;

private:
    std::vector<std::unique_ptr<Fragment>> _fragments;
};

} // namespace lewis::elf

