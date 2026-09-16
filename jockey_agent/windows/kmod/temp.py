#!/usr/bin/env python3
"""final.py - disable Inf2Cat + TestSign; copy jockey.sys next to the INF."""
import os, re, shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
os.chdir(HERE)

proj = HERE / "jockey.vcxproj"
shutil.copy2(proj, proj.with_suffix(".vcxproj.bak4"))
t = proj.read_text(encoding="utf-8", errors="replace")

# Remove any earlier SignMode insertions in wrong groups
t = re.sub(r"\s*<SignMode>[^<]*</SignMode>", "", t)
t = re.sub(r"\s*<EnableTestSigning>[^<]*</EnableTestSigning>", "", t)
t = re.sub(r"\s*<EnableInf2cat>[^<]*</EnableInf2cat>", "", t)

# Insert into the Label="Configuration" PropertyGroup
def fix(m):
    body = m.group(1)
    body = body.replace(
        "<ConfigurationType>Driver</ConfigurationType>",
        "<ConfigurationType>Driver</ConfigurationType>\n"
        "    <SignMode>Off</SignMode>\n"
        "    <EnableTestSign>false</EnableTestSign>\n"
        "    <EnableInf2cat>false</EnableInf2cat>")
    return '<PropertyGroup Label="Configuration">' + body + '</PropertyGroup>'

t, n = re.subn(
    r'<PropertyGroup\s+Label="Configuration">(.*?)</PropertyGroup>',
    fix, t, flags=re.DOTALL)
print(f"[+] patched {n} Configuration block(s)")

proj.write_text(t, encoding="utf-8")
print(f"[+] backup: {proj.with_suffix('.vcxproj.bak4')}")