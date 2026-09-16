savedcmd_rootkit.mod := printf '%s\n'   main.o hooks.o cred_escalate.o ioctl_handler.o | awk '!x[$$0]++ { print("./"$$0) }' > rootkit.mod
