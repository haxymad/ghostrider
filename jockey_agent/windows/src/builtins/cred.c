/*
 * cred.c — Credential harvesting builtins for Windows.
 */
#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <ntsecapi.h>
#include <stdlib.h>
#include <lm.h>
#include <stdlib.h>
#include <sddl.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "netapi32.lib")

static BuiltinResult bi_cred_users(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    LPUSER_INFO_0 info = NULL;
    DWORD entries = 0, total = 0, resume = 0;

    if (NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT,
                    (LPBYTE *)&info, MAX_PREFERRED_LENGTH,
                    &entries, &total, &resume) == NERR_Success) {
        for (DWORD i = 0; i < entries; i++) {
            char narrow[256];
            int n = WideCharToMultiByte(CP_UTF8, 0, info[i].usri0_name, -1,
                                        narrow, sizeof(narrow), NULL, NULL);
            if (n <= 0) narrow[0] = 0;
            Value item = mkdict();
            dict_set(item.v.dict, "name", mkstr(narrow));
            arr_push(arr.v.arr, item);
        }
        NetApiBufferFree(info);
    }

    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_sessions(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    PLUID sessions = NULL;
    ULONG count = 0;

    if (LsaEnumerateLogonSessions(&count, &sessions) == 0) {
        for (ULONG i = 0; i < count; i++) {
            LUID luid = sessions[i];
            Value item = mkdict();
            dict_set(item.v.dict, "luid_low",  mkint((int64_t)luid.LowPart));
            dict_set(item.v.dict, "luid_high", mkint((int64_t)luid.HighPart));
            arr_push(arr.v.arr, item);
        }
        LsaFreeReturnBuffer(sessions);
    }

    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_token(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_QUERY | TOKEN_QUERY_SOURCE, &tok)) {
        *out = mknone();
        return BUILTIN_OK;
    }

    Value d = mkdict();

    {
        DWORD sz = 0;
        GetTokenInformation(tok, TokenUser, NULL, 0, &sz);
        if (sz) {
            TOKEN_USER *tu = (TOKEN_USER *)malloc(sz);
            if (GetTokenInformation(tok, TokenUser, tu, sz, &sz)) {
                LPSTR sidstr = NULL;
                if (ConvertSidToStringSidA(tu->User.Sid, &sidstr)) {
                    dict_set(d.v.dict, "user_sid", mkstr(sidstr));
                    LocalFree(sidstr);
                }
            }
            free(tu);
        }
    }

    {
        TOKEN_ELEVATION te;
        DWORD sz = sizeof(te);
        if (GetTokenInformation(tok, TokenElevation, &te, sz, &sz)) {
            dict_set(d.v.dict, "elevated", mkint(te.TokenIsElevated ? 1 : 0));
        }
    }

    CloseHandle(tok);
    *out = d;
    return BUILTIN_OK;
}

const Builtin BUILTINS_CRED[] = {
    { "cred_users",    0, 0, bi_cred_users    },
    { "cred_sessions", 0, 0, bi_cred_sessions },
    { "cred_token",    0, 0, bi_cred_token    },
    { NULL, 0, 0, NULL },
};