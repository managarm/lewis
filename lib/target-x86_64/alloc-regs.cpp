
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <frg/interval_tree.hpp>
#include <lewis/target-x86_64/arch-ir.hpp>
#include <lewis/target-x86_64/arch-passes.hpp>

namespace lewis::targets::x86_64 {

struct LiveInterval {
    int allocatedRegister = -1;

    // Value that is allocated to the register.
    // TODO: For inter-basic-block allocation, multiple values can be associated
    //       with single interval. We should probably add a class 'LiveCompound'
    //       that subsumes multiple LiveIntervals.
    Value *associatedValue = nullptr;

    // Program counters of interval origin and final use.
    int originPc = -1;
    int finalPc = -1;

	// Intrusive tree that stores all intervals.
    frg::rbtree_hook rbHook;
    frg::interval_hook<int> intervalHook;
};

struct AllocateRegistersImpl : AllocateRegistersPass {
    AllocateRegistersImpl(BasicBlock *bb)
    : _bb{bb} { }

    void run() override;

private:
    void _linearizePcs();
    void _generateIntervals();
    int _determineFinalPc(int originPc, Value *v);
    void _establishAllocation();

    BasicBlock *_bb;

    int _nPcs = 1;
    std::unordered_map<Instruction *, int> _pcMap;

    // Stores all intervals that still need to be allocated.
    // TODO: Prioritize the intervals in some way, e.g. by use density.
    std::queue<LiveInterval *> _queue;

    // Stores all intervals that have already been allocated.
    frg::interval_tree<
        LiveInterval,
        int,
        &LiveInterval::originPc,
        &LiveInterval::finalPc,
        &LiveInterval::rbHook,
        &LiveInterval::intervalHook
    > _allocated;
};

void AllocateRegistersImpl::run() {
    _linearizePcs();
    _generateIntervals();

    // The following loop performs the actual allocation.
    while (!_queue.empty()) {
        auto interval = _queue.front();
        _queue.pop();

        // Determine a bitmask of registers that are already allocated.
        uint64_t registersBlocked = 0;
        _allocated.for_overlaps([&] (LiveInterval *overlap) {
            assert(overlap->allocatedRegister >= 0);
            registersBlocked |= 1 << overlap->allocatedRegister;
        }, interval->originPc, interval->finalPc);

        // Chose the first free register using the bitmask.
        // TODO: Currently, we just allocate to the first 4 registers: rax, rcx, rdx, rbx.
        //       Generalize this register model.
        for (int i = 0; i < 4; i++) {
            if (registersBlocked & (1 << i))
                continue;
            interval->allocatedRegister = i;
            break;
        }
        assert(interval->allocatedRegister >= 0
                && "Register spilling is not implemented yet");
        std::cout << "Allocating to register " << interval->allocatedRegister << std::endl;

        _allocated.insert(interval);
    }

    _establishAllocation();
}

// Maps instructions to monotonically increasing numbers.
void AllocateRegistersImpl::_linearizePcs() {
    for (auto inst : _bb->instructions()) {
        _pcMap.insert({inst, _nPcs});
        _nPcs++;
    }
}

// Called before allocation. Generates all LiveIntervals and adds them to the queue.
void AllocateRegistersImpl::_generateIntervals() {
    int currentPc = 1;
    for (auto inst : _bb->instructions()) {
        assert(_pcMap.at(inst) == currentPc);

        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            auto interval = new LiveInterval;
            interval->associatedValue = movMC->result();
            interval->originPc = currentPc;
            interval->finalPc = _determineFinalPc(currentPc, movMC->result());
            _queue.push(interval);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(inst);
                unaryMInPlace) {
            auto interval = new LiveInterval;
            interval->associatedValue = unaryMInPlace->result();
            interval->originPc = currentPc;
            interval->finalPc = _determineFinalPc(currentPc, unaryMInPlace->result());
            _queue.push(interval);
        } else {
            assert(!"Unexpected IR instruction");
        }

        currentPc++;
    }
}

int AllocateRegistersImpl::_determineFinalPc(int originPc, Value *v) {
    int finalPc = originPc;
    for (auto use : v->uses()) {
        auto usePc = _pcMap.at(use->instruction());
        if (usePc > finalPc)
            finalPc = usePc;
    }
    return finalPc;
}

// This is called *after* the actual allocation is done. It "implements" the allocation by
// fixing registers in the IR and generating necessary move instructions.
void AllocateRegistersImpl::_establishAllocation() {
    std::unordered_map<Value *, LiveInterval *> liveMap;
    std::unordered_map<Value *, LiveInterval *> resultMap;

    int currentPc = 1;
    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        assert(_pcMap.at(*it) == currentPc);

        // Find all intervals that originate from the current PC.
        _allocated.for_overlaps([&] (LiveInterval *interval) {
            if (interval->originPc != currentPc)
                return;
            resultMap.insert({interval->associatedValue, interval});
        }, currentPc, currentPc);

        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto resultInterval = resultMap.at(movMC->result());
            movMC->result()->modeRegister = resultInterval->allocatedRegister;
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto resultInterval = resultMap.at(unaryMInPlace->result());
            auto primaryInterval = liveMap.at(unaryMInPlace->primary.get());
            if(resultInterval->allocatedRegister != primaryInterval->allocatedRegister) {
                auto move = std::make_unique<MovMRInstruction>(unaryMInPlace->primary.get());
                move->result()->modeRegister = resultInterval->allocatedRegister;
                unaryMInPlace->primary = move->result();
                _bb->insertInstruction(it, std::move(move));
            }
            unaryMInPlace->result()->modeRegister = resultInterval->allocatedRegister;
        } else {
            assert(!"Unexpected IR instruction");
        }

        // Erase all intervals that died after the current PC.
        for (auto it = liveMap.begin(); it != liveMap.end(); ) {
            if (it->second->finalPc == currentPc) {
                it = liveMap.erase(it);
            }else{
                assert(it->second->finalPc > currentPc);
                ++it;
            }
        }

        currentPc++;

        // Merge all intervals that originate from the previous PC.
        liveMap.merge(resultMap);
        assert(resultMap.empty());
    }
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(BasicBlock *bb) {
    return std::make_unique<AllocateRegistersImpl>(bb);
}

} // namespace lewis::targets::x86_64
