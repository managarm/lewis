// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <lewis/ir.hpp>

namespace lewis {

void ValueUse::assign(Value *v) {
    if (_ref) {
        auto it = _ref->_useList.iterator_to(this);
        _ref->_useList.erase(it);
    }

    if (v)
        v->_useList.push_back(this);
    _ref = v;
}

void Value::replaceAllUses(Value *other) {
    auto it = _useList.begin();
    while (it != _useList.end()) {
        ValueUse *use = *it;
        ++it;
        use->assign(other);
    }
}

} // namespace lewis