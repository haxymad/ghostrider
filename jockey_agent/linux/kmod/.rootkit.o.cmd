savedcmd_rootkit.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o rootkit.o @rootkit.mod  ; /usr/lib/modules/7.0.10-zen1-1-zen/build/tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --ibt --orc --retpoline --rethunk --sls --static-call --uaccess --prefix=16  --link  --module rootkit.o

rootkit.o: $(wildcard /usr/lib/modules/7.0.10-zen1-1-zen/build/tools/objtool/objtool)
