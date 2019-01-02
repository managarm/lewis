
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
    // Value that is allocated to the register.
    // Note that for LiveCompounds that represent phi nodes, the associatedValue is
    // different in each source BasicBlock.
    Value *associatedValue = nullptr;

    LiveCompound *compound = nullptr;

    // Program counters of interval origin and final use.
    ProgramCounter originPc;
    ProgramCounter finalPc;

    bool inMoveChain = false;
    LiveInterval *previousMoveInChain = nullptr;

    // List of intervals that share the same compound.
    frg::default_list_hook<LiveInterval> compoundHook;

    // Intrusive tree that stores all intervals.
    frg::rbtree_hook rbHook;
    frg::interval_hook<ProgramCounter> intervalHook;
};

// Encapsulates multiple LiveIntervals that are always allocated to the same register.
struct LiveCompound {
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
            std::cout << "    Affects " << interval->associatedValue
                    << " at [" << interval->originPc.instructionIndex
                    << ", " << interval->finalPc.instructionIndex << "]" << std::endl;

        for (auto interval : compound->intervals)
            _allocated.insert(interval);
    }

    for (auto bb : _fn->blocks())
        _establishAllocation(bb);
}

// Called before allocation. Generates all LiveIntervals and adds them to the queue.
void AllocateRegistersImpl::_collectIntervals(BasicBlock *bb) {
    // Map instructions to monotonically increasing numbers.
    size_t nInstructionIndices = 1;
    for (auto inst : bb->instructions()) {
        assert(bb->indexOfInstruction(inst) + 1 == nInstructionIndices);
        _indexMap.insert({inst, nInstructionIndices});
        nInstructionIndices++;
    }

    // Generate LiveIntervals for phis.
    for (auto phi : bb->phis()) {
        auto compound = new LiveCompound;

        auto nodeInterval = new LiveInterval;
        compound->intervals.push_back(nodeInterval);
        nodeInterval->associatedValue = phi;
        nodeInterval->compound = compound;
        nodeInterval->originPc = {bb, 0, afterInstruction};
        nodeInterval->finalPc = _determineFinalPc(bb, 0, phi);

        for (auto edge : phi->edges()) {
            auto aliasInterval = new LiveInterval;
            assert(edge->alias);
            compound->intervals.push_back(aliasInterval);
            aliasInterval->associatedValue = edge->alias.get();
            aliasInterval->compound = compound;
            aliasInterval->originPc = {edge->source, INT_MAX, afterInstruction};
            aliasInterval->finalPc = {edge->source, INT_MAX, afterInstruction};
        }

        _queue.push(compound);
    }

    // Generate LiveIntervals for instructions.
    int currentIndex = 1;
    for (auto inst : bb->instructions()) {
        assert(_indexMap.at(inst) == currentIndex);

        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            auto compound = new LiveCompound;
            auto interval = new LiveInterval;
            compound->intervals.push_back(interval);
            interval->associatedValue = movMC->result();
            interval->compound = compound;
            interval->originPc = {bb, currentIndex, afterInstruction};
            interval->finalPc = _determineFinalPc(bb, currentIndex, movMC->result());
            _queue.push(compound);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(inst);
                unaryMInPlace) {
            auto compound = new LiveCompound;
            auto interval = new LiveInterval;
            compound->intervals.push_back(interval);
            interval->associatedValue = unaryMInPlace->result();
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
        std::cout << "    Phi node " << interval->associatedValue << " is live" << std::endl;
        liveMap.insert({interval->associatedValue, interval});
    }, {bb, 0, afterInstruction});

    for (auto phi : bb->phis()) {
        auto modeM = hierarchy_cast<ModeMPhiNode *>(phi);
        assert(modeM);
        auto interval = liveMap.at(phi);
        modeM->modeRegister = interval->compound->allocatedRegister;
    }

    int currentIndex = 1;
    for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
        std::cout << "    Fixing instruction " << currentIndex << ", kind "
                << (*it)->kind << std::endl;
        assert(_indexMap.at(*it) == currentIndex);

        // Find all intervals that originate from the current PC.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc.instructionIndex != currentIndex)
                return;
            std::cout << "        Instruction returns " << interval->associatedValue << std::endl;
            resultMap.insert({interval->associatedValue, interval});
        }, {bb, currentIndex, afterInstruction});

        // Emit code before the current instruction.
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto resultInterval = resultMap.at(movMC->result());
            auto resultCompound = resultInterval->compound;
            movMC->result()->modeRegister = resultCompound->allocatedRegister;
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            assert(unaryMInPlace->primary);
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

    // The following code emits moves to ensure that Values that are used in phis are in the
    // correct register before a branch. It minimizes the number of move instructions.
    // This is done as follows:
    // - The code constructs "move chains", i.e., chains of registers that need to be moved.
    //   For example, such a chain could be rax -> rcx -> rdx.
    // - The last move is emitted first, as the contents of that register are not used anymore.
    // - If a move chain forms a cycle, an additional move is necessary.

    // First, determine the current register allocation.
    LiveInterval *currentState[4] = {nullptr};

    for (auto entry : liveMap) {
        auto interval = entry.second;
        auto compound = interval->compound;
        assert(compound->allocatedRegister >= 0);
        assert(!currentState[compound->allocatedRegister]);
        currentState[compound->allocatedRegister] = interval;
    }

    _allocated.for_overlaps([&] (LiveInterval *interval) {
        resultMap.insert({interval->associatedValue, interval});
    }, {bb, INT_MAX, afterInstruction});

    // Determine the head of each move chain.
    struct MoveChain {
        LiveInterval *lastInChain;
        bool isCyclic = false;
    };
    std::vector<MoveChain> chainList;
    for (auto entry : resultMap) {
        auto entryInterval = entry.second;
        std::cout << "    Expanding move chain at "
                << entryInterval->associatedValue << std::endl;

        // Special case self-loops in move chains (no move is necessary).
        auto entryRegister = entryInterval->compound->allocatedRegister;
        assert(entryRegister >= 0);
        if (currentState[entryRegister]
                && currentState[entryRegister]->associatedValue == entryInterval->associatedValue)
            continue;

        // Follow the move chain starting at the current entry until it's end.
        auto chainInterval = entryInterval;
        while(true) {
            if (chainInterval->inMoveChain)
                break;
            chainInterval->inMoveChain = true;

            auto chainRegister = chainInterval->compound->allocatedRegister;
            assert(chainRegister >= 0);

            // Check if the value that is currently in the chainRegister has to be moved.
            auto srcInterval = currentState[chainRegister];
            if (!srcInterval) {
                chainList.push_back({chainInterval, false});
                break;
            }

            auto destIt = resultMap.find(srcInterval->associatedValue);
            if (destIt == resultMap.end()) {
                chainList.push_back({chainInterval, false});
                break;
            }

            auto destInterval = destIt->second;
            std::cout << destInterval->associatedValue << std::endl;
            std::cout << "        Move chain: " << srcInterval->associatedValue
                    << " from " << chainRegister
                    << " to " << destInterval->compound->allocatedRegister << std::endl;
            if (destInterval == entryInterval) {
                // We ran into a cycle.
                chainList.push_back({chainInterval, true});
                break;
            }

            // The current chainInterval was not visited before; thus, we can assert:
            assert(!destInterval->previousMoveInChain);
            destInterval->previousMoveInChain = chainInterval;
            chainInterval = destInterval;
        }
    }

    // Actually emit the move chains.
    for (const auto &moveChain : chainList) {
        if (!moveChain.isCyclic) {
            // The move chain is a path.
            auto chainInterval = moveChain.lastInChain;
            do {
                assert(chainInterval->associatedValue);
                auto move = std::make_unique<MovMRInstruction>(chainInterval->associatedValue);
                move->result()->modeRegister = chainInterval->compound->allocatedRegister;
                bb->insertInstruction(std::move(move));

                chainInterval = chainInterval->previousMoveInChain;
            } while (chainInterval);
        } else {
            // The move chain is a cycle.
            auto nextResult = moveChain.lastInChain->associatedValue;
            auto nextRegister = moveChain.lastInChain->compound->allocatedRegister;
            auto chainInterval = moveChain.lastInChain->previousMoveInChain;
            do {
                assert(chainInterval->associatedValue);
                auto move = std::make_unique<XchgMRInstruction>(nextResult,
                        chainInterval->associatedValue);
                move->firstResult()->modeRegister = nextRegister;
                move->secondResult()->modeRegister = chainInterval->compound->allocatedRegister;
                nextResult = move->secondResult();
                nextRegister = chainInterval->compound->allocatedRegister;
                bb->insertInstruction(std::move(move));

                chainInterval = chainInterval->previousMoveInChain;
            } while (chainInterval);
        }
    }

    // TODO: Fix the ValueUses in phi edges.
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(Function *fn) {
    return std::make_unique<AllocateRegistersImpl>(fn);
}

} // namespace lewis::targets::x86_64
