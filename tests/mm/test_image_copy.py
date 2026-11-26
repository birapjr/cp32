"""Check full-image contents and bound the work done while IRQs are masked."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(root / "tests"))
from test_idle_handoff import extract_function

image_bytes = int(re.search(r"#define CP32_APP_IMAGE_BYTES (\d+)U",
    (root / "src/kernel/app_process.h").read_text()).group(1))
copy = extract_function(root / "src/mm/process.c", "copy_application_image")
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "copy.c"
    source.write_text("#include <assert.h>\n#include <stdint.h>\n#include <string.h>\n"
                      f"#define CP32_APP_IMAGE_BYTES {image_bytes}\n#define CP32_IRAM_EXT\n"
                      + copy + r'''
struct area { uint32_t before; unsigned char image[CP32_APP_IMAGE_BYTES]; uint32_t after; };
int main(void) {
  struct area src = {0}, dst = {0};
  for (unsigned i = 0; i < sizeof(src.image); i++)
    src.image[i] = (unsigned char)((i * 43U) ^ (i >> 8));
  memset(&dst, 0xa5, sizeof(dst));
  copy_application_image(dst.image, src.image);
  assert(!memcmp(src.image, dst.image, sizeof(dst.image)));
  assert(dst.before == 0xa5a5a5a5 && dst.after == 0xa5a5a5a5);
}
''')
    exe = source.with_suffix("")
    subprocess.run(["cc", "-std=c99", "-O2", "-fsanitize=undefined",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

for elf in sorted((root / "src").glob("build*/cp32.elf")):
    asm = subprocess.check_output(["xtensa-esp32s3-elf-objdump", "-d",
                                   "--disassemble=copy_application_image", str(elf)], text=True)
    code = [(int(pc, 16), op.removesuffix(".n"), args) for pc, op, args in re.findall(
        r"^\s*([a-f0-9]+):\s+[a-f0-9]+\s+(\S+)\s*([^\n]*)$", asm, re.M)]
    assert code and not any(op.startswith("call") for _, op, _ in code), f"{elf}: image copy must be a leaf"
    branches = [(pc, args) for pc, op, args in code if op.startswith(("b", "j", "loop"))]
    assert len(branches) == 1, f"{elf}: expected one bounded copy loop"
    branch_pc, args = branches[0]
    start = int(re.search(r"\b([0-9a-f]+) <", args).group(1), 16)
    body = [op for pc, op, _ in code if start <= pc <= branch_pc]
    # Every word store moves four bytes. A simple branch-only body lets us
    # bound dynamic instructions without assuming a clock speed or SRAM latency.
    step = 4 * body.count("s32i")
    assert step and step == 4 * body.count("l32i") and image_bytes % step == 0
    instructions = len(body) * (image_bytes // step) + len(code) - len(body)
    assert instructions <= image_bytes, f"{elf}: {instructions} instructions per image"
    print(f"{elf.parent.name}: {instructions} instructions for {image_bytes}-byte image copy")
print("MM image copy: all bytes and adjacent guards pass")
