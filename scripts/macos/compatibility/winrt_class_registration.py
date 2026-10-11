"""Resolve WinRT ActivatableClassId DllPath values for a bottle.

System classes must point at the single system32 provider, as on genuine
Windows; a package-local copy of the same DLL loads as a second module
instance with its own singletons (observed for Cuphead's MrmCoreR.dll).
"""
from pathlib import Path, PureWindowsPath

ACTIVATABLE_CLASS_ROOT = "HKLM\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\"
SYSTEM32 = PureWindowsPath("C:\\windows\\system32")
SYSTEM_CLASS_PROVIDERS = {
    "Windows.ApplicationModel.Resources.Core.ResourceContext": "mrmcorer.dll",
    "Windows.ApplicationModel.Resources.Core.ResourceManager": "mrmcorer.dll",
}


class RegistrationError(RuntimeError):
    pass


def _system32_file(drive_c, name):
    directory = Path(drive_c) / "windows" / "system32"
    if directory.is_dir():
        for entry in directory.iterdir():
            if entry.name.lower() == name.lower() and entry.is_file():
                return entry
    return None


def registration_plan(drive_c, package_dir, classes):
    """Return [(registry key, DllPath)] for class name -> package DLL name.

    Classes with a known system provider resolve to system32 and require that
    provider in the bottle; others resolve inside the package directory.
    """
    plan = []
    for name, library in classes.items():
        provider = SYSTEM_CLASS_PROVIDERS.get(name)
        if provider is None:
            path = str(PureWindowsPath(package_dir) / library)
        else:
            if PureWindowsPath(library).name.lower() != provider:
                raise RegistrationError(name + " is served by " + provider + ", not " + library)
            if _system32_file(drive_c, provider) is None:
                raise RegistrationError("system32 provider " + provider + " is absent for " + name)
            path = str(SYSTEM32 / provider)
        plan.append((ACTIVATABLE_CLASS_ROOT + name, path))
    return plan
