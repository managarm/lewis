// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <algorithm>
#include <memory>
#include <vector>
#include <frg/list.hpp>
#include <lewis/hierarchy.hpp>

namespace lewis {

//---------------------------------------------------------------------------------------
// ValueUse class to represent "uses" of a Value.
//---------------------------------------------------------------------------------------

struct Value;
struct Instruction;

// Represents a single "use" of a Value. This is necessary to support Value replacement.
// For convenience, this class also has a Value pointer-like interface.
struct ValueUse {
    friend struct Value;

    ValueUse(Instruction *inst)
    : _inst{inst}, _ref{nullptr} { }

    ValueUse(Instruction *inst, Value *v)
    : _inst{inst}, _ref{nullptr} {
        assign(v);
    }

    ValueUse(const ValueUse &) = delete;

    ValueUse &operator= (const ValueUse &) = delete;

    Instruction *instruction() {
        return _inst;
    }

    void assign(Value *v);

    Value *get() {
        return _ref;
    }

    // The following operators define the pointer-like interface.

    ValueUse &operator= (Value *v) {
        assign(v);
        return *this;
    }

    bool operator== (Value *v) { return _ref == v; }
    bool operator!= (Value *v) { return _ref != v; }

    explicit operator bool () { return _ref; }

    Value &operator* () { return *_ref; }
    Value *operator-> () { return _ref; }

private:
    Instruction *_inst;
    Value *_ref;
    frg::default_list_hook<ValueUse> _useListHook;
};

//---------------------------------------------------------------------------------------
// Value base class.
//---------------------------------------------------------------------------------------

// Defines an ID for each subclass of Value.
using ValueKindType = uint32_t;

namespace value_kinds {
    enum : ValueKindType {
        null,
        genericResult,

        // Give each architecture 16k values; that should be enough.
        kindsForX86 = 1 << 14
    };
}

struct Value {
    friend struct ValueUse;

    using UseList = frg::intrusive_list<
        ValueUse,
        frg::locate_member<
            ValueUse,
            frg::default_list_hook<ValueUse>,
            &ValueUse::_useListHook
        >
    >;

    struct UseRange {
        UseRange(Value *v)
        : _v{v} { }

        auto begin() { return _v->_useList.begin(); }
        auto end() { return _v->_useList.begin(); }

    private:
        Value *_v;
    };

    using UseIterator = UseList::iterator;

    Value(ValueKindType kind_)
    : kind{kind_} { }

    UseRange uses() {
        return UseRange{this};
    }

    void replaceAllUses(Value *other);

    const ValueKindType kind;

private:
    // Linked list of all uses of this Value.
    UseList _useList;
};

// Template magic to enable hierarchy_cast<>.
template<ValueKindType K>
struct IsValueKind {
    bool operator() (Value *p) {
        return p->kind == K;
    }
};

// Template magic to enable hierarchy_cast<>.
template<typename T, ValueKindType K>
struct CastableIfValueKind : Castable<T, IsValueKind<K>> { };

//---------------------------------------------------------------------------------------
// Instruction base class.
//---------------------------------------------------------------------------------------

// Defines an ID for each subclass of Instruction.
using InstructionKindType = uint32_t;

namespace instruction_kinds {
    enum : InstructionKindType {
        null,
        loadConst,
        unaryMath,

        // Give each architecture 16k instructions; that should be enough.
        kindsForX86 = 1 << 14
    };
}

struct Instruction {
    friend struct BasicBlock;

    Instruction(InstructionKindType kind_)
    : kind{kind_} { }

    const InstructionKindType kind;

private:
    frg::default_list_hook<Instruction> _listHook;
};

// Template magic to enable hierarchy_cast<>.
template<InstructionKindType... S>
struct IsInstructionKind {
    bool operator() (Instruction *p) {
        std::array<InstructionKindType, sizeof...(S)> kinds{S...};
        return std::find(kinds.begin(), kinds.end(), p->kind) != kinds.end();
    }
};

// Specialization for the simple case.
template<InstructionKindType K>
struct IsInstructionKind<K> {
    bool operator() (Instruction *p) {
        return p->kind == K;
    }
};

// Template magic to enable hierarchy_cast<>.
template<typename T, InstructionKindType... S>
struct CastableIfInstructionKind : Castable<T, IsInstructionKind<S...>> { };

struct BasicBlock {
    using InstructionList = frg::intrusive_list<
        Instruction,
        frg::locate_member<
            Instruction,
            frg::default_list_hook<Instruction>,
            &Instruction::_listHook
        >
    >;

    struct InstructionIterator {
        friend struct BasicBlock;
    private:
        using Base = InstructionList::iterator;

    public:
        InstructionIterator(Base b)
        : _b{b} { }

        bool operator!= (const InstructionIterator &other) const {
            return _b != other._b;
        }

        Instruction *operator* () const {
            return *_b;
        }

        void operator++ () {
            ++_b;
        }

    private:
        Base _b;
    };

    struct InstructionRange {
        InstructionRange(BasicBlock *bb)
        : _bb{bb} { }

        InstructionIterator begin() {
            return InstructionIterator{_bb->_insts.begin()};
        }
        InstructionIterator end() {
            return InstructionIterator{_bb->_insts.end()};
        }

    private:
        BasicBlock *_bb;
    };

    void doInsertInstruction(std::unique_ptr<Instruction> inst) {
        _insts.push_back(inst.release());
    }

    void doInsertInstruction(InstructionIterator before, std::unique_ptr<Instruction> inst) {
        _insts.insert(before._b, inst.release());
    }

    template<typename I>
    I *insertInstruction(std::unique_ptr<I> inst) {
        auto ptr = inst.get();
        doInsertInstruction(std::move(inst));
        return ptr;
    }

    template<typename I>
    I *insertInstruction(InstructionIterator it, std::unique_ptr<I> inst) {
        auto ptr = inst.get();
        doInsertInstruction(it, std::move(inst));
        return ptr;
    }

    InstructionIterator replaceInstruction(InstructionIterator from, std::unique_ptr<Instruction> to) {
        auto it = from._b;
        auto nit = _insts.insert(it, to.release());
        _insts.erase(it);
        return nit;
    }

    InstructionRange instructions() {
        return InstructionRange{this};
    }

private:
    frg::intrusive_list<
        Instruction,
        frg::locate_member<
            Instruction,
            frg::default_list_hook<Instruction>,
            &Instruction::_listHook
        >
    > _insts;
};

//---------------------------------------------------------------------------------------
// Helper class to define instructions with a single result.
//---------------------------------------------------------------------------------------

// Value subclass for results of instructions without special properties.
struct GenericResult
: Value,
        CastableIfValueKind<GenericResult, value_kinds::genericResult> {
    GenericResult()
    : Value{value_kinds::genericResult} { }
};

struct WithGenericResult {
    GenericResult *result() {
        return &_result;
    }

private:
    GenericResult _result;
};

//---------------------------------------------------------------------------------------
// Next, the actual instruction classes are defined.
//---------------------------------------------------------------------------------------

struct LoadConstInstruction
: Instruction, WithGenericResult,
        CastableIfInstructionKind<LoadConstInstruction, instruction_kinds::loadConst> {
    LoadConstInstruction(uint64_t value_ = 0)
    : Instruction{instruction_kinds::loadConst}, value{value_} { }

    // TODO: This value should probably be more generic. For now, uint64_t is sufficient though.
    uint64_t value;
};

enum class UnaryMathOpcode {
    null,
    negate
};

struct UnaryMathInstruction
: Instruction, WithGenericResult,
        CastableIfInstructionKind<UnaryMathInstruction, instruction_kinds::unaryMath> {
    UnaryMathInstruction(UnaryMathOpcode opcode_ = UnaryMathOpcode::null,
            Value *operand_ = nullptr)
    : Instruction{instruction_kinds::unaryMath}, opcode{opcode_}, operand{this, operand_} { }

    UnaryMathOpcode opcode;
    ValueUse operand;
};

} // namespace lewis
