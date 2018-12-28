
#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
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
    int deathPc = -1;
};

bool doIntersect(LiveInterval *x, LiveInterval *y) {
    return (y->originPc >= x->originPc && y->originPc < x->deathPc)
            || (x->originPc >= y->originPc && x->originPc < y->deathPc);
}

struct AllocateRegistersImpl : AllocateRegistersPass {
    AllocateRegistersImpl(BasicBlock *bb)
    : _bb{bb} { }

    void run() override;

private:
    void _linearizePcs();
    void _generateIntervals();
    int _determineDeathPc(Value *v);
    void _establishAllocation();

    BasicBlock *_bb;

    int _nPcs = 1;
    std::unordered_map<Instruction *, int> _pcMap;

    // Stores all intervals that still need to be allocated.
    // TODO: Prioritize the intervals in some way, e.g. by use density.
    std::queue<LiveInterval *> _queue;

    // Stores all intervals that have already been allocated.
    // TODO: Use an interval tree instead.
    std::vector<LiveInterval *> _allocated;
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
        for (auto overlap : _allocated) {
            if (!doIntersect(interval, overlap))
                continue;
            assert(overlap->allocatedRegister >= 0);
            registersBlocked |= 1 << overlap->allocatedRegister;
        }

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

        _allocated.push_back(interval);
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
    for (auto inst : _bb->instructions()) {
        if (auto movMC = hierarchy_cast<MovMCInstruction *>(inst); movMC) {
            auto interval = new LiveInterval;
            interval->associatedValue = movMC->result();
            interval->originPc = _pcMap.at(inst);
            interval->deathPc = _determineDeathPc(movMC->result());
            _queue.push(interval);
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(inst);
                unaryMInPlace) {
            auto interval = new LiveInterval;
            interval->associatedValue = unaryMInPlace->result();
            interval->originPc = _pcMap.at(inst);
            interval->deathPc = _determineDeathPc(unaryMInPlace->result());
            _queue.push(interval);
        } else {
            assert(!"Unexpected IR instruction");
        }
    }
}

int AllocateRegistersImpl::_determineDeathPc(Value *v) {
    int deathPc = -1;
    for (auto use : v->uses()) {
        auto usePc = _pcMap.at(use->instruction());
        if (usePc + 1 > deathPc)
            deathPc = usePc + 1;
    }
    return deathPc;
}

// This is called *after* the actual allocation is done. It "implements" the allocation by
// fixing registers in the IR and generating necessary move instructions.
void AllocateRegistersImpl::_establishAllocation() {
    std::unordered_map<Value *, LiveInterval *> liveMap;
    std::unordered_map<Value *, LiveInterval *> bornMap;

    int currentPc = 1;
    for (auto it = _bb->instructions().begin(); it != _bb->instructions().end(); ++it) {
        assert(_pcMap.at(*it) == currentPc);

        // Find all intervals that originate from the current PC.
        for (auto interval : _allocated)
            if (interval->originPc == currentPc)
                bornMap.insert({interval->associatedValue, interval});

        if (auto movMC = hierarchy_cast<MovMCInstruction *>(*it); movMC) {
            auto resultInterval = bornMap.at(movMC->result());
            movMC->result()->modeRegister = resultInterval->allocatedRegister;
        } else if (auto unaryMInPlace = hierarchy_cast<UnaryMInPlaceInstruction *>(*it);
                unaryMInPlace) {
            auto resultInterval = bornMap.at(unaryMInPlace->result());
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

        // Erase all intervals that died after the previous PC.
        currentPc++;
        for (auto it = liveMap.begin(); it != liveMap.end(); ) {
            if (it->second->deathPc == -1 || it->second->deathPc == currentPc) {
                it = liveMap.erase(it);
            }else{
                assert(it->second->deathPc > currentPc);
                ++it;
            }
        }

        // Merge all intervals that originate from the previous PC.
        liveMap.merge(bornMap);
        assert(bornMap.empty());
    }
}

std::unique_ptr<AllocateRegistersPass> AllocateRegistersPass::create(BasicBlock *bb) {
    return std::make_unique<AllocateRegistersImpl>(bb);
}

} // namespace lewis::targets::x86_64
