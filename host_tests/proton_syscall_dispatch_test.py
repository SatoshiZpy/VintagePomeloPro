"""AArch64 execution regression; run in Linux Docker with cross GCC and qemu-user."""
import ast
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
tree=ast.parse((ROOT/'scripts/build_linux_proton_probe.py').read_text())
function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='proton_syscall_variant')
namespace={}
exec(compile(ast.Module(body=[function],type_ignores=[]),'<variant>','exec'),namespace)
original=(ROOT/'entry/src/main/cpp/linux_steam/gnu_fixture/syscall_compat.S').read_text()
# Reproduce the old insertion, which was shadowed by the earlier FEX branch.
broken=original.replace('    cmp x0, #__NR_openat2',
    '    cmp x0, #374\n    b.eq 2f\n    cmp x0, #282\n    b.eq 2f\n    cmp x0, #449\n    b.eq 2f\n    cmp x0, #__NR_openat2',1)
with tempfile.TemporaryDirectory(prefix='proton-syscall-') as temp:
    for name,assembly,expected in [('regression',broken,1),('fixed',namespace['proton_syscall_variant'](original),0)]:
        source=Path(temp)/(name+'.S');source.write_text(assembly);binary=Path(temp)/name
        subprocess.run(['aarch64-linux-gnu-gcc','-O2','-Wall','-Wextra','-Werror','-static',
            str(ROOT/'host_tests/proton_syscall_dispatch_test.c'),str(source),'-o',str(binary)],check=True)
        result=subprocess.run(['qemu-aarch64',str(binary)],capture_output=True,text=True)
        if result.returncode!=expected:raise RuntimeError(name+': exit='+str(result.returncode)+' '+result.stdout+result.stderr)
        print(('Confirmed original regression' if name=='regression' else result.stdout.strip()),flush=True)
