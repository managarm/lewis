
#include <cassert>
#include <climits>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <frg/interval_tree.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>

namespace lewis::targets::x86_64 {

// We need to be able to distinguish the allocation situation before and after an instruction.
// The atInstruction value is not really used.
enum SubInstruction {
    beforeInstruction = -1,
    atInstruction = 0,
    afterInstruction = 1
};

// Represents a point in the program at which we perform register allocation.
struct ProgramCounter {
    bool operator== (const ProgramCounter &other) const {
        return block == other.block
                && instructionIndex == other.instructionIndex
                && subInstruction == other.subInstruction;
    }
    bool operator!= (const ProgramCounter &other) const {
        return !(*this == other);
    }

    bool operator< (const ProgramCounter &other) const {
        if (block != other.block)
            return block < other.block;
        if (instructionIndex != other.instructionIndex)
            return instructionIndex < other.instructionIndex;
        return subInstruction < other.subInstruction;
    }
    bool operator<= (const ProgramCounter &other) const {
        return (*this < other) || (*this == other);
    }

    BasicBlock *block = nullptr;
    int instructionIndex = -1;
    SubInstruction subInstruction = atInstruction;
};

struct LiveCompound;

struct LiveInterval {
    LiveCompound *compound = nullptr;

    // Program counters of interval origin and final use.
    ProgramCounter originPc;
    ProgramCounter finalPc;

    // List of intervals that share the same compound.
    frg::default_list_hook<LiveInterval> compoundHook;

    // Intrusive tree that stores all intervals.
    frg::rbtree_hook rbHook;
    frg::interval_hook<ProgramCounter> intervalHook;
};

// Encapsulates multiple LiveIntervals that are always allocated to the same register.
struct LiveCompound {
    // Value that is allocated to the register.
    Value *associatedValue = nullptr;

    frg::intrusive_list<
        LiveInterval,
        frg::locate_member<
            LiveInterval,
            frg::default_list_hook<LiveInterval>,
            &LiveInterval::compoundHook
        >
    > intervals;

    int allocatedRegister = -1;
};

struct AllocateRegistersImpl : AllocateRegistersPass {
    AllocateRegistersImpl(Function *fn)
    : _fn{fn} { }

    void run() override;

private:
    void _collectIntervals(BasicBlock *bb);
    ProgramCounter _determineFinalPc(BasicBlock *bb, int originIndex, Value *v);
    void _establishAllocation(BasicBlock *bb);

    Function *_fn;

    std::unordered_map<Instruction *, int> _indexMap;

    // Stores all intervals that still need to be allocated.
    // TODO: Prioritize the intervals in some way, e.g. by use density.
    std::queue<LiveCompound *> _queue;

    // Stores all intervals that have already been allocated.
    frg::interval_tree<
        LiveInterval,
        ProgramCounter,
        &LiveInterval::originPc,
        &LiveInterval::finalPc,
        &LiveInterval::rbHook,
        &LiveInterval::intervalHook
    > _allocated;
};

void AllocateRegistersImpl::run() {
    for (auto bb : _fn->blocks())
        _collectIntervals(bb);

    // The following loop performs the actual allocation.
    while (!_queue.empty()) {
        auto compound = _queue.front();
        _queue.pop();

        // Determine a bitmask of registers that are already allocated.
        uint64_t registersBlocked = 0;
        for (auto interval : compound->intervals)
            _allocated.for_overlaps([&] (LiveInterval *overlap) {
                assert(overlap->compound->allocatedRegister >= 0);
                registersBlocked |= 1 << overlap->compound->allocatedRegister;
            }, interval->originPc, interval->finalPc);

        // Chose the first free register using the bitmask.
        // TODO: Currently, we just allocate to the first 4 registers: rax, rcx, rdx, rbx.
        //       Generalize this register model.
        for (int i = 0; i < 4; i++) {
            if (registersBlocked & (1 << i))
                continue;
            compound->allocatedRegister = i;
            break;
        }
        assert(compound->allocatedRegister >= 0
                && "Register spilling is not implemented yet");
        std::cout << "Allocating to register " << compound->allocatedRegister << std::endl;

        for (auto interval : compound->intervals)
            _allocated.insert(interval);
    }

    for (auto bb : _fn->blocks())
        _establishAllocation(bb);
}

// Called before allocation. Generates all LiveIntervals and adds them to the queue.
void AllocateRegistersImpl::_collectIntervals(BasicBlock *bb) {
    // Map instructions to monotonically increasing numbers.
    int nInstructionIndices = 1;
    for (auto inst : bb->instructions()) {
        _indexMap.insert({inst, nInstructionIndices});
        nInstructionIndices++;
    }

    // Generate LiveIntervals for phis.
    for (auto phi : bb->phis()) {
        auto compound = new LiveCompound;
        auto interval = new LiveInterval;
        compound->associatedValue = phi;
        compound->intervals.push_back(interval);
        interval->compound = compound;
        interval->originPc = {bb, 0, afterInstruction};
        interval->finalPc = _determineFinalPc(bb, 0, phi);
        _queue.push(compound);
    }

    // Generate LiveIntervals for instructions.
    int currentIndex = 1;
    for (auto inst : bb->instructions()) {
        assert(_indexMap.at(inst) == currentIndex);

        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            auto compound = new LiveCompound;
            auto interval = new LiveInterval;
            compound->associatedValue = movMC->result();
            compound->intervals.push_back(interval);
            interval->compound = compound;
            interval->originPc = {bb, currentIndex, afterInstruction};
            interval->finalPc = _determineFinalPc(bb, currentIndex, movMC->result());
            _queue.push(compound);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(inst);
                unaryMInPlace) {
            auto compound = new LiveCompound;
            auto interval = new LiveInterval;
            compound->associatedValue = unaryMInPlace->result();
            compound->intervals.push_back(interval);
            interval->compound = compound;
            interval->originPc = {bb, currentIndex, afterInstruction};
            interval->finalPc = _determineFinalPc(bb, currentIndex, unaryMInPlace->result());
            _queue.push(compound);
        } else {
            assert(!"Unexpected IR instruction");
        }

        currentIndex++;
    }
}

ProgramCounter AllocateRegistersImpl::_determineFinalPc(BasicBlock *bb, int originIndex, Value *v) {
    int finalIndex = -1;
    for (auto use : v->uses()) {
        if (!use->instruction()) // Currently, instruction() only return null for phis.
            return {bb, INT_MAX, beforeInstruction};
        auto useIndex = _indexMap.at(use->instruction());
        if (useIndex > finalIndex)
            finalIndex = useIndex;
    }
    if(finalIndex > 0)
        return {bb, finalIndex, beforeInstruction};
    return {bb, originIndex, afterInstruction};
}

// This is called *after* the actual allocation is done. It "implements" the allocation by
// fixing registers in the IR and generating necessary move instructions.
void AllocateRegistersImpl::_establishAllocation(BasicBlock *bb) {
    std::unordered_map<Value *, LiveInterval *> liveMap;
    std::unordered_map<Value *, LiveInterval *> resultMap;
    std::cout << "Fixing basic block " << bb << std::endl;

    // Find all intervals that originate from phis.
    _allocated.for_overlaps([&] (LiveInterval *interval) {
        assert(!interval->originPc.instructionIndex);
        auto compound = interval->compound;
        std::cout << "    Phi node " << compound->associatedValue << " is live" << std::endl;
        liveMap.insert({compound->associatedValue, interval});
    }, {bb, 0, afterInstruction});

    for (auto phi : bb->phis())
        assert(liveMap.find(phi) != liveMap.end());

    int currentIndex = 1;
    for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
        std::cout << "    Fixing instruction " << currentIndex << std::endl;
        assert(_indexMap.at(*it) == currentIndex);

        // Find all intervals that originate from the current PC.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc.instructionIndex != currentIndex)
                return;
            auto compound = interval->compound;
            std::cout << "        Instruction returns " << compound->associatedValue << std::endl;
            resultMap.insert({compound->associatedValue, interval});
        }, {bb, currentIndex, afterInstruction});

        // Emit code before the current instruction.
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto resultInterval = resultMap.at(movMC->result());
            auto resultCompound = resultInterval->compound;
            movMC->result()->modeRegister = resultCompound->allocatedRegister;
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto resultInterval = resultMap.at(unaryMInPlace->result());
            auto primaryInterval = liveMap.at(unaryMInPlace->primary.get());
            auto resultCompound = resultInterval->compound;
            auto primaryCompound = primaryInterval->compound;
            if(resultCompound->allocatedRegister != primaryCompound->allocatedRegister) {
                auto move = std::make_unique<MovMRInstruction>(unaryMInPlace->primary.get());
                move->result()->modeRegister = resultCompound->allocatedRegister;
                unaryMInPlace->primary = move->result();
                bb->insertInstruction(it, std::move(move));
            }
            unaryMInPlace->result()->modeRegister = resultCompound->allocatedRegister;
        } else {
            assert(!"Unexpected IR instruction");
        }

        // Erase all intervals that end before the current instruction.
        // TODO: In principle, the loops to erase intervals can be accelerated by maintaining
        //       a tree of intervals, ordered by their finalPc. For now, we just use a linear scan.
        for (auto it = liveMap.begin(); it != liveMap.end(); ) {
            auto finalPc = it->second->finalPc;
            if (finalPc == ProgramCounter{bb, currentIndex, beforeInstruction}) {
                it = liveMap.erase(it);
            }else{
                assert(finalPc.instructionIndex > currentIndex);
                ++it;
            }
        }

        // Merge all intervals that originate from the previous PC.
        liveMap.merge(resultMap);
        assert(resultMap.empty());

        // Erase all intervals that end after the current instruction.
        for (auto it = liveMap.begin(); it != liveMap.end(); ) {
            auto finalPc = it->second->finalPc;
            if (finalPc == ProgramCounter{bb, currentIndex, afterInstruction}) {
                it = liveMap.erase(it);
            }else{
                assert(finalPc.block == bb);
                assert(finalPc.instructionIndex > currentIndex);
                ++it;
            }
        }

        currentIndex++;
    }
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(Function *fn) {
    return std::make_unique<AllocateRegistersImpl>(fn);
}

} // namespace lewis::targets::x86_64
