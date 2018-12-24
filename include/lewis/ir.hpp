
#pragma once

#include <memory>
#include <vector>
#include <frg/list.hpp>
#include <lewis/hierarchy.hpp>

namespace lewis {

//---------------------------------------------------------------------------------------
// ValueUse class to represent "uses" of a Value.
//---------------------------------------------------------------------------------------

struct Value;

// Represents a single "use" of a Value. This is necessary to support Value replacement.
// For convenience, this class also has a Value pointer-like interface.
struct ValueUse {
    friend struct Value;

    ValueUse()
    : _ref{nullptr} { }

    ValueUse(Value *v)
    : _ref{nullptr} {
        assign(v);
    }

    ValueUse(const ValueUse &) = delete;

    ValueUse &operator= (const ValueUse &) = delete;

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

    Value(ValueKindType kind_)
    : kind{kind_} { }

    void replaceAllUses(Value *other);

    const ValueKindType kind;

private:
    // Linked list of all uses of this Value.
    frg::intrusive_list<
        ValueUse,
        frg::locate_member<
            ValueUse,
            frg::default_list_hook<ValueUse>,
            &ValueUse::_useListHook
        >
    > _useList;
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
    Instruction(InstructionKindType kind_)
    : kind{kind_} { }

    const InstructionKindType kind;
};

// Template magic to enable hierarchy_cast<>.
template<InstructionKindType K>
struct IsInstructionKind {
    bool operator() (Instruction *p) {
        return p->kind == K;
    }
};

// Template magic to enable hierarchy_cast<>.
template<typename T, InstructionKindType K>
struct CastableIfInstructionKind : Castable<T, IsInstructionKind<K>> { };

struct BasicBlock {
    struct InstructionIterator {
    private:
        using Base = std::vector<std::unique_ptr<Instruction>>::iterator;

    public:
        InstructionIterator(Base b)
        : _b{b} { }

        bool operator!= (const InstructionIterator &other) const {
            return _b != other._b;
        }

        Instruction *operator* () const {
            return _b->get();
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

        size_t size() {
            return _bb->_insts.size();
        }

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
        _insts.push_back(std::move(inst));
    }

    template<typename I>
    I *insertInstruction(std::unique_ptr<I> inst) {
        auto ptr = inst.get();
        doInsertInstruction(std::move(inst));
        return ptr;
    }

    void replaceInstruction(Instruction *from, std::unique_ptr<Instruction> to) {
        // TODO: Handle errors.
        // TODO: Make this more efficient.
        for(auto &inst : _insts)
            if(inst.get() == from)
                inst = std::move(to);
    }

    InstructionRange instructions() {
        return InstructionRange{this};
    }

private:
    std::vector<std::unique_ptr<Instruction>> _insts;
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
    : Instruction{instruction_kinds::unaryMath}, opcode{opcode_}, operand{operand_} { }

    UnaryMathOpcode opcode;
    ValueUse operand;
};

} // namespace lewis

