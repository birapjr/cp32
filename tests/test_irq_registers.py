#!/usr/bin/env python3
"""Execute the actual level-1 assembly over sentinel register/frame state.

This deliberately models only the instructions on these paths, not Xtensa
timing or peripheral behavior. C dispatch is mocked at the ABI boundary.
RFE semantics follow Cadence's Xtensa ISA Summary, section 8.3.258; RFI 1
is undefined (8.3.259), and SR192 is DEPC, not a saved PS register.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/kernel/irq.S").read_text()
CONSTANTS = {
    name: int(value, 0)
    for name, value in re.findall(
        r"^#define\s+(CP32_\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$",
        (ROOT / "src/kernel/irq_const.h").read_text(), re.M)
}


class Machine:
    def __init__(self):
        code = re.sub(r"/\*.*?\*/", "", SOURCE, flags=re.S)
        code = re.sub(r"\.macro\b.*?\.endm", "", code, flags=re.S)
        self.ops, self.labels = [], {}
        for line in code.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if ":" in line:
                label, line = line.split(":", 1)
                self.labels.setdefault(label.strip(), []).append(len(self.ops))
                line = line.strip()
            if line and not line.startswith("."):
                self.ops.append(re.split(r"[\s,]+", line))
        self.reg = [0xA0000000 + i * 0x010101 for i in range(16)]
        self.reg[1] = 0x3FCFF000
        self.original = self.reg.copy()
        self.sr = {"ps": 0x110, "epc1": 0x40371234,
                   "exccause": 4, "interrupt": 4, "intenable": 4}
        self.mem = {}
        self.symbols = {"_stack_top": 0x3FCFF000}
        self.pc = 0
        self.return_pc = None
        self.dispatch = None
        self.calls = []
        self.stores = []

    def value(self, token):
        if token.startswith("-"):
            return -self.value(token[1:])
        if re.fullmatch(r"a\d+", token):
            return self.reg[int(token[1:])]
        if token in CONSTANTS:
            return CONSTANTS[token]
        try:
            return int(token, 0)
        except ValueError:
            if token not in self.symbols:
                self.symbols[token] = 0x3FC80000 + len(self.symbols) * 0x100
            return self.symbols[token]

    def put(self, name, value):
        self.reg[int(name[1:])] = value & 0xFFFFFFFF

    def global_set(self, name, value):
        self.mem[self.value(name)] = value

    def global_get(self, name):
        return self.mem[self.value(name)]

    def frame_write(self, address, regs, pc, ps):
        for i, value in enumerate([*regs, pc, ps, regs[1]]):
            self.mem[address + 4 * i] = value

    def frame_read(self, address):
        return [self.mem[address + 4 * i] for i in range(19)]

    def jump(self, label):
        if re.fullmatch(r"\d+[fb]", label):
            choices = self.labels[label[:-1]]
            self.pc = (min(i for i in choices if i >= self.pc)
                       if label[-1] == "f" else
                       max(i for i in choices if i < self.pc))
        else:
            self.pc = self.labels[label][0]

    def run(self, label):
        self.jump(label)
        for _ in range(1000):
            op, *args = self.ops[self.pc]
            self.pc += 1
            if op in ("movi", "mov"):
                self.put(args[0], self.value(args[1]))
            elif op in ("addi", "or"):
                x, y = map(self.value, args[1:])
                self.put(args[0], x + y if op == "addi" else x | y)
            elif op == "l32i":
                self.put(args[0], self.mem[self.value(args[1]) + self.value(args[2])])
            elif op == "s32i":
                address = self.value(args[1]) + self.value(args[2])
                self.mem[address] = self.value(args[0])
                self.stores.append(address)
            elif op == "rsr":
                self.put(args[0], self.sr[args[1].lower()])
            elif op == "wsr":
                self.sr[args[1].lower()] = self.value(args[0])
                if args[1].lower() == "ps":
                    assert self.sr["ps"] & 0x10, "EXCM cleared during partial restore"
            elif op == "xsr":
                old = self.sr[args[1].lower()]
                self.sr[args[1].lower()] = self.value(args[0])
                self.put(args[0], old)
            elif op == "j":
                self.jump(args[0])
            elif op in ("beq", "bne", "beqi", "bnei", "beqz", "bnez"):
                x = self.value(args[0])
                y = 0 if op.endswith("z") else self.value(args[1])
                if (x == y) == op.startswith("beq"):
                    self.jump(args[-1])
            elif op == "call0":
                self.calls.append(args[0])
                assert self.reg[1] % 16 == 0, "unaligned C stack"
                assert self.dispatch is not None, "unexpected C call"
                self.dispatch(self, args[0])
            elif op == "rsync":
                pass
            elif op == "rfe":
                self.sr["ps"] &= ~0x10
                self.return_pc = self.sr["epc1"]
                return
            else:
                raise AssertionError(f"unsupported instruction {op} {args}")
        raise AssertionError("assembly did not return")


class IRQRegisters(unittest.TestCase):
    def prepare(self):
        machine = Machine()
        machine.global_set("proc_ptr", 0x3FC81000)
        machine.global_set("cp32_user_probe_mode", 1)
        machine.global_set("k_reenter", 0)
        machine.global_set("cp32_user_dispatch_blocked", 0)
        machine.global_set("cp32_user_rfe_count", 0)
        return machine

    def test_irq_owner_and_handoff_from_both_vectors(self):
        for entry in ("irq_kernel", "irq_user"):
            for handoff in (False, True):
                with self.subTest(entry=entry, handoff=handoff):
                    m = self.prepare()
                    owner = m.global_get("proc_ptr")
                    original_ps = 0x130 if entry == "irq_user" else 0x110
                    m.sr["ps"] = original_ps
                    original_pc = m.sr["epc1"]
                    m.global_set("cp32_context_handoff_gate", int(handoff))
                    selected = 0x3FC82000
                    regs = [0xB0000000 + i * 0x030303 for i in range(16)]
                    regs[1] = 0x3FCED000
                    # Preserve a nonzero INTLEVEL in the chosen frame; the
                    # decoy proc_ptr has a conflicting status and stack.
                    selected_pc, selected_ps = 0x40374567, 0x103
                    m.frame_write(selected, regs, selected_pc, selected_ps)

                    def dispatch(cpu, name):
                        self.assertEqual(name, "cp32_irq_dispatch")
                        self.assertEqual(cpu.reg[3], 4)
                        self.assertEqual(cpu.global_get("k_reenter"), 1)
                        self.assertEqual(cpu.frame_read(owner),
                                         [*cpu.original, original_pc, original_ps,
                                          cpu.original[1]])
                        self.assertEqual([cpu.mem[cpu.reg[2] + i * 4]
                                          for i in range(16)], cpu.original)
                        self.assertFalse(any(0x60000000 <= a < 0x60100000
                                             for a in cpu.stores),
                                         "device acknowledged before pending dispatch")
                        cpu.global_set("proc_ptr", 0x3FC83000)
                        cpu.frame_write(0x3FC83000, [0xDEADBEEF] * 16,
                                        0x40370000, 15)
                        cpu.global_set("cp32_irq_return_proc", selected)
                        # call0 caller-saved registers may all be destroyed.
                        for i in range(12):
                            if i != 1:
                                cpu.reg[i] = 0xCCCCCCCC

                    m.dispatch = dispatch
                    m.run(entry)
                    self.assertEqual(m.reg, regs if handoff else m.original)
                    self.assertEqual(m.return_pc, selected_pc if handoff else original_pc)
                    self.assertEqual(m.sr["ps"],
                                     selected_ps if handoff else original_ps & ~0x10)
                    self.assertEqual(m.global_get("k_reenter"), 0)
                    self.assertEqual(m.sr["intenable"], 4)
                    self.assertNotIn("192", m.sr)

    def test_syscall_preserves_arguments_and_result(self):
        for entry in ("irq_kernel", "irq_user"):
            with self.subTest(entry=entry):
                m = self.prepare()
                m.sr["exccause"] = 0
                owner = m.global_get("proc_ptr")
                original_pc, original_ps = m.sr["epc1"], m.sr["ps"]

                def dispatch(cpu, name):
                    self.assertEqual(name, "cp32_user_trap_dispatch")
                    self.assertEqual(cpu.reg[2], owner)
                    self.assertEqual(cpu.reg[4], 0)
                    self.assertEqual(cpu.frame_read(cpu.reg[3]),
                                     [*cpu.original, original_pc, original_ps,
                                      cpu.original[1]])
                    regs = cpu.original.copy()
                    regs[2] = 0x1234
                    cpu.frame_write(owner, regs, original_pc + 3, original_ps)
                    cpu.global_set("cp32_irq_return_proc", owner)
                    cpu.reg[2] = 0

                m.dispatch = dispatch
                m.run(entry)
                expected = m.original.copy()
                expected[2] = 0x1234
                self.assertEqual(m.reg, expected)
                self.assertEqual(m.return_pc, original_pc + 3)
                self.assertEqual(m.sr["ps"], original_ps & ~0x10)
                self.assertEqual(m.global_get("cp32_user_rfe_count"), 1)

    def test_initial_entry_restores_every_general_register(self):
        m = self.prepare()
        regs = m.original.copy()
        target = 0x3FC84000
        m.frame_write(target, regs, 0x4037BEEF, 0x100)
        m.reg[2] = target
        m.run("cp32_enter_initial_user")
        self.assertEqual(m.reg, regs)
        self.assertEqual(m.return_pc, 0x4037BEEF)
        self.assertEqual(m.sr["ps"], 0x100)


if __name__ == "__main__":
    unittest.main()
