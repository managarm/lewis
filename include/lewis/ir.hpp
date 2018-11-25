
#pragma once

#include <memory>
#include <vector>
#include <lewis/hierarchy.hpp>

namespace lewis {

using InstructionKindType = uint32_t;

namespace instruction_kinds {
    enum {
        null,
        loadConstCode
    };
}

struct Instruction {
    Instruction(InstructionKindType kind_)
    : kind{kind_} { }

    const InstructionKindType kind;
};

template<InstructionKindType K>
struct IsInstructionKind {
    bool operator() (Instruction *p) {
        return p->kind == K;
    }
};

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

    InstructionRange instructions() {
        return InstructionRange{this};
    }

private:
    std::vector<std::unique_ptr<Instruction>> _insts;
};

} // namespace lewis

