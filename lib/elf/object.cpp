// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstring>
#include <elf.h>
#include <lewis/elf/object.hpp>
#include <lewis/elf/utils.hpp>

namespace lewis::elf {

// --------------------------------------------------------------------------------------
// FragmentUse class
// --------------------------------------------------------------------------------------

void FragmentUse::assign(Fragment *f) {
    if (_ref) {
        auto it = _ref->_useList.iterator_to(this);
        _ref->_useList.erase(it);
    }

    if (f)
        f->_useList.push_back(this);
    _ref = f;
}

// --------------------------------------------------------------------------------------
// Fragment class
// --------------------------------------------------------------------------------------

void Fragment::replaceAllUses(Fragment *other) {
    auto it = _useList.begin();
    while (it != _useList.end()) {
        FragmentUse *use = *it;
        ++it;
        use->assign(other);
    }
}

// --------------------------------------------------------------------------------------
// Object class
// --------------------------------------------------------------------------------------

void Object::doInsertFragment(std::unique_ptr<Fragment> fragment) {
    if (fragment->isSection()) _numSections++;
    _fragments.push_back(std::move(fragment));
}

void Object::doAddString(std::unique_ptr<String> string) {
    _strings.push_back(std::move(string));
}

void Object::doAddSymbol(std::unique_ptr<Symbol> symbol) {
    _symbols.push_back(std::move(symbol));
}

void Object::doAddRelocation(std::unique_ptr<Relocation> relocation) {
    _relocations.push_back(std::move(relocation));
}

void Object::doAddInternalRelocation(std::unique_ptr<Relocation> relocation) {
    _internalRelocations.push_back(std::move(relocation));
}

void Object::replaceFragment(Fragment *from, std::unique_ptr<Fragment> to) {
    assert((from->isSection() && to->isSection())
            || (!from->isSection() && !to->isSection()));

    from->replaceAllUses(to.get());

    for (auto &slot : _fragments) {
        if (slot.get() != from) continue;
        slot = std::move(to);
        return;
    }

    assert(!"replaceFragment(): Fragment does not exist");
}

} // namespace lewis::elf
