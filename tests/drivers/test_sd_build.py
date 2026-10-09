"""Check the generated image and optional target ELF storage/memory contract."""
from pathlib import Path
import shutil,subprocess,sys,struct,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools'))
from make_sd_image import wrap
from make_minix_demo import build_image
volume=build_image(True)
disk=wrap(volume)
assert disk[510:512]==b'\x55\xaa' and disk[450]==0x81
assert struct.unpack_from('<II',disk,454)==(2048,128)
assert disk[2048*512:]==volume
for bad in (b'',volume[:-1],bytes(len(volume))):
 try:wrap(bad)
 except ValueError:pass
 else:raise AssertionError('bad volume accepted')
# Reject a symlink output and leave its destination untouched.
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'volume').write_bytes(volume);(p/'keep').write_text('keep');(p/'link').symlink_to(p/'keep')
 r=subprocess.run([sys.executable,str(root/'tools/make_sd_image.py'),str(p/'volume'),str(p/'link')],capture_output=True)
 assert r.returncode and (p/'keep').read_text()=='keep'
nm=shutil.which('xtensa-esp32s3-elf-nm')
elf=root/'src/build/cp32.elf'
if nm and elf.exists():
 symbols={line.split()[-1]:int(line.split()[0],16) for line in subprocess.check_output([nm,'-n',str(elf)],text=True).splitlines() if len(line.split())==3}
 assert not any(n in symbols for n in ('ramdisk','cp32_demo_bytes','cp32_demo_runs'))
 assert symbols['_heap_end']-symbols['_heap_start']==192*1024
 assert symbols['_runtime_stack_end']<=0x3fce8000
 assert 0x40378000<=symbols['cp32_sd_init']<symbols['_iram_ext_end']
print('SD image/build: partition payload, invalid input, safe output and SRAM/ELF contract pass')
