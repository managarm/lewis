
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
    void _collectBlockIntervals(BasicBlock *bb);
    void _collectPhiIntervals(BasicBlock *bb);
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
        _collectBlockIntervals(bb);
    for (auto bb : _fn->blocks())
        _collectPhiIntervals(bb);

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
void AllocateRegistersImpl::_collectBlockIntervals(BasicBlock *bb) {
    std::vector<LiveCompound *> collected;

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
            assert(interval->associatedValue);

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
            assert(resultInterval->associatedValue);

            collected.push_back(compound);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveSingleInstruction>(unaryMInPlace->primary.get()));
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
            assert(resultInterval->associatedValue);

            collected.push_back(compound);
        } else if (auto binaryMRInPlace = hierarchy_cast<BinaryMRInPlaceInstruction *>(*it);
                binaryMRInPlace) {
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveSingleInstruction>(binaryMRInPlace->primary.get()));
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
            assert(resultInterval->associatedValue);

            collected.push_back(compound);
        } else if (auto call = hierarchy_cast<CallInstruction *>(*it); call) {
            // Add a PseudoMove instruction for the operands.
            auto pseudoMove = bb->insertInstruction(it,
                    std::make_unique<PseudoMoveMultipleInstruction>(call->numOperands()));
            for (size_t i = 0; i < call->numOperands(); ++i) {
                pseudoMove->operand(i) = call->operand(i).get();
                auto pseudoMoveResult = pseudoMove->result(i).setNew<ModeMValue>();
                call->operand(i) = pseudoMoveResult;

                auto copyCompound = new LiveCompound;
                switch (i) {
                case 0: copyCompound->possibleRegisters = 0x80; break;
                case 1: copyCompound->possibleRegisters = 0x40; break;
                default: assert(!"TODO: Implement correct ABI for arbitrary arguments");
                }

                auto copyInterval = new LiveInterval;
                copyCompound->intervals.push_back(copyInterval);
                copyInterval->associatedValue = pseudoMoveResult;
                copyInterval->compound = copyCompound;
                copyInterval->originPc = ProgramCounter{bb, inBlock, pseudoMove, afterInstruction};

                collected.push_back(copyCompound);
            }

            // Add LiveIntervals for the results.
            auto resultCompound = new LiveCompound;
            resultCompound->possibleRegisters = 0x1;

            auto resultInterval = new LiveInterval;
            resultCompound->intervals.push_back(resultInterval);
            resultInterval->associatedValue = call->result.get();
            resultInterval->compound = resultCompound;
            resultInterval->originPc = ProgramCounter{bb, inBlock, *it, afterInstruction};
            assert(resultInterval->associatedValue);

            collected.push_back(resultCompound);
        } else {
            std::cout << "lewis: Unknown instruction kind " << (*it)->kind << std::endl;
            assert(!"Unexpected IR instruction");
        }
    }

    // Generate a PseudoMove instruction for data-flow PhiNodes.
    std::vector<DataFlowEdge *> edges; // Need to index edges.
    for (auto edge : bb->source.edges())
        edges.push_back(edge);

    if (!edges.empty()) {
        auto pseudoMove = bb->insertInstruction(
                std::make_unique<PseudoMoveMultipleInstruction>(edges.size()));
        for (size_t i = 0; i < edges.size(); i++) {
            pseudoMove->operand(i) = edges[i]->alias.get();
            edges[i]->alias = pseudoMove->result(i).setNew<ModeMValue>();
        }
    }

    // Post-process the generated intervals.
    for (auto compound : collected) {
        for (auto interval : compound->intervals) {
            // Compute the final PC for each interval.
            assert(interval->associatedValue);
            auto maybeFinalPc = _determineFinalPc(bb, interval->associatedValue);
            interval->finalPc = maybeFinalPc.value_or(interval->originPc);
        }

        _queue.push(compound);
    }
}

void AllocateRegistersImpl::_collectPhiIntervals(BasicBlock *bb) {
    // Generate LiveIntervals for PhiNodes.
    for (auto phi : bb->phis()) {
        if (auto argument = hierarchy_cast<ArgumentPhi *>(phi); argument) {
            auto compound = new LiveCompound;
            compound->possibleRegisters = 0x80;

            auto nodeInterval = new LiveInterval;
            compound->intervals.push_back(nodeInterval);
            nodeInterval->associatedValue = phi->value.get();
            nodeInterval->compound = compound;
            nodeInterval->originPc = {bb, beforeBlock, nullptr, afterInstruction};
            assert(nodeInterval->associatedValue);
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
            assert(nodeInterval->associatedValue);
            auto maybeFinalPc = _determineFinalPc(bb, phi->value.get());
            nodeInterval->finalPc = maybeFinalPc.value_or(nodeInterval->originPc);

            // By construction, we only see values from the PseudoMove instruction
            // that was generated in _collectBlockIntervals().
            for (auto edge : dataFlow->sink.edges()) {
                auto sourceInterval = new LiveInterval;
                assert(edge->alias);
                compound->intervals.push_back(sourceInterval);
                sourceInterval->associatedValue = edge->alias.get();
                sourceInterval->compound = compound;
                assert(sourceInterval->associatedValue);
                sourceInterval->originPc = ProgramCounter{edge->source()->block(), inBlock,
                        edge->alias.get()->origin()->instruction(), afterInstruction};
                sourceInterval->finalPc = ProgramCounter{edge->source()->block(), afterBlock,
                        nullptr, afterInstruction};
            }

            _queue.push(compound);

        } else {
            assert(!"Unexpected IR phi");
        }
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
        // Determine the current register allocation.
        LiveInterval *currentState[16] = {};

        for (auto entry : liveMap) {
            auto interval = entry.second;
            auto currentRegister = interval->compound->allocatedRegister;
            assert(currentRegister >= 0);
            assert(!currentState[currentRegister]);
            currentState[currentRegister] = interval;
            std::cout << "    Current state[" << currentRegister << "]: "
                    << interval->associatedValue << std::endl;
        }

        // Find all intervals that originate from the current PC.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc.instruction != *it)
                return;
            std::cout << "        Instruction returns " << interval->associatedValue << std::endl;
            resultMap.insert({interval->associatedValue, interval});
        }, {bb, inBlock, *it, afterInstruction});

        // Helper function to fuse a result interval (from resultMap)
        // into a live interval (from liveMap).
        auto fuseResultInterval = [&] (LiveInterval *interval, LiveInterval *into) {
            auto resIt = resultMap.find(interval->associatedValue);
            assert(resIt != resultMap.end());
            resultMap.erase(resIt);
            _allocated.remove(interval);

            assert((into->finalPc == ProgramCounter{bb, inBlock, *it, beforeInstruction}));
            _allocated.remove(into);
            into->finalPc = interval->finalPc;
            _allocated.insert(into);
        };

        // Helper function to rewrite the associatedValue of a result interval (from resultMap).
        auto reassociateResultInterval = [&] (LiveInterval *interval, Value *newValue) {
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
                auto movMRResult = movMR->result.setNew<ModeMValue>();
                setRegister(movMRResult, resultInterval->compound->allocatedRegister);

                pseudoMoveSingle->operand = nullptr;
                pseudoMoveSingle->result.get()->replaceAllUses(movMRResult);

                reassociateResultInterval(resultInterval, movMRResult);
                bb->insertInstruction(it, std::move(movMR));
            }
            rewroteInstruction = true;
        } else if (auto pseudoMoveMultiple = hierarchy_cast<PseudoMoveMultipleInstruction *>(*it);
                pseudoMoveMultiple) {
            // The following code minimizes the number of move instructions.
            // This is done as follows:
            // - The code constructs "move chains", i.e., chains of registers that need to be moved.
            //   For example, such a chain could be rax -> rcx -> rdx.
            // - If the chain is acyclic, the last move is emitted first as
            //   the contents of that register are not used anymore.
            //   Afterwards, the other moves are done in reverse order.
            // - If a chain forms a cycle, an additional move is necessary.
            std::cout << "        Rewriting pseudoMoveMultiple" << std::endl;

            // Determine the head of each move chain.
            struct MoveChain {
                LiveInterval *lastInChain;
                bool isCyclic = false;
            };

            std::vector<MoveChain> chainList;

            for (size_t i = 0; i < pseudoMoveMultiple->arity(); ++i) {
                auto operandInterval = liveMap.at(pseudoMoveMultiple->operand(i).get());
                auto resultInterval = resultMap.at(pseudoMoveMultiple->result(i).get());
                std::cout << "            Expanding move chain at "
                        << resultInterval->associatedValue << std::endl;

                // Special case self-loops in move chains (no move is necessary).
                auto resultRegister = resultInterval->compound->allocatedRegister;
                assert(resultRegister >= 0);
                if (operandInterval->compound->allocatedRegister == resultRegister) {
                    fuseResultInterval(resultInterval, operandInterval);
                    continue;
                }

                // Follow the move chain starting at the current entry until it's end.
                auto chainInterval = resultInterval;
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
                    std::cout << "                Move chain: " << srcInterval->associatedValue
                            << " from " << chainRegister
                            << " to " << destInterval->compound->allocatedRegister << std::endl;
                    if (destInterval == resultInterval) {
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
                        auto move = std::make_unique<MovMRInstruction>
                                (chainInterval->associatedValue);
                        auto moveResult = move->result.setNew<ModeMValue>();
                        setRegister(moveResult, chainInterval->compound->allocatedRegister);
                        bb->insertInstruction(it, std::move(move));

                        // TODO: Reassociate intervals here.
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
                        bb->insertInstruction(it, std::move(move));

                        // TODO: Reassociate intervals here.
                        chainInterval = chainInterval->previousMoveInChain;
                    } while (chainInterval);
                }
            }
            rewroteInstruction = true;
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
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(Function *fn) {
    return std::make_unique<AllocateRegistersImpl>(fn);
}

} // namespace lewis::targets::x86_64
