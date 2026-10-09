using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

// Read-only metadata/IL inspection. Does not activate Store classes or invoke
// any method from the Microsoft installer.
public static class PackageManagerStoreInspect
{
    private const string StoreNamespace = "Windows.ApplicationModel.Store.Preview.InstallControl";
    private static IEnumerable<Type> Types(Assembly assembly)
    {
        try { return assembly.GetTypes(); }
        catch (ReflectionTypeLoadException error) { return error.Types.Where(type => type != null); }
    }

    private static void Metadata()
    {
        Type manager = Type.GetType(StoreNamespace + ".AppInstallManager, Windows.ApplicationModel, ContentType=WindowsRuntime", true);
        Console.WriteLine("GENUINE_METADATA_CLASS=" + manager.AssemblyQualifiedName);
        foreach (Type contract in Types(manager.Assembly)
            .Where(type => type.IsInterface &&
                (type.FullName.StartsWith(StoreNamespace + ".IAppInstallManager", StringComparison.Ordinal) ||
                 type.FullName.StartsWith(StoreNamespace + ".IAppInstallOptions", StringComparison.Ordinal)))
            .OrderBy(type => type.FullName))
        {
            Console.WriteLine("INTERFACE=" + contract.FullName + " IID=" + contract.GUID.ToString("D"));
            int slot = 6; // IUnknown + IInspectable precede the metadata methods.
            foreach (MethodInfo method in contract.GetMethods().OrderBy(method => method.MetadataToken))
                Console.WriteLine("ABI_SLOT=" + slot++ + " " + method);
        }
        foreach (MethodInfo method in manager.GetMethods().Where(method => method.DeclaringType == manager)
            .OrderBy(method => method.MetadataToken))
            Console.WriteLine("PROJECTED_METHOD=" + method);
    }

    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 1 && args[0] == "metadata") Metadata();
            else { Console.Error.WriteLine("usage: package-manager-store-inspect.exe metadata"); return 2; }
            Console.WriteLine("PACKAGE_MANAGER_STORE_INSPECTION_PASS");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
