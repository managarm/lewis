// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

namespace lewis {

// Helper to encode types in values.
template<typename D>
struct HierarchyTag { };

// CRTP that enables hierarchy_cast<> for the class D.
// Parameter P is a predicate that determines if the cast is possible.
template<typename D, typename P>
struct Castable {
    template<typename B>
    friend D *castTaggedPointer(HierarchyTag<D>, B *p) {
        if (!p)
            return nullptr;
        P pred;
        if (!pred(p))
            return nullptr;
        return static_cast<D *>(p);
    }
};

// This struct is used to match From and To types and makes sure only
// <From *> to <To *> casts are allowed.
template<typename To, typename From>
struct CastHelper;

template<typename D, typename B>
struct CastHelper<D *, B *> {
    static D *doCast(B *p) {
        return castTaggedPointer(HierarchyTag<D>{}, p);
    }
};

template<typename To, typename From>
To hierarchy_cast(From x) {
    return CastHelper<To, From>::doCast(x);
}

} // namespace lewis
