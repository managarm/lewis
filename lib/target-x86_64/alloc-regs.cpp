
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <frg/interval_tree.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>

namespace lewis::targets::x86_64 {

namespace {
    constexpr bool ignorePenalties = false;

    constexpr uint64_t gprMask = 0xFCF;

    std::unique_ptr<Value> cloneModeValue(Value *value) {
        auto modeM = hierarchy_cast<ModeMValue *>(value);
        assert(modeM);
        auto clone = std::make_unique<ModeMValue>();
        clone->operandSize = modeM->operandSize;
        return clone;
    }

    void setRegister(Value *v, int registerIdx) {
        if (auto modeMValue = hierarchy_cast<ModeMValue *>(v); modeMValue) {
            modeMValue->modeRegister = registerIdx;
        } else {
            assert(!"Unexpected x86_64 IR value");
        }
    }
}

enum SubBlock {
    beforeBlock = -1,
    inBlock = 0,
    afterBlock = 1,
};

// We need to be able to distinguish the allocation situation before and after an instruction.
// atInstruction is used for clobbers.
enum SubInstruction {
    beforeInstruction = -1,
    atInstruction = 0,
    afterInstruction = 1,
};

// Represents a point in the program at which we perform register allocation.
struct ProgramCounter {
    friend std::ostream &operator<< (std::ostream &out, const ProgramCounter &pc) {
        assert(pc.block);
        if (pc.subBlock == beforeBlock) {
            out << "before block";
        } else if (pc.subBlock == afterBlock) {
            out << "after block";
        } else {
            assert(pc.subBlock == inBlock);
            assert(pc.instruction);
            if (pc.subInstruction == beforeInstruction) {
                out << "before ";
            } else if (pc.subInstruction == afterInstruction) {
                out << "after ";
            } else {
                assert(pc.subInstruction == atInstruction);
                out << "at ";
            }
            out << pc.block->indexOfInstruction(pc.instruction);
        }
        return out;
    }

    bool operator== (const ProgramCounter &other) const {
        return block == other.block
                && subBlock == other.subBlock
                && instruction == other.instruction
                && subInstruction == other.subInstruction;
    }
    bool operator!= (const ProgramCounter &other) const {
        return !(*this == other);
    }

    bool operator< (const ProgramCounter &other) const {
        if (block != other.block)
            return block < other.block;
        if (subBlock != other.subBlock)
            return subBlock < other.subBlock;
        if (instruction != other.instruction) {
            auto index = block->indexOfInstruction(instruction);
            auto otherIndex = block->indexOfInstruction(other.instruction);
            if (index != otherIndex)
                return index < otherIndex;
        }
        return subInstruction < other.subInstruction;
    }
    bool operator<= (const ProgramCounter &other) const {
        return (*this < other) || (*this == other);
    }

    bool operator> (const ProgramCounter &other) const {
        return !(*this <= other);
    }
    bool operator>= (const ProgramCounter &other) const {
        return !(*this < other);
    }

    BasicBlock *block = nullptr;
    SubBlock subBlock = inBlock;
    Instruction *instruction = nullptr;
    SubInstruction subInstruction = atInstruction;
};

struct LiveCompound;

struct LiveInterval {
    LiveInterval() = default;

    LiveInterval(const LiveInterval &) = delete;

    LiveInterval &operator= (const LiveInterval &) = delete;

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
    LiveCompound() = default;

    LiveCompound(const LiveCompound &) = delete;

    LiveCompound &operator= (const LiveCompound &) = delete;

    frg::intrusive_list<
        LiveInterval,
        frg::locate_member<
            LiveInterval,
            frg::default_list_hook<LiveInterval>,
            &LiveInterval::compoundHook
        >
    > intervals;

    int allocatedRegister = -1;

    uint64_t possibleRegisters = 0;
};

struct Penalty {
    std::array<LiveCompound *, 2> compounds;
};

struct AllocateRegistersImpl : AllocateRegistersPass {
    AllocateRegistersImpl(Function *fn)
    : _fn{fn} { }

    void run() override;

private:
    void _allocateCompound(LiveCompound *compound);
    void _collectBlockIntervals(BasicBlock *bb);
    std::optional<ProgramCounter> _determineFinalPc(BasicBlock *bb, Value *v);
    void _establishAllocation(BasicBlock *bb);

    Function *_fn;

    std::unordered_map<PhiNode *, LiveCompound *> _phiCompounds;

    // Stores all intervals that still need to be allocated.
    // TODO: Prioritize the intervals in some way, e.g. by use density.
    std::queue<LiveCompound *> _restrictedQueue;
    std::queue<LiveCompound *> _unrestrictedQueue;

    std::vector<Penalty> _penalties;

    // Stores all intervals that have already been allocated.
    frg::interval_tree<
        LiveInterval,
        ProgramCounter,
        &LiveInterval::originPc,
        &LiveInterval::finalPc,
        &LiveInterval::rbHook,
        &LiveInterval::intervalHook
    > _allocated;

    // Bitmask of all registers that are used.
    // The function prologue is constructed from this.
    uint64_t _usedRegisters = 0;

    // Some statistics to quantify the quality of the allocation.
    int _achievedCost = 0;
    int _numRegisterMoves = 0;
};

void AllocateRegistersImpl::run() {
    // LiveCompounds for phi nodes span multiple basic blocks.
    // Create them here and set them up in _collectBlockIntervals().
    for (auto bb : _fn->blocks()) {
        for (auto phi : bb->phis())
            _phiCompounds.insert({phi, new LiveCompound});
    }

    for (auto bb : _fn->blocks())
        _collectBlockIntervals(bb);

    // The following loops performs the actual allocation.
    // Perform "restricted" allocations first. Restricted allocations are those that *must*
    // fulfill certain conditions in order to yield a feasible allocation, i.e.
    // they cannot be split or spilled. The ISA has to guarantee that even without
    // splitting or spilling we can find a feasible allocation.
    // For x86 this is easy, as all restricted allocation always go into fixed registers.
    while (!_restrictedQueue.empty()) {
        auto compound = _restrictedQueue.front();
        _restrictedQueue.pop();
        _allocateCompound(compound);
    }
    // We perform unrestricted allocations afterwards. If those cannot be satisfied, we can
    // just split or spill the intervals. We *never* have to split or spill a restricted
    // allocation in this second loop, as those are all already fixed.
    while (!_unrestrictedQueue.empty()) {
        auto compound = _unrestrictedQueue.front();
        _unrestrictedQueue.pop();
        _allocateCompound(compound);
    }

    for (auto bb : _fn->blocks())
        _establishAllocation(bb);

    std::cout << "Allocation cost is " << _achievedCost << " units" << std::endl;
    std::cout << "Allocation requires " << _numRegisterMoves << " moves" << std::endl;
}

void AllocateRegistersImpl::_allocateCompound(LiveCompound *compound) {
    assert(compound->allocatedRegister < 0 && "Compound is allocated twice");

    // Determine which allocations would be possible.
    struct AllocationState {
        int relativeCost = 0;
        bool allocationPossible = true;
    };

    int baseCost = 0;
    AllocationState state[16];

    std::cout << "Allocating compound " << compound << ", possible registers: "
            << compound->possibleRegisters << std::endl;
    for (auto interval : compound->intervals) {
        std::cout << "    Interval " << interval << " for value " << interval->associatedValue
                << " at [" << interval->originPc << ", " << interval->finalPc << "]" << std::endl;

        _allocated.for_overlaps([&] (LiveInterval *overlap) {
            auto overlapRegister = overlap->compound->allocatedRegister;
            assert(overlapRegister >= 0);
            state[overlapRegister].allocationPossible = false;
        }, interval->originPc, interval->finalPc);
    }

    // Compute allocation penalties.
    for(auto penalty : _penalties) {
        LiveCompound *other;
        if (compound == penalty.compounds[0]) {
            other = penalty.compounds[1];
        } else if(compound == penalty.compounds[1]) {
            other = penalty.compounds[0];
        } else {
            continue;
        }

        if (other->allocatedRegister < 0)
            continue;

        std::cout << "    Want to allocate to register " << other->allocatedRegister
                << std::endl;

        // Instead of increasing cost everywhere, we increment the base cost
        // and add a negative contribution of a single register.
        baseCost += 1;
        state[other->allocatedRegister].relativeCost -= 1;
    }

    // Chose the best free register according to its cost.
    int bestRegister = -1;
    for (int i = 0; i < 16; i++) {
        if (!(compound->possibleRegisters & (1 << i)))
            continue;
        if (!state[i].allocationPossible)
            continue;

        std::cout << "    Register " << i << " has cost "
                << (baseCost + state[i].relativeCost) << std::endl;

        if (bestRegister < 0) {
            bestRegister = i;
        } else if (!ignorePenalties && state[bestRegister].relativeCost
                > state[i].relativeCost) {
            bestRegister = i;
        }
    }
    assert(bestRegister >= 0 && "Could not find possible register for allocation");

    compound->allocatedRegister = bestRegister;
    std::cout << "    Allocating to register " << compound->allocatedRegister
            << ", cost: " << (baseCost + state[bestRegister].relativeCost) << std::endl;

    for (auto interval : compound->intervals) {
        if (interval->associatedValue)
            setRegister(interval->associatedValue, compound->allocatedRegister);
        _allocated.insert(interval);
    }
    _usedRegisters |= 1 << bestRegister;
    _achievedCost += baseCost + state[bestRegister].relativeCost;
}

// Called before allocation. Generates all LiveIntervals and adds them to the queue.
void AllocateRegistersImpl::_collectBlockIntervals(BasicBlock *bb) {
    std::vector<LiveCompound *> collected;
    std::unordered_map<Value *, LiveCompound *> compoundMap;

    // Make sure to skip new instructions inserted at the beginning of the block.
    auto instructionsBegin = bb->instructions().begin();

    // Generate LiveIntervals for PhiNodes.
    for (auto phi : bb->phis()) {
        auto pseudoMove = bb->insertInstruction(instructionsBegin,
                std::make_unique<PseudoMoveSingleInstruction>());
        auto pseudoMoveResult = pseudoMove->result.set(cloneModeValue(phi->value.get()));
        phi->value.get()->replaceAllUses(pseudoMoveResult);
        pseudoMove->operand = phi->value.get();

        // Setup the LiveCompound that was reserved earlier.
        // Note that for data-flow phis, we amend the compound with intervals
        // in other basic blocks later on.
        auto nodeCompound = _phiCompounds.at(phi);
        if (auto argument = hierarchy_cast<ArgumentPhi *>(phi); argument) {
            nodeCompound->possibleRegisters = 0x80;

            auto nodeInterval = new LiveInterval;
            nodeCompound->intervals.push_back(nodeInterval);
            nodeInterval->associatedValue = phi->value.get();
            nodeInterval->compound = nodeCompound;
            nodeInterval->originPc = {bb, beforeBlock, nullptr, afterInstruction};
            nodeInterval->finalPc = {bb, inBlock, pseudoMove, beforeInstruction};
            assert(nodeInterval->associatedValue);
        } else if (auto dataFlow = hierarchy_cast<DataFlowPhi *>(phi); dataFlow) {
            nodeCompound->possibleRegisters = gprMask;

            auto nodeInterval = new LiveInterval;
            nodeCompound->intervals.push_back(nodeInterval);
            nodeInterval->associatedValue = phi->value.get();
            nodeInterval->compound = nodeCompound;
            nodeInterval->originPc = {bb, beforeBlock, nullptr, afterInstruction};
            nodeInterval->finalPc = {bb, inBlock, pseudoMove, beforeInstruction};
            assert(nodeInterval->associatedValue);
        } else {
            assert(!"Unexpected IR phi");
        }

        auto copyCompound = new LiveCompound;
        copyCompound->possibleRegisters = gprMask;

        auto copyInterval = new LiveInterval;
        copyCompound->intervals.push_back(copyInterval);
        copyInterval->associatedValue = pseudoMoveResult;
        copyInterval->compound = copyCompound;
        copyInterval->originPc = {bb, inBlock, pseudoMove, afterInstruction};

        compoundMap.insert({pseudoMoveResult, copyCompound});
        _unrestrictedQueue.push(nodeCompound);
        collected.push_back(copyCompound);
        _penalties.push_back(Penalty{{nodeCompound, copyCompound}});
    }

    // Generate LiveIntervals for instructions.
    for (auto it = instructionsBegin; it != bb->instructions().end(); ++it) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = gprMask;

            auto interval = new LiveInterval;
            compound->intervals.push_back(interval);
            interval->associatedValue = movMC->result.get();
            interval->compound = compound;
            interval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            assert(interval->associatedValue);

            compoundMap.insert({movMC->result.get(), compound});
            collected.push_back(compound);
        } else if (auto unaryMOverwrite = hierarchy_cast<UnaryMOverwriteInstruction *>(*it);
                unaryMOverwrite) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = gprMask;

            auto resultInterval = new LiveInterval;
            compound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = unaryMOverwrite->result.get();
            resultInterval->compound = compound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            assert(resultInterval->associatedValue);

            compoundMap.insert({unaryMOverwrite->result.get(), compound});
            collected.push_back(compound);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto originalPrimary = unaryMInPlace->primary.get();
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveSingleInstruction>(originalPrimary));
            auto pseudoMoveResult = pseudoMove->result.set(cloneModeValue(originalPrimary));
            unaryMInPlace->primary = pseudoMoveResult;

            auto compound = new LiveCompound;
            compound->possibleRegisters = gprMask;

            auto copyInterval = new LiveInterval;
            compound->intervals.push_back(copyInterval);
            copyInterval->associatedValue = pseudoMoveResult;
            copyInterval->compound = compound;
            copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};

            auto resultInterval = new LiveInterval;
            compound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = unaryMInPlace->result.get();
            resultInterval->compound = compound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            assert(resultInterval->associatedValue);

            compoundMap.insert({unaryMInPlace->result.get(), compound});
            collected.push_back(compound);
            _penalties.push_back(Penalty{{compoundMap.at(originalPrimary), compound}});
        } else if (auto binaryMRInPlace = hierarchy_cast<BinaryMRInPlaceInstruction *>(*it);
                binaryMRInPlace) {
            auto originalPrimary = binaryMRInPlace->primary.get();
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveSingleInstruction>(originalPrimary));
            auto pseudoMoveResult = pseudoMove->result.set(cloneModeValue(originalPrimary));
            binaryMRInPlace->primary = pseudoMoveResult;

            auto compound = new LiveCompound;
            compound->possibleRegisters = gprMask;

            auto copyInterval = new LiveInterval;
            compound->intervals.push_back(copyInterval);
            copyInterval->associatedValue = pseudoMoveResult;
            copyInterval->compound = compound;
            copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};

            auto resultInterval = new LiveInterval;
            compound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = binaryMRInPlace->result.get();
            resultInterval->compound = compound;
            resultInterval->originPc = {bb, inBlock, *it, afterInstruction};
            assert(resultInterval->associatedValue);

            compoundMap.insert({binaryMRInPlace->result.get(), compound});
            collected.push_back(compound);
            _penalties.push_back(Penalty{{compoundMap.at(originalPrimary), compound}});
        } else if (auto call = hierarchy_cast<CallInstruction *>(*it); call) {
            std::array<int, 6> operandRegs{0x80, 0x40, 0x04, 0x02, 0x100, 0x200};
            std::array<int, 2> clobberRegs{0x400, 0x800};

            // Add a PseudoMove instruction for the operands.
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveMultipleInstruction>(call->numOperands()));
            for (size_t i = 0; i < call->numOperands(); ++i) {
                auto originalOperand = call->operand(i).get();
                pseudoMove->operand(i) = originalOperand;
                auto pseudoMoveResult = pseudoMove->result(i).set(cloneModeValue(originalOperand));
                call->operand(i) = pseudoMoveResult;

                auto copyCompound = new LiveCompound;
                if (i < operandRegs.size())
                    copyCompound->possibleRegisters = operandRegs[i];
                else
                    assert(!"TODO: Implement correct ABI for arbitrary arguments");

                auto copyInterval = new LiveInterval;
                copyCompound->intervals.push_back(copyInterval);
                copyInterval->associatedValue = pseudoMoveResult;
                copyInterval->compound = copyCompound;
                copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};
                copyInterval->finalPc = ProgramCounter{bb, inBlock, *it, beforeInstruction};

                _restrictedQueue.push(copyCompound);
                _penalties.push_back(Penalty{{compoundMap.at(originalOperand), copyCompound}});
            }

            // Add LiveIntervals for clobbered operand registers.
            for (size_t i = call->numOperands(); i < operandRegs.size(); ++i) {
                auto clobberCompound = new LiveCompound;
                clobberCompound->possibleRegisters = operandRegs[i];

                auto clobberInterval = new LiveInterval;
                clobberCompound->intervals.push_back(clobberInterval);
                clobberInterval->compound = clobberCompound;
                clobberInterval->originPc = ProgramCounter{bb, inBlock, *it, atInstruction};
                clobberInterval->finalPc = ProgramCounter{bb, inBlock, *it, atInstruction};

                _restrictedQueue.push(clobberCompound);
            }

            // Add LiveIntervals for other clobbers.
            for (size_t i = 0; i < clobberRegs.size(); ++i) {
                auto clobberCompound = new LiveCompound;
                clobberCompound->possibleRegisters = clobberRegs[i];

                auto clobberInterval = new LiveInterval;
                clobberCompound->intervals.push_back(clobberInterval);
                clobberInterval->compound = clobberCompound;
                clobberInterval->originPc = ProgramCounter{bb, inBlock, *it, atInstruction};
                clobberInterval->finalPc = ProgramCounter{bb, inBlock, *it, atInstruction};

                _restrictedQueue.push(clobberCompound);
            }

            // TODO: When we make results optional, we still have to add a clobber for RAX.
            auto nit = it;
            ++nit;
            auto pseudoMoveRetval = bb->insertInstruction(nit,
                    std::make_unique<PseudoMoveSingleInstruction>());
            auto pseudoMoveRetvalResult = pseudoMoveRetval->result.set(cloneModeValue(call->result.get()));
            call->result.get()->replaceAllUses(pseudoMoveRetvalResult);
            pseudoMoveRetval->operand = call->result.get();

            // Add LiveIntervals for the results.
            auto resultCompound = new LiveCompound;
            resultCompound->possibleRegisters = 0x1;

            auto resultInterval = new LiveInterval;
            resultCompound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = call->result.get();
            resultInterval->compound = resultCompound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            resultInterval->finalPc = ProgramCounter{bb, inBlock, pseudoMoveRetval, beforeInstruction};
            assert(resultInterval->associatedValue);

            // Add a LiveInterval for a copy of the result.
            auto retvalCopyCompound = new LiveCompound;
            retvalCopyCompound->possibleRegisters = gprMask;

            auto retvalCopyInterval = new LiveInterval;
            retvalCopyCompound->intervals.push_back(retvalCopyInterval);
            retvalCopyInterval->associatedValue = pseudoMoveRetvalResult;
            retvalCopyInterval->compound = retvalCopyCompound;
            retvalCopyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMoveRetval, afterInstruction};

            compoundMap.insert({pseudoMoveRetvalResult, resultCompound});
            _restrictedQueue.push(resultCompound);
            collected.push_back(retvalCopyCompound);
            _penalties.push_back(Penalty{{resultCompound, retvalCopyCompound}});

            // Skip the PseudoMove instruction.
            ++it;
            assert(*it == pseudoMoveRetval);
        } else {
            std::cout << "lewis: Unknown instruction kind " << (*it)->kind << std::endl;
            assert(!"Unexpected IR instruction");
        }
    }

    // Generate a PseudoMove instruction for data-flow PhiNodes at the end of the block.
    // Modify all data-flow PhiNodes to take *only* this PseudoMove as input.
    std::vector<DataFlowEdge *> edges; // Need to index edges.
    for (auto edge : bb->source.edges())
        edges.push_back(edge);

    if (!edges.empty()) {
        auto pseudoMove = bb->insertInstruction(
                std::make_unique<PseudoMoveMultipleInstruction>(edges.size()));
        for (size_t i = 0; i < edges.size(); i++) {
            auto originalAlias = edges[i]->alias.get();
            pseudoMove->operand(i) = originalAlias;
            auto pseudoMoveResult = pseudoMove->result(i).set(cloneModeValue(originalAlias));
            edges[i]->alias = pseudoMoveResult;

            // Add an interval to the PhiNode's compound.
            auto nodeCompound = _phiCompounds.at(edges[i]->sink()->phiNode());
            auto sourceInterval = new LiveInterval;
            nodeCompound->intervals.push_back(sourceInterval);
            sourceInterval->associatedValue = pseudoMoveResult;
            sourceInterval->compound = nodeCompound;
            assert(sourceInterval->associatedValue);
            sourceInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};
            sourceInterval->finalPc = ProgramCounter{bb, afterBlock, nullptr, afterInstruction};

            _penalties.push_back(Penalty{{compoundMap.at(originalAlias), nodeCompound}});
        }
    }

    // Generate a PseudoMove instruction to function returns.
    if (auto ret = hierarchy_cast<RetBranch *>(bb->branch()); ret) {
        auto pseudoMove = bb->insertInstruction(
                std::make_unique<PseudoMoveMultipleInstruction>(ret->numOperands()));
        for (size_t i = 0; i < ret->numOperands(); ++i) {
            auto originalOperand = ret->operand(i).get();
            pseudoMove->operand(i) = originalOperand;
            auto pseudoMoveResult = pseudoMove->result(i).set(cloneModeValue(originalOperand));
            ret->operand(i) = pseudoMoveResult;

            auto copyCompound = new LiveCompound;
            switch (i) {
            case 0: copyCompound->possibleRegisters = 0x01; break;
            default: assert(!"TODO: Implement correct ABI for arbitrary return values");
            }

            auto copyInterval = new LiveInterval;
            copyCompound->intervals.push_back(copyInterval);
            copyInterval->associatedValue = pseudoMoveResult;
            copyInterval->compound = copyCompound;
            copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};
            copyInterval->finalPc = ProgramCounter{bb, afterBlock, nullptr, afterInstruction};

            _restrictedQueue.push(copyCompound);
            _penalties.push_back(Penalty{{compoundMap.at(originalOperand), copyCompound}});
        }
    } else if (auto jnz = hierarchy_cast<JnzBranch *>(bb->branch()); jnz) {
        auto originalOperand = jnz->operand.get();
        auto pseudoMove = bb->insertNewInstruction<PseudoMoveSingleInstruction>();
        pseudoMove->operand = originalOperand;
        auto pseudoMoveResult = pseudoMove->result.set(cloneModeValue(originalOperand));
        jnz->operand = pseudoMoveResult;

        auto copyCompound = new LiveCompound;
        copyCompound->possibleRegisters = gprMask;

        auto copyInterval = new LiveInterval;
        copyCompound->intervals.push_back(copyInterval);
        copyInterval->associatedValue = pseudoMoveResult;
        copyInterval->compound = copyCompound;
        copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};
        copyInterval->finalPc = ProgramCounter{bb, afterBlock, nullptr, afterInstruction};

        _unrestrictedQueue.push(copyCompound);
        _penalties.push_back(Penalty{{compoundMap.at(originalOperand), copyCompound}});
    }

    // Post-process the generated intervals.
    for (auto compound : collected) {
        for (auto interval : compound->intervals) {
            // Compute the final PC for each interval.
            assert(interval->associatedValue);
            auto maybeFinalPc = _determineFinalPc(bb, interval->associatedValue);
            interval->finalPc = maybeFinalPc.value_or(interval->originPc);
        }

        // TODO: This popcount is ugly. Find a better solution.
        assert(__builtin_popcountl(compound->possibleRegisters) > 1);
        _unrestrictedQueue.push(compound);
    }
}

std::optional<ProgramCounter> AllocateRegistersImpl::_determineFinalPc(BasicBlock *bb, Value *v) {
    Instruction *finalInst = nullptr;
    size_t finalIndex;
    for (auto use : v->uses()) {
        // We should never see uses in DataFlowEdges, as they should all originate from the
        // PseudoMove instruction generated in _collectBlockIntervals().
        // This function is never called on those values.
        assert(use->instruction());
        auto useInst = use->instruction();
        auto useIndex = bb->indexOfInstruction(useInst);
        if (!finalInst || useIndex > finalIndex) {
            finalInst = useInst;
            finalIndex = useIndex;
        }
    }
    if(finalInst)
        return ProgramCounter{bb, inBlock, finalInst, beforeInstruction};
    return std::nullopt;
}

// This is called *after* the actual allocation is done. It "implements" the allocation by
// fixing registers in the IR and generating necessary move instructions.
void AllocateRegistersImpl::_establishAllocation(BasicBlock *bb) {
    // Callee-saved registers (i.e. owned by the caller).
    const uint64_t callerRegs = 0xF028;

    // Mask of registers that need to be saved.
    auto saveMask = callerRegs & _usedRegisters;

    // Stack space required by this function.
    auto stackSpace = __builtin_popcountl(saveMask) * 8;
    assert((stackSpace & 0xF) && "TODO: adjust the stack to be aligned on call");

    // Generate the function prologue.
    if (bb == *_fn->blocks().begin()) {
        for (int i = 0; i < 16; i++) {
            if (!(saveMask & (1 << i)))
                continue;
            bb->insertInstruction(bb->instructions().begin(),
                    std::make_unique<PushSaveInstruction>(i));
        }
    }

    std::unordered_map<Value *, LiveInterval *> liveMap;
    std::unordered_map<Value *, LiveInterval *> resultMap;
    std::cout << "Fixing basic block " << bb << std::endl;

    for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ) {
        std::cout << "    Fixing instruction " << bb->indexOfInstruction(*it) << ", kind "
                << (*it)->kind << std::endl;
        // Fill the liveMap and the resultMap.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc < ProgramCounter{bb, inBlock, *it, beforeInstruction})
                liveMap.insert({interval->associatedValue, interval});
            else if (interval->originPc == ProgramCounter{bb, inBlock, *it, afterInstruction})
                resultMap.insert({interval->associatedValue, interval});
        }, {bb, inBlock, *it, beforeInstruction}, {bb, inBlock, *it, afterInstruction});

        // Determine the current register allocation.
        // TODO: This does not take clobbers into account.
        LiveInterval *currentState[16] = {};

        for (auto entry : liveMap) {
            auto interval = entry.second;
            auto currentRegister = interval->compound->allocatedRegister;
            assert(currentRegister >= 0);
            assert(!currentState[currentRegister]);
            currentState[currentRegister] = interval;
            std::cout << "        Current state[" << currentRegister << "]: "
                    << interval->associatedValue << std::endl;
        }

        for (auto entry : liveMap) {
            auto interval = entry.second;
            std::cout << "        Instruction returns "
                    << interval->associatedValue << std::endl;
        }

        // Helper function to fuse a result interval (from resultMap)
        // into a live interval (from liveMap).
        auto fuseResultInterval = [&] (LiveInterval *interval, LiveInterval *into) {
            assert(interval->associatedValue && into->associatedValue);

            auto resIt = resultMap.find(interval->associatedValue);
            assert(resIt != resultMap.end());
            resultMap.erase(resIt);
            _allocated.remove(interval);

            assert((into->finalPc == ProgramCounter{bb, inBlock, *it, beforeInstruction}));
            _allocated.remove(into);
            // TODO: In this case, the finalPc now points to a non-existing instruction.
            //       Find the second to last PC and update finalPc correctly.
            if(interval->finalPc != ProgramCounter{bb, inBlock, *it, afterInstruction})
                into->finalPc = interval->finalPc;
            _allocated.insert(into);
        };

        // Helper function to rewrite the associatedValue of a result interval (from resultMap).
        auto reassociateResultInterval = [&] (LiveInterval *interval, Value *newValue) {
            assert(interval->associatedValue);

            auto resIt = resultMap.find(interval->associatedValue);
            assert(resIt != resultMap.end());
            resultMap.erase(resIt);
            _allocated.remove(interval);

            interval->associatedValue = newValue;
            resultMap.insert({newValue, interval});
        };

        // Rewrite pseudo instructions to real instructions.
        bool rewroteInstruction = false;
        if (auto pseudoMoveSingle = hierarchy_cast<PseudoMoveSingleInstruction *>(*it);
                pseudoMoveSingle) {
            auto operandInterval = liveMap.at(pseudoMoveSingle->operand.get());
            auto resultInterval = resultMap.at(pseudoMoveSingle->result.get());
            if (operandInterval->compound->allocatedRegister
                    == resultInterval->compound->allocatedRegister) {
                std::cout << "        Rewriting pseudoMoveSingle (fuse)" << std::endl;
                pseudoMoveSingle->result.get()->replaceAllUses(pseudoMoveSingle->operand.get());

                fuseResultInterval(resultInterval, operandInterval);
            }else{
                std::cout << "        Rewriting pseudoMoveSingle (reassociate)" << std::endl;
                auto movMR = std::make_unique<MovMRInstruction>(pseudoMoveSingle->operand.get());
                auto movMRResult = movMR->result.set(cloneModeValue(pseudoMoveSingle->operand.get()));
                setRegister(movMRResult, resultInterval->compound->allocatedRegister);

                pseudoMoveSingle->operand = nullptr;
                pseudoMoveSingle->result.get()->replaceAllUses(movMRResult);

                reassociateResultInterval(resultInterval, movMRResult);
                bb->insertInstruction(it, std::move(movMR));
                _numRegisterMoves++;
            }

            rewroteInstruction = true;
        } else if (auto pseudoMoveMultiple = hierarchy_cast<PseudoMoveMultipleInstruction *>(*it);
                pseudoMoveMultiple) {
            // The following code minimizes the number of move instructions.
            // This is done as follows:
            // - The code constructs "move chains", i.e., chains of registers that need to be moved.
            //   For example, such a chain could be rax -> rcx -> rdx.
            // - The resulting graph only consists of paths and cycles
            //   (as every register has in-degree at most 1).
            // - Emit those paths in cycles.
            std::cout << "        Rewriting pseudoMoveMultiple" << std::endl;

            // Represents a node of the move chain graph.
            struct MoveChain {
                // ----------------------------------------------------------------------
                // Members defined for all MoveChains.
                // ----------------------------------------------------------------------

                bool isTail() {
                    if(!isTarget())
                        return false;
                    if(isSource() && pendingMovesFromThisSource)
                        return false;
                    return true;
                }

                bool seenInTraversal = false;
                bool traversalFinished = false;

                MoveChain *cyclePointer = nullptr;

                // ----------------------------------------------------------------------
                // Members defined for move *sources*.
                // ----------------------------------------------------------------------

                bool isSource() {
                    return firstTarget;
                }

                // Index of the corresponding PseudoMoveMultiple operand.
                int operandIndex = -1;

                // First chain that has this chain as uniqueSource.
                // TODO: Do we actually need this linked list?
                MoveChain *firstTarget = nullptr;

                // Number of non-emitted moves until this tail becomes active.
                int pendingMovesFromThisSource = 0;

                // ----------------------------------------------------------------------
                // Members defined for move *targets*.
                // ----------------------------------------------------------------------

                bool isTarget() {
                    return uniqueSource;
                }

                // Index of the corresponding PseudoMoveMultiple result.
                // TODO: Do we actually need this index?
                int resultIndex = -1;

                MoveChain *uniqueSource = nullptr;

                // Next chain that shares the same uniqueSource.
                MoveChain *siblingTarget = nullptr;

                bool didMoveToThisTarget = false;

                // ----------------------------------------------------------------------
                // Members defined for cycle representatives (pointed to by cyclePointer).
                // ----------------------------------------------------------------------

                // Number of non-emitted moves until this cycle becomes active.
                int pendingMovesFromThisCycle = 0;
            };

            MoveChain chains[16];

            auto chainRegister = [&] (MoveChain *chain) -> int {
                return chain - chains;
            };

            // Build the MoveChains from the PseudoMoveMultiple instruction.
            for (size_t i = 0; i < pseudoMoveMultiple->arity(); ++i) {
                auto operandInterval = liveMap.at(pseudoMoveMultiple->operand(i).get());
                auto resultInterval = resultMap.at(pseudoMoveMultiple->result(i).get());

                // Special case self-loops in move chains (no move is necessary).
                auto operandRegister = operandInterval->compound->allocatedRegister;
                auto resultRegister = resultInterval->compound->allocatedRegister;
                assert(operandRegister >= 0);
                assert(resultRegister >= 0);
                if (operandRegister == resultRegister) {
                    fuseResultInterval(resultInterval, operandInterval);
                    continue;
                }

                // Setup the MoveChain structs.
                auto operandChain = &chains[operandRegister];
                auto resultChain = &chains[resultRegister];

                operandChain->resultIndex = i;
                resultChain->operandIndex = i;

                assert(!resultChain->uniqueSource);
                assert(!resultChain->siblingTarget);
                resultChain->uniqueSource = operandChain;
                resultChain->siblingTarget = operandChain->firstTarget;

                operandChain->firstTarget = resultChain;
                operandChain->pendingMovesFromThisSource++;
            }

            std::vector<MoveChain *> activeTails;
            std::vector<MoveChain *> activeCycles;

            // Helper function to emit a single move of a move chain.
            auto emitMoveToChain = [&] (MoveChain *targetChain) {
                auto index = targetChain->operandIndex;
                auto srcChain = targetChain->uniqueSource;
                assert(!targetChain->didMoveToThisTarget);
                assert(srcChain->pendingMovesFromThisSource > 0);

                auto operandInterval = liveMap.at(pseudoMoveMultiple->operand(index).get());
                auto resultInterval = resultMap.at(pseudoMoveMultiple->result(index).get());
                assert(operandInterval->compound->allocatedRegister == chainRegister(srcChain));
                assert(resultInterval->compound->allocatedRegister == chainRegister(targetChain));

                // Emit the new move instruction.
                auto move = std::make_unique<MovMRInstruction>(
                        pseudoMoveMultiple->operand(index).get());
                auto moveResult = move->result.set(cloneModeValue(pseudoMoveMultiple->operand(index).get()));
                setRegister(moveResult, resultInterval->compound->allocatedRegister);

                pseudoMoveMultiple->operand(index) = nullptr;
                pseudoMoveMultiple->result(index).get()->replaceAllUses(moveResult);

                reassociateResultInterval(resultInterval, moveResult);
                bb->insertInstruction(it, std::move(move));
                _numRegisterMoves++;

                // Update the MoveChain structs.
                targetChain->didMoveToThisTarget = true;

                srcChain->pendingMovesFromThisSource--;
                if (srcChain->isTail())
                    activeTails.push_back(srcChain);

                auto cycleChain = srcChain->cyclePointer;
                if(cycleChain && cycleChain != targetChain->cyclePointer) {
                    cycleChain->pendingMovesFromThisCycle--;
                    if (!cycleChain->pendingMovesFromThisCycle)
                        activeCycles.push_back(cycleChain);
                }
            };

            // Traverse the graph backwards and determine all tails and cycles.
            std::vector<MoveChain *> stack;
            for (int i = 0; i < 16; i++) {
                auto rootChain = &chains[i];

                if (rootChain->isTail())
                    activeTails.push_back(rootChain);

                auto current = rootChain;
                while (current) {
                    // Check if we ran into a chain that was already traversed completely.
                    if (current->traversalFinished)
                        break;

                    // If we reach a visited but not finished chain, we ran into a cycle.
                    if (current->seenInTraversal) {
                        // current will become the cyclePointer.
                        auto it = stack.rbegin();
                        do {
                            // As current is not finished, it must be on the stack.
                            assert(it != stack.rend());
                            (*it)->cyclePointer = current;

                            // Accumulate the moves out of the cycle. Note that exactly one of the
                            // moves from pendingMovesFromThisSource is inside the cycle.
                            assert((*it)->pendingMovesFromThisSource > 0);
                            current->pendingMovesFromThisCycle
                                    += (*it)->pendingMovesFromThisSource - 1;
                        } while(*(it++) != current);
                        break;
                    }

                    current->seenInTraversal = true;
                    stack.push_back(current);

                    current = current->uniqueSource;
                }

                for (auto chain : stack)
                    chain->traversalFinished = true;
                stack.clear();
            }

            // First, handle all tails.
            while (!activeTails.empty()) {
                auto tailRegister = activeTails.back();
                activeTails.pop_back();
                emitMoveToChain(tailRegister);
            }

            // Now, handle all cycles.
            while (!activeCycles.empty()) {
                assert (!"Implement move cycles");
                // TODO: For cycles of length 2, use xchg.
                //       Otherwise, allocate a new temporary register.

                // Resolving the cycle always results in a tail.
                while (!activeTails.empty()) {
                    auto tailRegister = activeTails.back();
                    activeTails.pop_back();
                    emitMoveToChain(tailRegister);
                }
            }

            rewroteInstruction = true;
        }

        auto nextIt = it;
        ++nextIt;
        if (rewroteInstruction)
            bb->eraseInstruction(it);
        it = nextIt;

        liveMap.clear();
        resultMap.clear();
    }

    // Generate the function epilogue.
    if (auto ret = hierarchy_cast<RetBranch *>(bb->branch()); ret) {
        for (int i = 15; i >= 0; i--) {
            if (!(saveMask & (1 << i)))
                continue;
            bb->insertInstruction(std::make_unique<PopRestoreInstruction>(i));
        }
    }
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(Function *fn) {
    return std::make_unique<AllocateRegistersImpl>(fn);
}

} // namespace lewis::targets::x86_64
