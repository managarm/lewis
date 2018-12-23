
#pragma once

#include <memory>
#include <vector>
#include <lewis/hierarchy.hpp>

namespace lewis {

//---------------------------------------------------------------------------------------
// Instruction base class.
//---------------------------------------------------------------------------------------

// Defines an ID for each subclass of Instruction.
using InstructionKindType = uint32_t;

namespace instruction_kinds {
    enum : InstructionKindType {
        null,
        loadConst,

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

    void insertInstruction(std::unique_ptr<Instruction> inst) {
        _insts.push_back(std::move(inst));
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
// Next, the actual instruction classes are defined.
//---------------------------------------------------------------------------------------

struct LoadConstInstruction
: Instruction,
        CastableIfInstructionKind<LoadConstInstruction, instruction_kinds::loadConst> {
    LoadConstInstruction(uint64_t value_ = 0)
    : Instruction{instruction_kinds::loadConst}, value{value_} { }

    // TODO: This value should probably be more generic. For now, uint64_t is sufficient though.
    uint64_t value;
};

} // namespace lewis

