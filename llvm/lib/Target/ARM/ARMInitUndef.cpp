//===- ARMInitUndef.cpp - Initialize undef vector value to pseudo -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass runs to allow for Undef values to be given a definition in
// early-clobber instructions. This can occur when using higher levels of
// optimisations. Before, undef values were ignored and it would result in
// early-clobber instructions using the same registeres for the output as one of
// the inputs. This is an illegal operation according to the ARM Architecture
// Reference Manual. This pass will check for early-clobber instructions and
// give any undef values a defined Pseudo value to ensure that the early-clobber
// rules are followed.
//
// Example: Without this pass, for the vhcadd instruction the following would
// be generated: `vhcadd.s32 q0, q0, q0, #270`. This is an illegal instruction
// as the output register and 2nd input register cannot match. By using this
// pass, and using the Pseudo instruction, the following will be generated.
// `vhcadd.s32 q0, q1, q2, #270`. This is allowed, as the output register and qd
// input register are different.
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMSubtarget.h"
#include "llvm/ADT/bit.h"
#include "llvm/CodeGen/DetectDeadLanes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include <cstddef>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "arm-init-undef"
#define ARM_INIT_UNDEF_NAME "ARM init undef pass"

namespace {
class ARMInitUndef : public MachineFunctionPass {
  const TargetInstrInfo *TII;
  MachineRegisterInfo *MRI;
  const ARMSubtarget *ST;
  const TargetRegisterInfo *TRI;

  SmallSet<Register, 8> NewRegs;

public:
  static char ID;

  ARMInitUndef() : MachineFunctionPass(ID) {
    initializeARMInitUndefPass(*PassRegistry::getPassRegistry());
  }
  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return ARM_INIT_UNDEF_NAME; }

private:
  bool handleImplicitDef(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator &Inst);
  bool isVectorRegClass(const Register R);
  const TargetRegisterClass *
  getVRLargestSuperClass(const TargetRegisterClass *RC) const;
  bool processBasicBlock(MachineFunction &MF, MachineBasicBlock &MBB,
                         const DeadLaneDetector &DLD);
};
} // end anonymous namespace

char ARMInitUndef::ID = 0;
INITIALIZE_PASS(ARMInitUndef, DEBUG_TYPE, ARM_INIT_UNDEF_NAME, false, false)
char &llvm::ARMInitUndefPass = ARMInitUndef::ID;

static bool isEarlyClobberMI(MachineInstr &MI) {
  return llvm::any_of(MI.defs(), [](const MachineOperand &DefMO) {
    return DefMO.isReg() && DefMO.isEarlyClobber();
  });
}

static unsigned getUndefInitOpcode(unsigned RegClassID) {
  if (RegClassID == ARM::MQPRRegClass.getID())
    return ARM::PseudoARMInitUndef;

  llvm_unreachable("Unexpected register class.");
}

/* handleImplicitDef is used to apply the definition to any undef values. This
 * is only done for instructions that are early-clobber, and are not tied to
 * other instructions. It will cycle through every MachineBasicBlock iterator
 * that is an IMPLICIT_DEF then check for if it is early-clobber, not tied and
 * is used. If all these are true, then the value is applied. This is then
 * changed to be defined at ARM_INIT_UNDEF_PSEUDO
 */
bool ARMInitUndef::handleImplicitDef(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &Inst) {
  bool Changed = false;
  Register LastReg;

  while (Inst->getOpcode() == TargetOpcode::IMPLICIT_DEF &&
         Inst->getOperand(0).getReg() != LastReg) {

    bool HasOtherUse = false;
    SmallVector<MachineOperand *, 1> UseMOs;
    LastReg = Inst->getOperand(0).getReg();

    Register Reg = Inst->getOperand(0).getReg();
    if (!Reg.isVirtual())
      continue;

    for (MachineOperand &MO : MRI->reg_operands(Reg)) {
      LLVM_DEBUG(dbgs() << "Register: " << MO.getReg() << "\n");
      LLVM_DEBUG(dbgs() << "MO " << MO << " is EarlyClobber "
                        << isEarlyClobberMI(*MO.getParent()) << "\n");
      if (isEarlyClobberMI(*MO.getParent())) {
        LLVM_DEBUG(dbgs() << "MO " << &MO << " is use " << MO.isUse()
                          << " MO is tied " << MO.isTied() << "\n");
        if (MO.isUse() && !MO.isTied()) {
          LLVM_DEBUG(dbgs() << "MO " << MO << " added\n");
          UseMOs.push_back(&MO);
        } else
          HasOtherUse = true;
      }
    }
    LLVM_DEBUG(dbgs() << "UseMOs " << &UseMOs << " is empty " << UseMOs.empty()
                      << "\n");
    if (UseMOs.empty())
      continue;

    LLVM_DEBUG(
        dbgs() << "Emitting PseudoARMInitUndef for implicit vector register "
               << Reg << '\n'
               << '\n');

    const TargetRegisterClass *TargetRegClass =
        getVRLargestSuperClass(MRI->getRegClass(Reg));
    unsigned Opcode = getUndefInitOpcode(TargetRegClass->getID());

    Register NewDest = Reg;
    if (HasOtherUse) {
      NewDest = MRI->createVirtualRegister(TargetRegClass);
      NewRegs.insert(NewDest);
    }
    BuildMI(MBB, Inst, Inst->getDebugLoc(), TII->get(Opcode), NewDest);

    if (!HasOtherUse)
      Inst = MBB.erase(Inst);

    for (auto *MO : UseMOs) {
      MO->setReg(NewDest);
      MO->setIsUndef(false);
    }

    Changed = true;
  }
  return Changed;
}

bool ARMInitUndef::isVectorRegClass(Register R) {
  return ARM::MQPRRegClass.hasSubClassEq(MRI->getRegClass(R));
}

const TargetRegisterClass *
ARMInitUndef::getVRLargestSuperClass(const TargetRegisterClass *RC) const {
  if (ARM::MQPRRegClass.hasSubClassEq(RC))
    return &ARM::MQPRRegClass;
  return RC;
}

/* This will process a BasicBlock within the MachineFunction. This will take
 * each iterator and determine if there is an Implicit Def that needs dealing
 * with. This also deals with implicit def's that are tied to an operand, to
 * ensure that the correct undef values are given a definition.
 */
bool ARMInitUndef::processBasicBlock(MachineFunction &MF,
                                     MachineBasicBlock &MBB,
                                     const DeadLaneDetector &DLD) {
  bool Changed = false;

  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); I++) {
    MachineInstr &MI = *I;

    unsigned UseOpIdx;
    if (MI.getNumDefs() != 0 && MI.isRegTiedToUseOperand(0, &UseOpIdx)) {
      MachineOperand &UseMO = MI.getOperand(UseOpIdx);
      if (UseMO.getReg() == ARM::NoRegister) {
        const TargetRegisterClass *RC =
            TII->getRegClass(MI.getDesc(), UseOpIdx, TRI, MF);
        Register NewDest = MRI->createVirtualRegister(RC);

        NewRegs.insert(NewDest);
        BuildMI(MBB, I, I->getDebugLoc(), TII->get(TargetOpcode::IMPLICIT_DEF),
                NewDest);
        UseMO.setReg(NewDest);
        Changed = true;
      }
    }

    if (MI.isImplicitDef()) {
      auto DstReg = MI.getOperand(0).getReg();
      if (DstReg.isVirtual() && isVectorRegClass(DstReg))
        Changed |= handleImplicitDef(MBB, I);
    }
  }
  return Changed;
}

bool ARMInitUndef::runOnMachineFunction(MachineFunction &MF) {
  ST = &MF.getSubtarget<ARMSubtarget>();
  MRI = &MF.getRegInfo();
  TII = ST->getInstrInfo();
  TRI = MRI->getTargetRegisterInfo();

  bool Changed = false;
  DeadLaneDetector DLD(MRI, TRI);
  DLD.computeSubRegisterLaneBitInfo();

  for (MachineBasicBlock &BB : MF) {
    Changed |= processBasicBlock(MF, BB, DLD);
  }

  return Changed;
}

FunctionPass *llvm::createARMInitUndefPass() { return new ARMInitUndef(); }
