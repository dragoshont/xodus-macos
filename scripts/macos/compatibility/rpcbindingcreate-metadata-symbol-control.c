#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static HANDLE process;
static DWORD64 base;

static void type_name(ULONG type, unsigned depth)
{
    WCHAR *name = NULL;
    DWORD tag = 0, next = 0, count = 0;
    if (!type || depth > 6) return;
    SymGetTypeInfo(process, base, type, TI_GET_SYMTAG, &tag);
    if (SymGetTypeInfo(process, base, type, TI_GET_SYMNAME, &name))
    {
        printf(" TYPE id=%lu tag=%lu name=%ls\n", type, tag, name);
        LocalFree(name);
    }
    else printf(" TYPE id=%lu tag=%lu\n", type, tag);
    if (SymGetTypeInfo(process, base, type, TI_GET_TYPEID, &next) && next != type)
        type_name(next, depth + 1);
    if (depth && tag != 11 && tag != 13) return;
    if (SymGetTypeInfo(process, base, type, TI_GET_CHILDRENCOUNT, &count) && count && count <= 128)
    {
        TI_FINDCHILDREN_PARAMS *children = calloc(1, sizeof(*children) + count * sizeof(ULONG));
        ULONG i;
        children->Count = count;
        if (SymGetTypeInfo(process, base, type, TI_FINDCHILDREN, children))
            for (i = 0; i < count; ++i)
            {
                ULONG child = children->ChildId[i], child_tag = 0, child_type = 0;
                DWORD64 address = 0;
                name = NULL;
                SymGetTypeInfo(process, base, child, TI_GET_SYMTAG, &child_tag);
                SymGetTypeInfo(process, base, child, TI_GET_TYPEID, &child_type);
                SymGetTypeInfo(process, base, child, TI_GET_ADDRESS, &address);
                SymGetTypeInfo(process, base, child, TI_GET_SYMNAME, &name);
                printf(" CHILD id=%lu tag=%lu type=%lu address=%llx name=%ls\n",
                       child, child_tag, child_type, address, name ? name : L"");
                if (child_tag == 5 && name && depth == 0 && !wcsncmp(name, L"get_", 4))
                    type_name(child_type, depth + 1);
                if (name) LocalFree(name);
                if (child_tag == 20) type_name(child_type, depth + 1);
            }
        free(children);
    }
}

static BOOL CALLBACK symbol(PSYMBOL_INFO info, ULONG size, void *context)
{
    char undecorated[2048];
    (void)size; (void)context;
    UnDecorateSymbolName(info->Name, undecorated, sizeof(undecorated), UNDNAME_COMPLETE);
    printf("SYMBOL rva=%llx tag=%lu type=%lu name=%s\n",
           info->Address - base, info->Tag, info->TypeIndex, undecorated);
    if (strstr(info->Name, "RoGetActivatableClassRegistration") || info->Tag == 11)
        type_name(info->TypeIndex, 0);
    return TRUE;
}

int main(int argc, char **argv)
{
    IMAGEHLP_MODULE64 module = {0};
    if (argc < 3) return 2;
    process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_EXACT_SYMBOLS);
    if (!SymInitialize(process, argv[2], FALSE)) return 2;
    base = SymLoadModuleEx(process, NULL, argv[1], "combase", 0x180000000, 0, NULL, 0);
    if (!base) { printf("SYMBOL_LOAD_ERROR=%lu\n", GetLastError()); return 2; }
    if (argc == 3)
    {
        SymEnumSymbols(process, base, "*ActivatableClass*", symbol, NULL);
        SymEnumTypesByName(process, base, "*Activatable*", symbol, NULL);
    }
    else
    {
        int i;
        for (i = 3; i < argc; ++i) SymEnumTypesByName(process, base, argv[i], symbol, NULL);
        SymEnumSymbols(process, base, "*RoGetActivatableClassRegistration*", symbol, NULL);
    }
    module.SizeOfStruct = sizeof(module);
    SymGetModuleInfo64(process, base, &module);
    printf("SYMBOL_KIND=%d PDB=%s\n", module.SymType, module.LoadedPdbName);
    SymCleanup(process);
    return 0;
}
