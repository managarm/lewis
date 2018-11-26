
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
    if(_ref) {
        auto it = _ref->_useList.iterator_to(this);
        _ref->_useList.erase(it);
    }

    if(f)
        f->_useList.push_back(this);
    _ref = f;
}

// --------------------------------------------------------------------------------------
// Fragment class
// --------------------------------------------------------------------------------------

void Fragment::replaceAllUses(Fragment *other) {
    auto it = _useList.begin();
    while(it != _useList.end()) {
        FragmentUse *use = *it;
        ++it;
        use->assign(other);
    }
}

// --------------------------------------------------------------------------------------
// Object class
// --------------------------------------------------------------------------------------

void Object::insertFragment(std::unique_ptr<Fragment> fragment) {
    _fragments.push_back(std::move(fragment));
}

void Object::addString(std::unique_ptr<String> string) {
    _strings.push_back(std::move(string));
}

void Object::replaceFragment(Fragment *from, std::unique_ptr<Fragment> to) {
    from->replaceAllUses(to.get());

    for(auto &slot : _fragments) {
        if(slot.get() != from)
            continue;
        slot = std::move(to);
        return;
    }

    assert(!"replaceFragment(): Fragment does not exist");
}

} // namespace lewis::elf

