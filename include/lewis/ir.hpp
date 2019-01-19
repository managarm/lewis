// Copyright the lewis authors (AUTHORS.md) 2018
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <frg/list.hpp>
#include <frg/rbtree.hpp>
#include <lewis/hierarchy.hpp>

namespace lewis {

struct Value;
struct Instruction;
struct DataFlowSource;
struct DataFlowSink;
struct BasicBlock;

//---------------------------------------------------------------------------------------
// ValueUse class to represent "uses" of a Value.
//---------------------------------------------------------------------------------------

struct ValueOrigin {
    friend struct Value;

    ValueOrigin(Instruction *inst)
    : _inst{inst}, _value{nullptr} { }

    Instruction *instruction() {
        return _inst;
    }

    Value *get() {
        return _value;
    }

    void doSet(std::unique_ptr<Value> value);

    template<typename T>
    T *set(std::unique_ptr<T> value) {
        auto ptr = value.get();
        doSet(std::move(value));
        return ptr;
    }

    template<typename T, typename... Args>
    T *setNew(Args &&... args) {
        return set(std::make_unique<T>(std::forward<Args>(args)...));
    }

private:
    Instruction *_inst;
    Value *_value;
};

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
        local,

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
        auto end() { return _v->_useList.end(); }

    private:
        Value *_v;
    };

    using UseIterator = UseList::iterator;

    Value(ValueKindType valueKind_)
    : valueKind{valueKind_} { }

    UseRange uses() {
        return UseRange{this};
    }

    void replaceAllUses(Value *other);

    const ValueKindType valueKind;

private:
    // Linked list of all uses of this Value.
    UseList _useList;
};

// Template magic to enable hierarchy_cast<>.
template<ValueKindType K>
struct IsValueKind {
    bool operator() (Value *p) {
        return p->valueKind == K;
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
        loadOffset,
        unaryMath,
        binaryMath,
        invoke,

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
    BasicBlock *_bb = nullptr;
    frg::rbtree_hook _instTreeHook;
    size_t _numSubtreeInstr = 1;
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

//---------------------------------------------------------------------------------------
// Branch base class.
//---------------------------------------------------------------------------------------

// Defines an ID for each subclass of Branch.
using BranchKindType = uint32_t;

namespace branch_kinds {
    enum : BranchKindType {
        null,
        functionReturn,
        unconditional,

        // Give each architecture 16k branches; that should be enough.
        kindsForX86 = 1 << 14
    };
}

struct Branch {
    Branch(BranchKindType kind_)
    : kind{kind_} { }

    const BranchKindType kind;
};

template<BranchKindType K>
struct IsBranchKind {
    bool operator() (Branch *p) {
        return p->kind == K;
    }
};

// Template magic to enable hierarchy_cast<>.
template<typename T, BranchKindType... S>
struct CastableIfBranchKind : Castable<T, IsBranchKind<S...>> { };

struct FunctionReturnBranch
: Branch,
        CastableIfBranchKind<FunctionReturnBranch, branch_kinds::functionReturn> {
    FunctionReturnBranch()
    : Branch{branch_kinds::functionReturn} { }
};

struct UnconditionalBranch
: Branch,
        CastableIfBranchKind<UnconditionalBranch, branch_kinds::unconditional> {
    UnconditionalBranch(BasicBlock *target_ = nullptr)
    : Branch{branch_kinds::unconditional}, target{target_} { }

    // TODO: Use a BlockLink class similar to ValueUse.
    BasicBlock *target;
};

//---------------------------------------------------------------------------------------
// Data-flow related classes.
//---------------------------------------------------------------------------------------

struct DataFlowEdge {
    friend struct DataFlowSource;
    friend struct DataFlowSink;

    static void doAttach(std::unique_ptr<DataFlowEdge> edge,
            DataFlowSource &source, DataFlowSink &sink);

    static DataFlowEdge *attach(std::unique_ptr<DataFlowEdge> edge,
            DataFlowSource &source, DataFlowSink &sink) {
        auto ptr = edge.get();
        doAttach(std::move(edge), source, sink);
        return ptr;
    }

    // TODO: Do not pass nullptr as an Instruction to the ValueUse.
    DataFlowEdge()
    : alias{nullptr}, _source{nullptr}, _sink{nullptr} { }

    DataFlowSource *source() const { return _source; }
    DataFlowSink *sink() const { return _sink; }

    ValueUse alias;

private:
    DataFlowSource *_source;
    DataFlowSink *_sink;
    frg::default_list_hook<DataFlowEdge> _sourceListHook;
    frg::default_list_hook<DataFlowEdge> _sinkListHook;
};

struct DataFlowSource {
    friend struct DataFlowEdge;

    using EdgeList = frg::intrusive_list<
        DataFlowEdge,
        frg::locate_member<
            DataFlowEdge,
            frg::default_list_hook<DataFlowEdge>,
            &DataFlowEdge::_sourceListHook
        >
    >;

    using EdgeIterator = EdgeList::iterator;

    struct EdgeRange {
        EdgeRange(DataFlowSource *source)
        : _source{source} { }

        EdgeIterator begin() {
            return _source->_edges.begin();
        }
        EdgeIterator end() {
            return _source->_edges.end();
        }

    private:
        DataFlowSource *_source;
    };

    DataFlowSource(BasicBlock *bb)
    : _bb{bb} { }

    BasicBlock *block() {
        return _bb;
    }

    EdgeRange edges() {
        return EdgeRange{this};
    }

private:
    BasicBlock *_bb;
    EdgeList _edges;
};

struct DataFlowSink {
    friend struct DataFlowEdge;

    using EdgeList = frg::intrusive_list<
        DataFlowEdge,
        frg::locate_member<
            DataFlowEdge,
            frg::default_list_hook<DataFlowEdge>,
            &DataFlowEdge::_sinkListHook
        >
    >;

    using EdgeIterator = EdgeList::iterator;

    struct EdgeRange {
        EdgeRange(DataFlowSink *sink)
        : _sink{sink} { }

        EdgeIterator begin() {
            return _sink->_edges.begin();
        }
        EdgeIterator end() {
            return _sink->_edges.end();
        }

    private:
        DataFlowSink *_sink;
    };

    EdgeRange edges() {
        return EdgeRange{this};
    }

private:
    EdgeList _edges;
};

//---------------------------------------------------------------------------------------
// BasicBlock class and Phi classes.
//---------------------------------------------------------------------------------------

// Defines an ID for each subclass of PhiNode.
using PhiKindType = uint32_t;

namespace phi_kinds {
    enum : PhiKindType {
        null,
        argument,
        dataFlow
    };
}

struct PhiNode {
    friend struct BasicBlock;

    PhiNode(PhiKindType phiKind_)
    : phiKind{phiKind_}, value{nullptr} { }

    const PhiKindType phiKind;
    ValueOrigin value;

private:
    frg::default_list_hook<PhiNode> _phiListHook;
};

// Template magic to enable hierarchy_cast<>.
template<PhiKindType K>
struct IsPhiKind {
    bool operator() (PhiNode *p) {
        return p->phiKind == K;
    }
};

// Template magic to enable hierarchy_cast<>.
template<typename T, PhiKindType... S>
struct CastableIfPhiKind : Castable<T, IsPhiKind<S...>> { };

struct ArgumentPhi : PhiNode, CastableIfPhiKind<ArgumentPhi, phi_kinds::argument> {
    ArgumentPhi()
    : PhiNode{phi_kinds::argument} { }
};

struct DataFlowPhi : PhiNode, CastableIfPhiKind<DataFlowPhi, phi_kinds::dataFlow> {
    DataFlowPhi()
    : PhiNode{phi_kinds::dataFlow} { }

    DataFlowSink sink;
};

struct BasicBlock {
    friend struct Function;

    using PhiList = frg::intrusive_list<
        PhiNode,
        frg::locate_member<
            PhiNode,
            frg::default_list_hook<PhiNode>,
            &PhiNode::_phiListHook
        >
    >;

    using PhiIterator = PhiList::iterator;

    struct PhiRange {
        PhiRange(BasicBlock *bb)
        : _bb{bb} { }

        PhiIterator begin() {
            return _bb->_phis.begin();
        }
        PhiIterator end() {
            return _bb->_phis.end();
        }

    private:
        BasicBlock *_bb;
    };

    struct InstructionAggregator;

    using InstructionTree = frg::rbtree_order<
        Instruction,
        &Instruction::_instTreeHook,
        InstructionAggregator
    >;

    // Helper class for frg::rbtree_order. Allows us to calculate instruction indices quickly.
    struct InstructionAggregator {
        static bool aggregate(Instruction *inst) {
            size_t newSubtreeInstr = 1;
            if(InstructionTree::get_left(inst))
                newSubtreeInstr += InstructionTree::get_left(inst)->_numSubtreeInstr;
            if(InstructionTree::get_right(inst))
                newSubtreeInstr += InstructionTree::get_right(inst)->_numSubtreeInstr;

            if(newSubtreeInstr == inst->_numSubtreeInstr)
                return false;
            inst->_numSubtreeInstr = newSubtreeInstr;
            return true;
        }

        static bool check_invariant(InstructionTree &, Instruction *) {
            return true;
        }
    };

    struct InstructionIterator {
        friend struct BasicBlock;
    public:
        InstructionIterator(Instruction *inst)
        : _inst{inst} { }

        bool operator!= (const InstructionIterator &other) const {
            return _inst != other._inst;
        }

        Instruction *operator* () const {
            assert(_inst);
            return _inst;
        }

        void operator++ () {
            assert(_inst);
            _inst = InstructionTree::successor(_inst);
        }

    private:
        Instruction *_inst;
    };

    struct InstructionRange {
        InstructionRange(BasicBlock *bb)
        : _bb{bb} { }

        InstructionIterator begin() {
            return InstructionIterator{_bb->_insts.first()};
        }
        InstructionIterator end() {
            return InstructionIterator{nullptr};
        }

    private:
        BasicBlock *_bb;
    };

    BasicBlock()
    : source{this} { }

    PhiRange phis() {
        return PhiRange{this};
    }

    template<typename T>
    T *attachPhi(std::unique_ptr<T> phi) {
        auto ptr = phi.get();
        _phis.push_back(phi.release());
        return ptr;
    }

    PhiIterator replacePhi(PhiIterator from, std::unique_ptr<PhiNode> to) {
        auto it = from;
        auto nit = _phis.insert(it, to.release());
        _phis.erase(it);
        return nit;
    }

    InstructionIterator iteratorTo(Instruction *inst) {
        return InstructionIterator{inst};
    }

    InstructionRange instructions() {
        return InstructionRange{this};
    }

    // Computes the index of an instruction. Can be used to compare the position of instructions.
    // Note that the index changes if instructions are inserted before the given one.
    size_t indexOfInstruction(Instruction *inst) {
        if (!inst) {
            // Return the size of the tree.
            auto root = _insts.get_root();
            if (root) {
                return root->_numSubtreeInstr;
            } else {
                return 0;
            }
        }

        // Find the position of the instruction inside the rbtree.
        size_t index = 0;
        if (InstructionTree::get_left(inst))
            index += InstructionTree::get_left(inst)->_numSubtreeInstr;

        Instruction *current = inst;
        while (true) {
            auto parent = InstructionTree::get_parent(current);
            if (!parent)
                return index;

            if (InstructionTree::get_right(parent) == current) {
                if (InstructionTree::get_left(parent))
                    index += InstructionTree::get_left(parent)->_numSubtreeInstr;
                index += 1;
            }
            current = parent;
        }

        return index;
    }

    void doInsertInstruction(std::unique_ptr<Instruction> inst) {
        assert(!inst->_bb);
        inst->_bb = this;
        _insts.insert(nullptr, inst.release());
    }

    void doInsertInstruction(InstructionIterator before, std::unique_ptr<Instruction> inst) {
        assert(!inst->_bb);
        inst->_bb = this;
        _insts.insert(before._inst, inst.release());
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

    void eraseInstruction(InstructionIterator it) {
        _insts.remove(it._inst);
    }

    InstructionIterator replaceInstruction(InstructionIterator from,
            std::unique_ptr<Instruction> to) {
        assert(from._inst);
        assert(from._inst->_bb == this);
        assert(!to->_bb);
        auto ptr = to.get();
        to->_bb = this;
        _insts.insert(from._inst, to.release());
        from._inst->_bb = nullptr;
        _insts.remove(from._inst);
        return InstructionIterator{ptr};
    }

    void setBranch(std::unique_ptr<Branch> branch) {
        _branch = std::move(branch);
    }

    Branch *branch() {
        return _branch.get();
    }

    DataFlowSource source;

private:
    frg::default_list_hook<BasicBlock> _blockListHook;

    PhiList _phis;
    InstructionTree _insts;
    std::unique_ptr<Branch> _branch;
};

//---------------------------------------------------------------------------------------
// Function class.
//---------------------------------------------------------------------------------------

struct Function {
    using BlockList = frg::intrusive_list<
        BasicBlock,
        frg::locate_member<
            BasicBlock,
            frg::default_list_hook<BasicBlock>,
            &BasicBlock::_blockListHook
        >
    >;

    using BlockIterator = BlockList::iterator;

    struct BlockRange {
        BlockRange(Function *fn)
        : _fn{fn} { }

        BlockIterator begin() {
            return _fn->_blocks.begin();
        }
        BlockIterator end() {
            return _fn->_blocks.end();
        }

    private:
        Function *_fn;
    };

    BlockRange blocks() {
        return BlockRange{this};
    }

    BasicBlock *addBlock(std::unique_ptr<BasicBlock> block) {
        auto ptr = block.get();
        _blocks.push_back(block.release());
        return ptr;
    }

private:
    BlockList _blocks;
};

//---------------------------------------------------------------------------------------
// Helper class to define instructions with a single result.
//---------------------------------------------------------------------------------------

// Value subclass for results of instructions without special properties.
struct LocalValue
: Value,
        CastableIfValueKind<LocalValue, value_kinds::local> {
    LocalValue()
    : Value{value_kinds::local} { }
};

//---------------------------------------------------------------------------------------
// Next, the actual instruction classes are defined.
//---------------------------------------------------------------------------------------

struct LoadConstInstruction
: Instruction,
        CastableIfInstructionKind<LoadConstInstruction, instruction_kinds::loadConst> {
    LoadConstInstruction(uint64_t value_ = 0)
    : Instruction{instruction_kinds::loadConst}, result{this},
            value{value_} { }

    ValueOrigin result;
    // TODO: This value should probably be more generic. For now, uint64_t is sufficient though.
    uint64_t value;
};

// TODO: In the long term, we probably want instructions to form pointers
// and instructions to dereference them. This is only a short-term solution.
struct LoadOffsetInstruction
: Instruction,
        CastableIfInstructionKind<LoadOffsetInstruction, instruction_kinds::loadOffset> {
    LoadOffsetInstruction(Value *operand_ = nullptr, int64_t offset_ = 0)
    : Instruction{instruction_kinds::loadOffset}, result{this},
            operand{this, operand_}, offset{offset_} { }

    ValueOrigin result;
    ValueUse operand;
    int64_t offset;
};

enum class UnaryMathOpcode {
    null,
    negate
};

struct UnaryMathInstruction
: Instruction,
        CastableIfInstructionKind<UnaryMathInstruction, instruction_kinds::unaryMath> {
    UnaryMathInstruction(UnaryMathOpcode opcode_ = UnaryMathOpcode::null,
            Value *operand_ = nullptr)
    : Instruction{instruction_kinds::unaryMath}, opcode{opcode_}, result{this},
            operand{this, operand_} { }

    UnaryMathOpcode opcode;
    ValueOrigin result;
    ValueUse operand;
};

enum class BinaryMathOpcode {
    null,
    add,
    bitwiseAnd
};

struct BinaryMathInstruction
: Instruction,
        CastableIfInstructionKind<BinaryMathInstruction, instruction_kinds::binaryMath> {
    BinaryMathInstruction(BinaryMathOpcode opcode_ = BinaryMathOpcode::null,
            Value *left_ = nullptr, Value *right_ = nullptr)
    : Instruction{instruction_kinds::binaryMath}, opcode{opcode_}, result{this},
            left{this, left_}, right{this, right_} { }

    BinaryMathOpcode opcode;
    ValueOrigin result;
    ValueUse left;
    ValueUse right;
};

struct InvokeInstruction
: Instruction,
        CastableIfInstructionKind<InvokeInstruction, instruction_kinds::invoke> {
    InvokeInstruction(std::string function, Value *operand_ = nullptr)
    : Instruction{instruction_kinds::invoke}, function{std::move(function)}, result{this},
            operand{this, operand_} { }

    std::string function;
    ValueOrigin result;
    ValueUse operand;
};

} // namespace lewis
