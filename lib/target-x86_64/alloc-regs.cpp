
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <frg/interval_tree.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>

namespace lewis::targets::x86_64 {

namespace {
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
// The atInstruction value is not really used.
enum SubInstruction {
    beforeInstruction = -1,
    atInstruction = 0,
    afterInstruction = 1,
};

// Represents a point in the program at which we perform register allocation.
struct ProgramCounter {
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

    BasicBlock *block = nullptr;
    SubBlock subBlock = inBlock;
    Instruction *instruction = nullptr;
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

    uint64_t possibleRegisters = 0;
};

struct AllocateRegistersImpl : AllocateRegistersPass {
    AllocateRegistersImpl(Function *fn)
    : _fn{fn} { }

    void run() override;

private:
    void _collectIntervals(BasicBlock *bb);
    std::optional<ProgramCounter> _determineFinalPc(BasicBlock *bb, Value *v);
    void _establishAllocation(BasicBlock *bb);

    Function *_fn;

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
        for (int i = 0; i < 8; i++) {
            if (!(compound->possibleRegisters & (1 << i)))
                continue;
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
                    << " at [" << interval->originPc.instruction
                    << ", " << interval->finalPc.instruction << "]" << std::endl;

        for (auto interval : compound->intervals)
            _allocated.insert(interval);
    }

    for (auto bb : _fn->blocks())
        _establishAllocation(bb);
}

// Called before allocation. Generates all LiveIntervals and adds them to the queue.
void AllocateRegistersImpl::_collectIntervals(BasicBlock *bb) {
    std::vector<LiveCompound *> collected;

    // Generate LiveIntervals for phis.
    for (auto phi : bb->phis()) {
        if (auto argument = hierarchy_cast<ArgumentPhi *>(phi); argument) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = 0x80;

            auto nodeInterval = new LiveInterval;
            compound->intervals.push_back(nodeInterval);
            nodeInterval->associatedValue = phi->value.get();
            nodeInterval->compound = compound;
            nodeInterval->originPc = {bb, beforeBlock, nullptr, afterInstruction};
            auto maybeFinalPc = _determineFinalPc(bb, phi->value.get());
            nodeInterval->finalPc = maybeFinalPc.value_or(nodeInterval->originPc);

            _queue.push(compound);
        } else if (auto dataFlow = hierarchy_cast<DataFlowPhi *>(phi); dataFlow) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = 0xF;

            auto nodeInterval = new LiveInterval;
            compound->intervals.push_back(nodeInterval);
            nodeInterval->associatedValue = phi->value.get();
            nodeInterval->compound = compound;
            nodeInterval->originPc = {bb, beforeBlock, nullptr, afterInstruction};
            auto maybeFinalPc = _determineFinalPc(bb, phi->value.get());
            nodeInterval->finalPc = maybeFinalPc.value_or(nodeInterval->originPc);

            for (auto edge : dataFlow->sink.edges()) {
                auto aliasInterval = new LiveInterval;
                assert(edge->alias);
                compound->intervals.push_back(aliasInterval);
                aliasInterval->associatedValue = edge->alias.get();
                aliasInterval->compound = compound;
                aliasInterval->originPc = ProgramCounter{edge->source()->block(), afterBlock,
                        nullptr, afterInstruction};
                aliasInterval->finalPc = ProgramCounter{edge->source()->block(), afterBlock,
                        nullptr, afterInstruction};
            }

            _queue.push(compound);
        } else {
            assert(!"Unexpected IR phi");
        }
    }

    // Generate LiveIntervals for instructions.
    for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = 0xF;

            auto interval = new LiveInterval;
            compound->intervals.push_back(interval);
            interval->associatedValue = movMC->result.get();
            interval->compound = compound;
            interval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            collected.push_back(compound);
        } else if (auto unaryMOverwrite = hierarchy_cast<UnaryMOverwriteInstruction *>(*it);
                unaryMOverwrite) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = 0xF;

            auto resultInterval = new LiveInterval;
            compound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = unaryMOverwrite->result.get();
            resultInterval->compound = compound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};

            collected.push_back(compound);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMovMRInstruction>(unaryMInPlace->primary.get()));
            auto pseudoMoveResult = pseudoMove->result.setNew<ModeMValue>();
            unaryMInPlace->primary = pseudoMoveResult;

            auto compound = new LiveCompound;
            compound->possibleRegisters = 0xF;

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

            collected.push_back(compound);
        } else if (auto binaryMRInPlace = hierarchy_cast<BinaryMRInPlaceInstruction *>(*it);
                binaryMRInPlace) {
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMovMRInstruction>(binaryMRInPlace->primary.get()));
            auto pseudoMoveResult = pseudoMove->result.setNew<ModeMValue>();
            binaryMRInPlace->primary = pseudoMoveResult;

            auto compound = new LiveCompound;
            compound->possibleRegisters = 0xF;

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

            collected.push_back(compound);
        } else if (auto call = hierarchy_cast<CallInstruction *>(*it); call) {
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMovMRInstruction>(call->operand.get()));
            call->operand = pseudoMove->result.get();

            auto copyCompound = new LiveCompound;
            copyCompound->possibleRegisters = 0x80;

            auto copyInterval = new LiveInterval;
            copyCompound->intervals.push_back(copyInterval);
            copyInterval->associatedValue = pseudoMove->result.get();
            copyInterval->compound = copyCompound;
            copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};

            auto resultCompound = new LiveCompound;
            resultCompound->possibleRegisters = 0x1;

            auto resultInterval = new LiveInterval;
            resultCompound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = call->result.get();
            resultInterval->compound = resultCompound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};

            collected.push_back(copyCompound);
            collected.push_back(resultCompound);
        } else {
            std::cout << "lewis: Unknown instruction kind " << (*it)->kind << std::endl;
            assert(!"Unexpected IR instruction");
        }
    }

    // Post-process the generated intervals.
    for (auto compound : collected) {
        for (auto interval : compound->intervals) {
            // Compute the final PC for each interval.
            auto maybeFinalPc = _determineFinalPc(bb, interval->associatedValue);
            interval->finalPc = maybeFinalPc.value_or(interval->originPc);
        }

        _queue.push(compound);
    }
}

std::optional<ProgramCounter> AllocateRegistersImpl::_determineFinalPc(BasicBlock *bb, Value *v) {
    Instruction *finalInst = nullptr;
    size_t finalIndex;
    for (auto use : v->uses()) {
        if (!use->instruction()) // Currently, instruction() only return null for phis.
            return ProgramCounter{bb, afterBlock, nullptr, beforeInstruction};
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
    std::unordered_map<Value *, LiveInterval *> liveMap;
    std::unordered_map<Value *, LiveInterval *> resultMap;
    std::cout << "Fixing basic block " << bb << std::endl;

    // Find all intervals that originate from phis.
    _allocated.for_overlaps([&] (LiveInterval *interval) {
        std::cout << "    Value " << interval->associatedValue
                << " (from phi node) is live" << std::endl;
        liveMap.insert({interval->associatedValue, interval});
        setRegister(interval->associatedValue, interval->compound->allocatedRegister);
    }, {bb, beforeBlock, nullptr, afterInstruction});

    for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ) {
        std::cout << "    Fixing instruction " << *it << ", kind "
                << (*it)->kind << std::endl;

        // Find all intervals that originate from the current PC.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc.instruction != *it)
                return;
            std::cout << "        Instruction returns " << interval->associatedValue << std::endl;
            resultMap.insert({interval->associatedValue, interval});
        }, {bb, inBlock, *it, afterInstruction});

        // Helper function to fuse a result interval (from resultMap)
        // into a live interval (from liveMap).
        auto fuseResultInterval = [&] (LiveInterval *into, LiveInterval *interval) {
            auto resIt = resultMap.find(interval->associatedValue);
            assert(resIt != resultMap.end());
            resultMap.erase(resIt);

            assert((into->finalPc == ProgramCounter{bb, inBlock, *it, beforeInstruction}));
            into->finalPc = interval->finalPc;
        };

        // Helper function to rewrite the associatedValue of a result interval (from resultMap).
        auto reassociateResultInterval = [&] (LiveInterval *interval, Value *newValue) {
            auto resIt = resultMap.find(interval->associatedValue);
            assert(resIt != resultMap.end());
            resultMap.erase(resIt);

            interval->associatedValue = newValue;
            resultMap.insert({newValue, interval});
        };

        // Rewrite pseudo instructions to real instructions.
        bool rewroteInstruction = false;
        if (auto pseudoMovMR = hierarchy_cast<PseudoMovMRInstruction *>(*it); pseudoMovMR) {
            std::cout << "        Rewriting pseudoMovMR" << std::endl;
            auto operandInterval = liveMap.at(pseudoMovMR->operand.get());
            auto resultInterval = resultMap.at(pseudoMovMR->result.get());
            if (operandInterval->compound->allocatedRegister
                    == resultInterval->compound->allocatedRegister) {
                pseudoMovMR->result.get()->replaceAllUses(pseudoMovMR->operand.get());

                fuseResultInterval(operandInterval, resultInterval);
                rewroteInstruction = true;
            }else{
                auto movMR = std::make_unique<MovMRInstruction>(pseudoMovMR->operand.get());
                auto movMRResult = movMR->result.setNew<ModeMValue>();
                setRegister(movMRResult, resultInterval->compound->allocatedRegister);

                pseudoMovMR->operand = nullptr;
                pseudoMovMR->result.get()->replaceAllUses(movMRResult);

                reassociateResultInterval(resultInterval, movMRResult);
                bb->insertInstruction(it, std::move(movMR));
                rewroteInstruction = true;
            }
        }

        // Fix the allocation for all results of the current instruction.
        for (auto [value, interval] : resultMap)
            setRegister(value, interval->compound->allocatedRegister);

        // Erase all intervals that end before the current instruction.
        // TODO: In principle, the loops to erase intervals can be accelerated by maintaining
        //       a tree of intervals, ordered by their finalPc. For now, we just use a linear scan.
        for (auto liveIt = liveMap.begin(); liveIt != liveMap.end(); ) {
            auto finalPc = liveIt->second->finalPc;
            if (finalPc == ProgramCounter{bb, inBlock, *it, beforeInstruction}) {
                liveIt = liveMap.erase(liveIt);
            } else {
                assert(finalPc.block == bb);
                assert(!(finalPc <= ProgramCounter{bb, inBlock, *it, afterInstruction}));
                ++liveIt;
            }
        }

        // Merge all intervals that originate from the previous PC.
        liveMap.merge(resultMap);
        assert(resultMap.empty());

        // Erase all intervals that end after the current instruction.
        for (auto liveIt = liveMap.begin(); liveIt != liveMap.end(); ) {
            auto finalPc = liveIt->second->finalPc;
            if (finalPc == ProgramCounter{bb, inBlock, *it, afterInstruction}) {
                liveIt = liveMap.erase(liveIt);
            } else {
                assert(finalPc.block == bb);
                assert(!(finalPc <= ProgramCounter{bb, inBlock, *it, afterInstruction}));
                ++liveIt;
            }
        }

        auto nextIt = it;
        ++nextIt;
        if (rewroteInstruction)
            bb->eraseInstruction(it);
        it = nextIt;
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
    }, {bb, afterBlock, nullptr, afterInstruction});

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
                auto moveResult = move->result.setNew<ModeMValue>();
                setRegister(moveResult, chainInterval->compound->allocatedRegister);
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
                auto firstResult = move->firstResult.setNew<ModeMValue>();
                auto secondResult = move->secondResult.setNew<ModeMValue>();
                setRegister(firstResult, nextRegister);
                setRegister(secondResult, chainInterval->compound->allocatedRegister);
                nextResult = secondResult;
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
