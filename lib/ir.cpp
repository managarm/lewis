// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#include <cassert>
#include <lewis/ir.hpp>

namespace lewis {

Type *globalPointerType() {
    static Type singleton{type_kinds::pointer};
    return &singleton;
}

Type *globalInt32Type() {
    static Type singleton{type_kinds::int32};
    return &singleton;
}

Type *globalInt64Type() {
    static Type singleton{type_kinds::int64};
    return &singleton;
}

void ValueOrigin::doSet(std::unique_ptr<Value> v) {
    assert(!v->_origin);
    v->_origin = this;
    _value = v.get();
    v.release();
}

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
    assert(other != this);
    auto it = _useList.begin();
    while (it != _useList.end()) {
        ValueUse *use = *it;
        ++it;
        use->assign(other);
    }
}

void DataFlowEdge::doAttach(std::unique_ptr<DataFlowEdge> edge,
        DataFlowSource &source, DataFlowSink &sink) {
    assert(!edge->_source && !edge->_sink);
    edge->_source = &source;
    edge->_sink = &sink;
    source._edges.push_back(edge.get());
    sink._edges.push_back(edge.get());
    edge.release();
}

} // namespace lewis
