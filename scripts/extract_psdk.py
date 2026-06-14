#!/usr/bin/env python3
"""从 VS_SETUP.MSI 提取 PlatformSDK，修复 msitools msiextract 的 LongName:ShortName| 乱码文件名。
用法: python3 extract_psdk.py <VS_SETUP.MSI> <output_dir>
"""
import os, shutil, subprocess, sys
from pathlib import Path

msi = sys.argv[1]
out = Path(sys.argv[2])
tmp = Path("/tmp/th07_psdk_fix")

# 1. msiextract
if tmp.exists():
    shutil.rmtree(tmp)
tmp.mkdir(parents=True)
print(f"msiextract {msi} -> {tmp}")
subprocess.check_call(["msiextract", "-C", str(tmp), msi],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# 2. 后处理：rename 含 ":" 的目录/文件为正确长名
def fix_name(name):
    if ":" not in name:
        return name
    before, after = name.split(":", 1)
    if before == ".":
        # ".:common" -> "common"
        return after.split("|")[0] if "|" in after else after
    # "LongName:Short|LongName" -> "LongName"
    return before

changed = True
rounds = 0
while changed and rounds < 50:
    changed = False
    rounds += 1
    # 从深到浅 rename
    entries = sorted([p for p in tmp.rglob("*")], key=lambda p: -len(p.parts))
    for entry in entries:
        if not entry.exists():
            continue
        new_name = fix_name(entry.name)
        if new_name and new_name != entry.name:
            new_path = entry.parent / new_name
            if new_path == entry:
                continue
            if not new_path.exists():
                entry.rename(new_path)
                changed = True
            elif new_path.is_dir() and entry.is_dir():
                shutil.copytree(entry, new_path, dirs_exist_ok=True)
                shutil.rmtree(entry)
                changed = True
            elif new_path.is_file() and entry.is_file():
                # 保留先存在的
                entry.unlink()
                changed = True
    if changed:
        print(f"  round {rounds}: renamed entries")

# 3. 找 PlatformSDK
psdk = None
for p in tmp.rglob("PlatformSDK"):
    if p.is_dir():
        psdk = p
        break
if psdk is None:
    # 退化：找含 windows.h 的 SDK 根
    wh = list(tmp.rglob("windows.h")) + list(tmp.rglob("Windows.h"))
    print(f"PlatformSDK dir not found. windows.h files: {len(wh)}")
    for w in wh[:3]:
        print("  ", w)
    sys.exit(1)

print(f"Found PlatformSDK: {psdk}")
print(f"  windows.h: {list(psdk.rglob('windows.h'))[:1]}")
print(f"  include dir: {[p for p in psdk.iterdir() if p.is_dir()]}")

# 4. copytree 到 out（out 是 VC7 目录，PlatformSDK 放其下）
dst = out / "PlatformSDK"
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(psdk, dst)
print(f"Copied PlatformSDK -> {dst}")
print(f"  size: {sum(f.stat().st_size for f in dst.rglob('*') if f.is_file()) // 1024} KiB")
