#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef HRESULT (WINAPI *GET_LANGUAGES)(PCWSTR, SIZE_T *, WCHAR *);
typedef HRESULT (WINAPI *GET_DIRECTION)(PCWSTR, DWORD *);
static unsigned int checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

int main(void)
{
    HMODULE module = LoadLibraryW(L"Y:\\xodus-app-tooling\\cuphead\\decoded-attempt1\\bcp47mrm.dll");
    GET_LANGUAGES get_languages;
    GET_DIRECTION get_direction;
    struct { WCHAR before, value, after[3]; } small = {0x7777, 0x7777, {0x7777, 0x7777, 0x7777}};
    WCHAR *buffer, *other, *expected, *multi, locale[LOCALE_NAME_MAX_LENGTH], *cursor, *next;
    ULONG count = 0, chars = 0;
    SIZE_T length = 1, required;
    DWORD direction = ~0u;
    BOOL api_ok;
    WCHAR provider[MAX_PATH];

    CHECK(module != NULL);
    if (!module) return 1;
    CHECK(GetModuleFileNameW(module, provider, MAX_PATH) != 0 &&
          !lstrcmpiW(provider, L"Y:\\xodus-app-tooling\\cuphead\\decoded-attempt1\\bcp47mrm.dll"));
    printf("BCP47_PROVIDER=%ls\n", provider);
    get_languages = (GET_LANGUAGES)(ULONG_PTR)GetProcAddress(module, "GetApplicationLanguagesWithUserLanguagesFallback");
    get_direction = (GET_DIRECTION)(ULONG_PTR)GetProcAddress(module, "GetApplicationLayoutDirection");
    CHECK(get_languages && get_direction);
    if (!get_languages || !get_direction) return 1;
    CHECK(get_languages(NULL, NULL, &small.value) == E_POINTER);
    CHECK(get_languages(NULL, &length, &small.value) == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) && length > 1);
    CHECK(small.before == 0x7777 && small.value == 0x7777 && small.after[0] == 0x7777 &&
          small.after[1] == 0x7777 && small.after[2] == 0x7777);
    required = length;
    buffer = calloc(required + 1, sizeof(*buffer));
    other = calloc(required + 1, sizeof(*other));
    CHECK(buffer && other);
    if (!buffer || !other) { free(buffer); free(other); return 1; }
    buffer[required] = 0x7777;
    CHECK(get_languages(NULL, &length, buffer) == S_OK && length == required);
    CHECK(buffer[0] && wcslen(buffer) + 1 == required && buffer[required] == 0x7777);
    printf("BCP47_USER_LANGUAGES=%ls LENGTH=%llu\n", buffer, (unsigned long long)length);
    length = required;
    CHECK(get_languages(L"Contoso.Unknown_1.0.0.0_x64__0123456789abc", &length, other) == S_OK &&
          length == required && !wcscmp(buffer, other));
    length = 0;
    CHECK(get_languages(L"en-US;fr-FR", &length, NULL) == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
          length == required);
    length = required;
    CHECK(get_languages(L"en-US;fr-FR", &length, other) == S_OK && !wcscmp(buffer, other));
    api_ok = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &chars);
    CHECK(api_ok);
    multi = calloc(chars ? chars : 2, sizeof(*multi));
    CHECK(multi != NULL);
    if (!multi) return 1;
    if (count && chars >= 2)
    {
        api_ok = GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, multi, &chars);
        CHECK(api_ok);
        if (!api_ok) return 1;
    }
    if (!multi[0])
    {
        CHECK(GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) != 0 && locale[0]);
        expected = locale;
    }
    else
    {
        for (cursor = multi; *cursor; cursor = next)
        {
            next = cursor + wcslen(cursor) + 1;
            if (*next) next[-1] = ';';
        }
        expected = multi;
    }
    CHECK(!wcscmp(buffer, expected));
    CHECK(get_direction(NULL, NULL) == E_POINTER);
    CHECK(get_direction(NULL, &direction) == S_OK && direction <= 1);
    CHECK(get_direction(L"en-US;ar-SA", &direction) == S_OK && direction == 0);
    CHECK(get_direction(L"ar-SA;en-US", &direction) == S_OK && direction == 1);
    CHECK(get_direction(L"HE-il", &direction) == S_OK && direction == 1);
    CHECK(get_direction(L"fa-IR", &direction) == S_OK && direction == 1);
    CHECK(get_direction(L"ur-PK", &direction) == S_OK && direction == 1);
    CHECK(get_direction(L"arbitrary", &direction) == S_OK && direction == 0);
    free(multi);
    free(other);
    free(buffer);
    FreeLibrary(module);
    printf("BCP47_LANGUAGE_CHECKS=%u FAILURES=%u\n", checks, failures);
    return !!failures;
}
